#!/usr/bin/env python3
"""Run repeated RTT MAVLink FTP gates and summarize stability metrics.

The single-round MAVFTP gate proves that list/read/write/delete works once.
This wrapper repeats that gate serially so session-reset, EOF, SDCard, or USB
CDC stalls show up as failed rounds or p95/max latency instead of being hidden
by one lucky transfer.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def script_path(name: str) -> str:
    return str(Path(__file__).resolve().parent / name)


def percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    idx = min(len(ordered) - 1, max(0, int(round((pct / 100.0) * (len(ordered) - 1)))))
    return ordered[idx]


def round_float(value: float | None, digits: int = 3) -> float | None:
    return None if value is None else round(float(value), digits)


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        with path.open("r", encoding="utf-8") as infile:
            payload = json.load(infile)
        return payload if isinstance(payload, dict) else None
    except (OSError, json.JSONDecodeError):
        return None


def step_sum(payload: dict[str, Any]) -> float | None:
    steps = payload.get("steps", [])
    if not isinstance(steps, list):
        return None
    total = 0.0
    seen = False
    for step in steps:
        if isinstance(step, dict) and isinstance(step.get("elapsed_s"), (int, float)):
            total += float(step["elapsed_s"])
            seen = True
    return round(total, 3) if seen else None


def read_bytes(payload: dict[str, Any], key: str) -> int | None:
    value = payload.get(key)
    if isinstance(value, dict) and isinstance(value.get("bytes"), int):
        return int(value["bytes"])
    return None


def run_round(args: argparse.Namespace, outdir: Path, index: int) -> dict[str, Any]:
    round_dir = outdir / f"round_{index:02d}"
    round_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = round_dir / "stdout.log"
    remote_test_path = f"{args.remote_test_prefix}_{index:02d}.tmp"
    cmd = [
        sys.executable or "python3",
        script_path("rtt_mavftp_gate.py"),
        "--port",
        args.port,
        "--outdir",
        str(round_dir),
        "--source-system",
        str(args.source_system + index - 1),
        "--heartbeat-timeout",
        str(args.heartbeat_timeout),
        "--min-param-bytes",
        str(args.min_param_bytes),
        "--test-file-bytes",
        str(args.test_file_bytes),
        "--remote-test-path",
        remote_test_path,
        "--post-ftp-stability",
        str(args.post_ftp_stability),
    ]
    if args.require_sd:
        cmd.append("--require-sd")
    for status in args.accept_status:
        cmd.extend(["--accept-status", str(status)])

    start = time.monotonic()
    with stdout_path.open("w", encoding="utf-8") as stdout:
        proc = subprocess.run(
            ["timeout", str(args.round_timeout_s), *cmd],
            stdout=stdout,
            stderr=subprocess.STDOUT,
            check=False,
        )
    elapsed = time.monotonic() - start
    json_path = round_dir / "mavftp_gate.json"
    payload = load_json(json_path)
    round_result: dict[str, Any] = {
        "round": index,
        "rc": int(proc.returncode),
        "wall_elapsed_s": round(elapsed, 3),
        "stdout": str(stdout_path),
        "json_path": str(json_path),
        "remote_test_path": remote_test_path,
    }
    if payload is None:
        round_result.update({"verdict": "RED", "reason": "missing_or_invalid_json"})
    else:
        param_decode = payload.get("param_pck_decode", {})
        stability = payload.get("post_ftp_stability", {})
        round_result.update({
            "verdict": payload.get("verdict", "RED"),
            "reason": payload.get("reason", ""),
            "step_sum_elapsed_s": step_sum(payload),
            "param_pck_bytes": read_bytes(payload, "read_@PARAM/param.pck"),
            "param_decoded_count": (
                param_decode.get("decoded_count") if isinstance(param_decode, dict) else None
            ),
            "post_ftp_heartbeats": (
                stability.get("heartbeats") if isinstance(stability, dict) else None
            ),
            "post_ftp_stable": (
                stability.get("stable") if isinstance(stability, dict) else None
            ),
            "target_system": payload.get("target_system"),
            "target_component": payload.get("target_component"),
        })
    return round_result


def metric_values(rounds: list[dict[str, Any]], key: str) -> list[float]:
    values: list[float] = []
    for item in rounds:
        value = item.get(key)
        if isinstance(value, (int, float)):
            values.append(float(value))
    return values


def metric_summary(rounds: list[dict[str, Any]], key: str) -> dict[str, Any]:
    values = metric_values(rounds, key)
    return {
        "count": len(values),
        "min": round_float(min(values) if values else None),
        "p50": round_float(percentile(values, 50)),
        "p95": round_float(percentile(values, 95)),
        "max": round_float(max(values) if values else None),
    }


def classify(payload: dict[str, Any], args: argparse.Namespace) -> tuple[str, str]:
    failed = payload["failed_rounds"]
    wall = payload["metrics"]["wall_elapsed_s"]
    param_count = payload["metrics"]["param_decoded_count"]
    param_bytes = payload["metrics"]["param_pck_bytes"]
    heartbeats = payload["metrics"]["post_ftp_heartbeats"]

    if failed:
        return "RED", "round_failed"
    if wall["p95"] is None or wall["max"] is None:
        return "RED", "missing_wall_elapsed_metrics"
    if wall["p95"] > args.max_p95_total:
        return "RED", "p95_total_slow"
    if wall["max"] > args.max_max_total:
        return "RED", "max_total_slow"
    if param_count["min"] is None or param_count["min"] < args.min_param_count:
        return "RED", "param_decode_count_too_low"
    if param_bytes["min"] is None or param_bytes["min"] < args.min_param_bytes:
        return "RED", "param_pck_too_small"
    if heartbeats["min"] is None or heartbeats["min"] < args.min_post_ftp_heartbeats:
        return "RED", "post_ftp_heartbeats_too_low"
    if any(item.get("post_ftp_stable") is not True for item in payload["rounds"]):
        return "RED", "post_ftp_heartbeat_unstable"
    return "GREEN", "mavftp_benchmark_ok"


def run_benchmark(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    rounds = []
    for index in range(1, args.rounds + 1):
        rounds.append(run_round(args, outdir, index))
        if args.inter_round_sleep > 0 and index != args.rounds:
            time.sleep(args.inter_round_sleep)

    failed_rounds = [
        item for item in rounds
        if item.get("rc") != 0 or item.get("verdict") != "GREEN"
    ]
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": args.port,
        "rounds_requested": args.rounds,
        "rounds_completed": len(rounds),
        "rounds": rounds,
        "failed_rounds": [item["round"] for item in failed_rounds],
        "thresholds": {
            "max_p95_total": args.max_p95_total,
            "max_max_total": args.max_max_total,
            "min_param_count": args.min_param_count,
            "min_param_bytes": args.min_param_bytes,
            "min_post_ftp_heartbeats": args.min_post_ftp_heartbeats,
        },
        "metrics": {
            "wall_elapsed_s": metric_summary(rounds, "wall_elapsed_s"),
            "step_sum_elapsed_s": metric_summary(rounds, "step_sum_elapsed_s"),
            "param_pck_bytes": metric_summary(rounds, "param_pck_bytes"),
            "param_decoded_count": metric_summary(rounds, "param_decoded_count"),
            "post_ftp_heartbeats": metric_summary(rounds, "post_ftp_heartbeats"),
        },
    }
    payload["verdict"], payload["reason"] = classify(payload, args)
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--inter-round-sleep", type=float, default=0.3)
    parser.add_argument("--round-timeout-s", type=float, default=220.0)
    parser.add_argument("--heartbeat-timeout", type=float, default=25.0)
    parser.add_argument("--require-sd", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--min-param-bytes", type=int, default=128)
    parser.add_argument("--min-param-count", type=int, default=100)
    parser.add_argument("--test-file-bytes", type=int, default=512)
    parser.add_argument("--remote-test-prefix", default="/APM/rtt_mavftp_bench")
    parser.add_argument("--post-ftp-stability", type=float, default=4.0)
    parser.add_argument("--min-post-ftp-heartbeats", type=int, default=2)
    parser.add_argument("--max-p95-total", type=float, default=45.0)
    parser.add_argument("--max-max-total", type=float, default=60.0)
    parser.add_argument("--source-system", type=int, default=241)
    parser.add_argument("--accept-status", type=int, action="append", default=[3, 4, 5])
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be >= 1")

    payload = run_benchmark(args)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "mavftp_benchmark.json"
    payload["json_path"] = str(json_path)
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
