#!/usr/bin/env python3
"""Serial RTT CUAV v5 acceptance suite.

This wrapper deliberately runs MAVLink gates one at a time.  Concurrent readers
on the same USB CDC endpoint have produced false RED results during bring-up.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from rtt_openocd_guard import cleanup_openocd_quiet

from rtt_usb_port_select import MAVLINK_PORT

DEFAULT_PORT = MAVLINK_PORT
DEFAULT_MANIFEST = "docs/rtt-porting/manifests/cuav_v5_rtt_capabilities.json"

STATIC_CAPABILITY_STATES = {
    "scons_full_build": "manifest_static_evidence",
    "l0_boot_build": "manifest_static_evidence",
    "prearm_flight_readiness": "manifest_boundary",
}


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def script_path(name: str) -> str:
    return str(Path(__file__).resolve().parent / name)


def cleanup_openocd() -> None:
    cleanup_openocd_quiet()

def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_manifest(path: str) -> dict[str, Any]:
    manifest_path = Path(path)
    if not manifest_path.is_absolute():
        manifest_path = repo_root() / manifest_path
    with manifest_path.open("r", encoding="utf-8") as infile:
        manifest = json.load(infile)
    if not isinstance(manifest, dict):
        raise ValueError("manifest_root_not_object")
    return manifest


def run_step(step_spec: dict[str, Any], outdir: Path) -> dict[str, Any]:
    name = step_spec["name"]
    step_dir = outdir / name
    step_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = step_dir / "stdout.log"
    start = time.monotonic()
    with stdout_path.open("w", encoding="utf-8") as stdout:
        proc = subprocess.run(
            ["timeout", str(step_spec["timeout_s"]), *step_spec["cmd"]],
            stdout=stdout,
            stderr=subprocess.STDOUT,
            check=False,
        )
    elapsed = time.monotonic() - start
    cleanup_openocd()
    return {
        "name": name,
        "rc": int(proc.returncode),
        "elapsed_s": round(elapsed, 3),
        "stdout": str(stdout_path),
        "covers": list(step_spec.get("covers", [])),
        "allowed_verdicts": list(step_spec.get("allowed_verdicts", ["GREEN"])),
    }


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        with path.open("r", encoding="utf-8") as infile:
            return json.load(infile)
    except (OSError, json.JSONDecodeError):
        return None


def annotate_step(step: dict[str, Any], json_name: str | None) -> None:
    if not json_name:
        step["verdict"] = "GREEN" if step["rc"] == 0 else "RED"
        step["reason"] = "rc_ok" if step["rc"] == 0 else f"rc={step['rc']}"
        return
    json_path = Path(step["stdout"]).parent / json_name
    step["json_path"] = str(json_path)
    payload = load_json(json_path)
    if payload is None:
        step["verdict"] = "RED"
        step["reason"] = "missing_or_invalid_json"
        return
    step["verdict"] = payload.get("verdict", "RED")
    step["reason"] = payload.get("reason", "")
    if json_name == "source_artifact_audit.json":
        clean = int(payload.get("untracked_count", 1)) == 0 and int(payload.get("tracked_count", 1)) == 0
        step["verdict"] = "GREEN" if clean else "RED"
        step["reason"] = "source_artifact_audit_clean" if clean else "source_artifact_audit_dirty"
    if "reasons" in payload:
        step["reasons"] = payload["reasons"]
    for key in (
        "driver_verdict",
        "calibration_verdict",
        "elapsed_s",
        "rate_params_s",
        "reported_count",
        "unique_indices",
        "missing_count",
        "download",
        "rounds_completed",
        "failed_rounds",
        "metrics",
        "capability_count",
        "errors",
    ):
        if key in payload:
            step[key] = payload[key]


def step_spec(
    name: str,
    cmd: list[str],
    timeout_s: int,
    json_name: str | None,
    covers: list[str],
    allowed_verdicts: list[str] | None = None,
) -> dict[str, Any]:
    return {
        "name": name,
        "cmd": cmd,
        "timeout_s": timeout_s,
        "json_name": json_name,
        "covers": covers,
        "allowed_verdicts": allowed_verdicts or ["GREEN"],
    }


def build_steps(args: argparse.Namespace, outdir: Path) -> list[dict[str, Any]]:
    python = sys.executable or "python3"
    steps: list[dict[str, Any]] = []
    if args.full_chain:
        steps.extend([
            step_spec(
                "manifest_check",
                [
                    python,
                    script_path("rtt_capability_manifest_check.py"),
                    "--manifest",
                    args.manifest,
                    "--out",
                    str(outdir / "manifest_check" / "capability_manifest_check.json"),
                ],
                30,
                "capability_manifest_check.json",
                [],
            ),
            step_spec(
                "qgc_mp_host_evidence",
                [
                    python,
                    script_path("rtt_qgc_mp_evidence.py"),
                    "--out",
                    str(outdir / "qgc_mp_host_evidence" / "qgc_mp_evidence.json"),
                    "--pretty",
                ],
                30,
                "qgc_mp_evidence.json",
                ["qgc_missionplanner_host_evidence"],
                ["GREEN", "YELLOW"] if args.allow_boundary_yellow else ["GREEN"],
            ),
            step_spec(
                "loop_rate",
                [
                    python,
                    script_path("rtt_loop_rate_gate.py"),
                    "--port",
                    args.port,
                    "--outdir",
                    str(outdir / "loop_rate"),
                ],
                180,
                "loop_rate_gate.json",
                ["main_loop_400hz"],
            ),
        ])

    steps.extend([
        step_spec(
            "param_download",
            [
                python,
                script_path("rtt_param_download_gate.py"),
                "--port",
                args.port,
                "--outdir",
                str(outdir / "param_download"),
            ],
            80,
            "param_download.json",
            ["usb_mavlink_cdc_if00"] if args.param_benchmark else ["usb_mavlink_cdc_if00", "usb_param_download_fast"],
        ),
    ])

    if args.param_benchmark:
        steps.append(step_spec(
            "param_download_benchmark",
            [
                python,
                script_path("rtt_param_download_benchmark.py"),
                "--port",
                args.port,
                "--outdir",
                str(outdir / "param_download_benchmark"),
                "--rounds",
                str(args.param_benchmark_rounds),
            ],
            max(120, args.param_benchmark_rounds * 90),
            "param_download_benchmark.json",
            ["usb_param_download_fast"],
        ))

    steps.extend([
        step_spec(
            "mavftp",
            [
                python,
                script_path("rtt_mavftp_gate.py"),
                "--port",
                args.port,
                "--outdir",
                str(outdir / "mavftp"),
                "--require-sd",
            ],
            180,
            "mavftp_gate.json",
            [] if args.mavftp_benchmark else ["mavlink_ftp_sdcard"],
        ),
        *([
            step_spec(
                "mavftp_benchmark",
                [
                    python,
                    script_path("rtt_mavftp_benchmark.py"),
                    "--port",
                    args.port,
                    "--outdir",
                    str(outdir / "mavftp_benchmark"),
                    "--rounds",
                    str(args.mavftp_benchmark_rounds),
                ],
                max(240, args.mavftp_benchmark_rounds * 240),
                "mavftp_benchmark.json",
                ["mavlink_ftp_sdcard"],
            )
        ] if args.mavftp_benchmark else []),
        step_spec(
            "peripheral_driver",
            [
                python,
                script_path("rtt_peripheral_health_gate.py"),
                "--port",
                args.port,
                "--outdir",
                str(outdir / "peripheral_driver"),
            ],
            80,
            "peripheral_health.json",
            [
                "imu_ins_driver_streams",
                "compass_driver_streams",
                "barometer_driver_streams",
                "driver_level_peripheral_health",
            ],
        ),
        step_spec(
            "log_download",
            [
                python,
                script_path("rtt_log_download_gate.py"),
                "--port",
                args.port,
                "--outdir",
                str(outdir / "log_download"),
                "--reset-after-restore",
            ],
            240,
            "log_download_gate.json",
            ["sdcard_dataflash_logging"],
        ),
    ])

    if args.full_chain and args.include_socketcan:
        socketcan_cmd = [
            python,
            script_path("rtt_socketcan_dronecan_gate.py"),
            "--port",
            args.slcan_port,
            "--iface",
            args.socketcan_iface,
            "--outdir",
            str(outdir / "socketcan_dronecan"),
            "--cleanup-existing",
            "--cleanup-after",
            "--require-rx",
            "--require-dronecan",
            "--dronecan-any-node",
        ]
        if args.sudo_socketcan:
            socketcan_cmd.append("--sudo-system-tools")
        steps.append(step_spec(
            "socketcan_dronecan",
            socketcan_cmd,
            240,
            "socketcan_dronecan_gate.json",
            ["usb_slcan_cdc_if02", "socketcan_standard_and_extended", "dronecan_node_status"],
        ))

    if args.full_chain:
        steps.append(step_spec(
            "source_artifact_audit",
            [
                python,
                script_path("rtt_source_artifact_audit.py"),
                "--out",
                str(outdir / "source_artifact_audit" / "source_artifact_audit.json"),
            ],
            80,
            "source_artifact_audit.json",
            ["workspace_artifact_audit"],
        ))

    if args.include_param_persist:
        steps.insert(
            1,
            step_spec(
                "param_persist",
                [
                    python,
                    script_path("rtt_param_persist_gate.py"),
                    "--port",
                    args.port,
                    "--outdir",
                    str(outdir / "param_persist"),
                    "--param",
                    args.persist_param,
                ],
                180,
                "param_persist.json",
                [],
            ),
        )
    if args.strict_prearm_health:
        steps.append(
            step_spec(
                "peripheral_strict_prearm",
                [
                    python,
                    script_path("rtt_peripheral_health_gate.py"),
                    "--port",
                    args.port,
                    "--outdir",
                    str(outdir / "peripheral_strict_prearm"),
                    "--strict-prearm-health",
                ],
                80,
                "peripheral_health.json",
                ["prearm_flight_readiness"],
                ["GREEN", "RED"] if args.allow_strict_prearm_red else ["GREEN"],
            )
        )
    return steps


def step_is_allowed(step: dict[str, Any]) -> bool:
    return step.get("rc") == 0 and step.get("verdict") in step.get("allowed_verdicts", ["GREEN"])


def build_manifest_matrix(manifest: dict[str, Any], steps: list[dict[str, Any]]) -> dict[str, Any]:
    by_capability: dict[str, list[dict[str, Any]]] = {}
    for step in steps:
        for cap_id in step.get("covers", []):
            by_capability.setdefault(cap_id, []).append(step)

    entries = []
    missing_required = []
    failed_required = []
    boundary_required = []
    for cap in manifest.get("capabilities", []):
        cap_id = str(cap.get("id", ""))
        required = bool(cap.get("required_for_chibios_replacement"))
        cap_steps = by_capability.get(cap_id, [])
        if cap_steps:
            status = "covered_passed" if all(step_is_allowed(step) for step in cap_steps) else "covered_failed"
            if status == "covered_failed" and required:
                failed_required.append(cap_id)
        elif cap_id in STATIC_CAPABILITY_STATES:
            status = STATIC_CAPABILITY_STATES[cap_id]
            if status == "manifest_boundary" and required:
                boundary_required.append(cap_id)
        else:
            status = "not_covered"
            if required:
                missing_required.append(cap_id)
        entries.append({
            "id": cap_id,
            "category": cap.get("category"),
            "manifest_status": cap.get("status"),
            "required_for_chibios_replacement": required,
            "coverage_status": status,
            "steps": [step["name"] for step in cap_steps],
        })

    return {
        "entries": entries,
        "covered_count": sum(1 for e in entries if e["coverage_status"].startswith("covered_")),
        "manifest_static_count": sum(1 for e in entries if e["coverage_status"].startswith("manifest_")),
        "missing_required": missing_required,
        "failed_required": failed_required,
        "boundary_required": boundary_required,
    }


def run_round(args: argparse.Namespace, outdir: Path, round_index: int) -> dict[str, Any]:
    round_dir = outdir / f"round_{round_index:02d}" if args.rounds > 1 else outdir
    round_dir.mkdir(parents=True, exist_ok=True)
    payload: dict[str, Any] = {
        "round": round_index,
        "timestamp_utc": iso_now(),
        "port": args.port,
        "outdir": str(round_dir),
        "steps": [],
    }
    for spec in build_steps(args, round_dir):
        step = run_step(spec, round_dir)
        annotate_step(step, spec.get("json_name"))
        payload["steps"].append(step)
        if not step_is_allowed(step):
            payload["verdict"] = "RED"
            payload["reason"] = f"{step['name']}_failed:{step.get('reason', 'rc=' + str(step['rc']))}"
            return payload

    payload["verdict"] = "GREEN"
    payload["reason"] = "round_ok"
    return payload


def run_suite(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    manifest = load_manifest(args.manifest) if args.full_chain else None
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": args.port,
        "outdir": str(outdir),
        "rounds_requested": args.rounds,
        "full_chain": bool(args.full_chain),
        "notes": [
            "All MAVLink gates are run serially to avoid false REDs from competing CDC readers.",
            "Default peripheral pass/fail covers driver/data/logging health; use --strict-prearm-health for AHRS calibration closure.",
        ],
    }

    rounds = []
    for round_index in range(1, args.rounds + 1):
        round_payload = run_round(args, outdir, round_index)
        rounds.append(round_payload)
        if round_payload.get("verdict") != "GREEN":
            break
    payload["rounds"] = rounds
    payload["steps"] = rounds[-1]["steps"] if rounds else []

    if manifest is not None and rounds:
        matrix = build_manifest_matrix(manifest, rounds[-1]["steps"])
        payload["manifest_id"] = manifest.get("manifest_id")
        payload["manifest_matrix"] = matrix
    else:
        matrix = None

    failed_rounds = [r for r in rounds if r.get("verdict") != "GREEN"]
    if failed_rounds:
        payload["verdict"] = "RED"
        payload["reason"] = f"round_{failed_rounds[0]['round']:02d}_failed:{failed_rounds[0].get('reason', '')}"
    elif matrix and (matrix["missing_required"] or matrix["failed_required"]):
        payload["verdict"] = "RED"
        payload["reason"] = "manifest_required_coverage_failed"
    else:
        payload["verdict"] = "GREEN"
        payload["reason"] = "acceptance_suite_ok" if not args.full_chain else "manifest_full_chain_acceptance_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Serial RTT CUAV v5 acceptance suite")
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--slcan-port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--full-chain", action="store_true",
                        help="run manifest-driven full-chain gates instead of the legacy MAVLink-only subset")
    parser.add_argument("--rounds", type=int, default=1,
                        help="run the selected suite N times serially")
    parser.add_argument("--include-socketcan", action="store_true",
                        help="include SLCAN->SocketCAN->DroneCAN live bench gate")
    parser.add_argument("--socketcan-iface", default="can_rtt0")
    parser.add_argument("--sudo-socketcan", action="store_true",
                        help="run slcand/ip through sudo in the SocketCAN gate")
    parser.add_argument("--param-benchmark", action="store_true",
                        help="replace single-round fast-param proof with repeated p50/p95 benchmark coverage")
    parser.add_argument("--param-benchmark-rounds", type=int, default=5)
    parser.add_argument("--mavftp-benchmark", action="store_true",
                        help="replace single-round MAVFTP stability proof with repeated p50/p95 benchmark coverage")
    parser.add_argument("--mavftp-benchmark-rounds", type=int, default=5)
    parser.add_argument("--allow-boundary-yellow", action=argparse.BooleanOptionalAction, default=True,
                        help="allow YELLOW evidence for manifest boundary host GUI checks")
    parser.add_argument("--include-param-persist", action="store_true",
                        help="include PARAM_SET persistence reset test")
    parser.add_argument("--persist-param", default="LOG_DISARMED")
    parser.add_argument("--strict-prearm-health", action="store_true",
                        help="also run AHRS/pre-arm calibration health as a strict gate")
    parser.add_argument("--allow-strict-prearm-red", action="store_true",
                        help="record strict pre-arm RED without failing the suite")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be >= 1")
    if args.param_benchmark_rounds < 1:
        parser.error("--param-benchmark-rounds must be >= 1")
    if args.mavftp_benchmark_rounds < 1:
        parser.error("--mavftp-benchmark-rounds must be >= 1")

    payload = run_suite(args)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "acceptance_suite.json"
    payload["json_path"] = str(json_path)
    with json_path.open("w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
