#!/usr/bin/env python3
"""Run repeated RTT MAVLink parameter-download gates and summarize p50/p95.

The single-round gate proves that one PARAM_REQUEST_LIST transfer can complete
quickly.  This wrapper repeats that gate serially so rare CDC stalls show up as
p95/max latency or failed rounds instead of being hidden by a lucky sample.
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


def run_round(args: argparse.Namespace, outdir: Path, index: int) -> dict[str, Any]:
    round_dir = outdir / f"round_{index:02d}"
    round_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = round_dir / "stdout.log"
    cmd = [
        sys.executable or "python3",
        script_path("rtt_param_download_gate.py"),
        "--port",
        args.port,
        "--outdir",
        str(round_dir),
        "--timeout",
        str(args.timeout),
        "--settle-timeout",
        str(args.settle_timeout),
        "--drain",
        str(args.drain),
        "--max-total",
        str(args.round_max_total),
        "--max-gap",
        str(args.round_max_gap),
        "--min-rate",
        str(args.round_min_rate),
        "--source-system",
        str(args.source_system + index - 1),
    ]
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
    json_path = round_dir / "param_download.json"
    payload = load_json(json_path)
    round_result: dict[str, Any] = {
        "round": index,
        "rc": int(proc.returncode),
        "wall_elapsed_s": round(elapsed, 3),
        "stdout": str(stdout_path),
        "json_path": str(json_path),
    }
    if payload is None:
        round_result.update({"verdict": "RED", "reason": "missing_or_invalid_json"})
    else:
        round_result.update({
            "verdict": payload.get("verdict", "RED"),
            "reason": payload.get("reason", ""),
            "reported_count": payload.get("reported_count"),
            "unique_indices": payload.get("unique_indices"),
            "missing_count": payload.get("missing_count"),
            "elapsed_s": payload.get("elapsed_s"),
            "first_response_latency_s": payload.get("first_response_latency_s"),
            "max_gap_s_observed": payload.get("max_gap_s_observed"),
            "p95_gap_s": payload.get("p95_gap_s"),
            "rate_params_s": payload.get("rate_params_s"),
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
    elapsed = payload["metrics"]["elapsed_s"]
    max_gap = payload["metrics"]["max_gap_s_observed"]
    rate = payload["metrics"]["rate_params_s"]

    if failed:
        return "RED", "round_failed"
    if elapsed["p95"] is None or elapsed["max"] is None:
        return "RED", "missing_elapsed_metrics"
    if elapsed["p95"] > args.max_p95_total:
        return "RED", "p95_total_slow"
    if elapsed["max"] > args.max_max_total:
        return "RED", "max_total_slow"
    if max_gap["p95"] is not None and max_gap["p95"] > args.max_p95_gap:
        return "RED", "p95_gap_too_large"
    if max_gap["max"] is not None and max_gap["max"] > args.max_max_gap:
        return "RED", "max_gap_too_large"
    if rate["p50"] is not None and rate["p50"] < args.min_p50_rate:
        return "RED", "p50_rate_too_low"
    return "GREEN", "param_download_benchmark_ok"


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
            "max_p95_gap": args.max_p95_gap,
            "max_max_gap": args.max_max_gap,
            "min_p50_rate": args.min_p50_rate,
        },
        "metrics": {
            "elapsed_s": metric_summary(rounds, "elapsed_s"),
            "first_response_latency_s": metric_summary(rounds, "first_response_latency_s"),
            "max_gap_s_observed": metric_summary(rounds, "max_gap_s_observed"),
            "p95_gap_s": metric_summary(rounds, "p95_gap_s"),
            "rate_params_s": metric_summary(rounds, "rate_params_s"),
            "missing_count": metric_summary(rounds, "missing_count"),
        },
    }
    payload["verdict"], payload["reason"] = classify(payload, args)
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--inter-round-sleep", type=float, default=0.2)
    parser.add_argument("--round-timeout-s", type=float, default=80.0)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--settle-timeout", type=float, default=30.0)
    parser.add_argument("--drain", type=float, default=0.5)
    parser.add_argument("--round-max-total", type=float, default=12.0)
    parser.add_argument("--round-max-gap", type=float, default=1.0)
    parser.add_argument("--round-min-rate", type=float, default=90.0)
    parser.add_argument("--max-p95-total", type=float, default=4.0)
    parser.add_argument("--max-max-total", type=float, default=6.0)
    parser.add_argument("--max-p95-gap", type=float, default=0.2)
    parser.add_argument("--max-max-gap", type=float, default=0.5)
    parser.add_argument("--min-p50-rate", type=float, default=300.0)
    parser.add_argument("--source-system", type=int, default=245)
    parser.add_argument("--accept-status", type=int, action="append", default=[3, 4, 5])
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be >= 1")

    payload = run_benchmark(args)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "param_download_benchmark.json"
    payload["json_path"] = str(json_path)
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
