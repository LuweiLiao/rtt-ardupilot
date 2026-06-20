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

from rtt_usb_port_select import MAVLINK_PORT

DEFAULT_PORT = MAVLINK_PORT


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def script_path(name: str) -> str:
    return str(Path(__file__).resolve().parent / name)


def cleanup_openocd() -> None:
    subprocess.run(["pkill", "-9", "-x", "openocd"], check=False)
    subprocess.run(["pkill", "-9", "-f", "[o]penoccd"], check=False)


def run_step(name: str, cmd: list[str], outdir: Path, timeout_s: int) -> dict[str, Any]:
    step_dir = outdir / name
    step_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = step_dir / "stdout.log"
    start = time.monotonic()
    with stdout_path.open("w", encoding="utf-8") as stdout:
        proc = subprocess.run(
            ["timeout", str(timeout_s), *cmd],
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
    }


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        with path.open("r", encoding="utf-8") as infile:
            return json.load(infile)
    except (OSError, json.JSONDecodeError):
        return None


def annotate_step(step: dict[str, Any], json_name: str) -> None:
    json_path = Path(step["stdout"]).parent / json_name
    step["json_path"] = str(json_path)
    payload = load_json(json_path)
    if payload is None:
        step["verdict"] = "RED"
        step["reason"] = "missing_or_invalid_json"
        return
    step["verdict"] = payload.get("verdict", "RED")
    step["reason"] = payload.get("reason", "")
    for key in (
        "driver_verdict",
        "calibration_verdict",
        "elapsed_s",
        "rate_params_s",
        "reported_count",
        "unique_indices",
        "missing_count",
        "download",
    ):
        if key in payload:
            step[key] = payload[key]


def build_steps(args: argparse.Namespace, outdir: Path) -> list[tuple[str, list[str], int, str]]:
    python = sys.executable or "python3"
    steps: list[tuple[str, list[str], int, str]] = [
        (
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
        ),
        (
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
        ),
        (
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
        ),
        (
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
        ),
    ]
    if args.include_param_persist:
        steps.insert(
            1,
            (
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
            ),
        )
    if args.strict_prearm_health:
        steps.append(
            (
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
            )
        )
    return steps


def run_suite(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": args.port,
        "outdir": str(outdir),
        "steps": [],
        "notes": [
            "All MAVLink gates are run serially to avoid false REDs from competing CDC readers.",
            "Default peripheral pass/fail covers driver/data/logging health; use --strict-prearm-health for AHRS calibration closure.",
        ],
    }
    for name, cmd, timeout_s, json_name in build_steps(args, outdir):
        step = run_step(name, cmd, outdir, timeout_s)
        annotate_step(step, json_name)
        payload["steps"].append(step)
        if step["rc"] != 0 and not (name == "peripheral_strict_prearm" and args.allow_strict_prearm_red):
            payload["verdict"] = "RED"
            payload["reason"] = f"{name}_failed:{step.get('reason', 'rc=' + str(step['rc']))}"
            return payload

    red_steps = [
        step for step in payload["steps"]
        if step.get("verdict") != "GREEN" and not (
            step["name"] == "peripheral_strict_prearm" and args.allow_strict_prearm_red
        )
    ]
    if red_steps:
        payload["verdict"] = "RED"
        payload["reason"] = "step_verdict_red"
    else:
        payload["verdict"] = "GREEN"
        payload["reason"] = "acceptance_suite_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Serial RTT CUAV v5 acceptance suite")
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--include-param-persist", action="store_true",
                        help="include PARAM_SET persistence reset test")
    parser.add_argument("--persist-param", default="LOG_DISARMED")
    parser.add_argument("--strict-prearm-health", action="store_true",
                        help="also run AHRS/pre-arm calibration health as a strict gate")
    parser.add_argument("--allow-strict-prearm-red", action="store_true",
                        help="record strict pre-arm RED without failing the suite")
    args = parser.parse_args()

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
