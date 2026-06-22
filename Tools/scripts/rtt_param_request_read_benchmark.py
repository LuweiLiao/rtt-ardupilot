#!/usr/bin/env python3
"""RTT MAVLink PARAM_REQUEST_READ repeat-read benchmark.

This benchmark measures repeated single-parameter reads over the normal
PARAM_REQUEST_READ/PARAM_VALUE path.  It is intended to catch rare tail
latency or gap regressions that would not show up in a one-off read.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pymavlink import mavutil

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port

DEFAULT_PORT = MAVLINK_PORT
DEFAULT_PARAM_CANDIDATES = ("LOG_DISARMED", "SCHED_LOOP_RATE", "AHRS_EKF_TYPE")


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(port_arg: str) -> str:
    return resolve_mavlink_port(port_arg)


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def drain(conn: Any, seconds: float = 0.3) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def connect(port: str, source_system: int = 245, timeout_s: float = 25.0) -> Any:
    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=source_system,
    )
    hb = conn.wait_heartbeat(timeout=timeout_s)
    if hb is None or conn.target_system == 0:
        conn.close()
        raise RuntimeError("no_heartbeat")
    return conn


def connect_retry(port: str, source_system: int, attempts: int = 6,
                  timeout_s: float = 12.0, gap_s: float = 2.0) -> Any:
    last_error: str | None = None
    for _ in range(attempts):
        try:
            return connect(port, source_system=source_system, timeout_s=timeout_s)
        except Exception as exc:
            last_error = str(exc)
            time.sleep(gap_s)
    raise RuntimeError(f"connect_retry_failed:{last_error}")


def request_param_direct(conn: Any, name: str, timeout_s: float = 8.0,
                         drain_seconds: float = 0.2) -> float:
    if drain_seconds > 0:
        drain(conn, drain_seconds)
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        -1,
    )
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.8)
        if msg is not None and param_name(msg) == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_read_timeout:{name}")


def request_param_from_list(conn: Any, name: str, timeout_s: float = 45.0,
                            drain_seconds: float = 0.4) -> float:
    if drain_seconds > 0:
        drain(conn, drain_seconds)
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    deadline = time.monotonic() + timeout_s
    reported_count: int | None = None
    seen = 0
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1.0)
        if msg is None:
            continue
        seen += 1
        reported_count = int(msg.param_count)
        if param_name(msg) == name:
            return float(msg.param_value)
        if reported_count > 0 and seen >= reported_count:
            break
    raise RuntimeError(f"param_list_timeout:{name}:seen={seen}:count={reported_count}")


def read_param(conn: Any, name: str, timeout_s: float = 8.0,
               drain_seconds: float = 0.2) -> float:
    try:
        return request_param_direct(conn, name, timeout_s=timeout_s, drain_seconds=drain_seconds)
    except Exception:
        return request_param_from_list(conn, name, drain_seconds=drain_seconds)


def percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    idx = min(len(ordered) - 1, max(0, int(round((pct / 100.0) * (len(ordered) - 1)))))
    return ordered[idx]


def round_float(value: float | None, digits: int = 3) -> float | None:
    return None if value is None else round(float(value), digits)


def run_round(args: argparse.Namespace, outdir: Path, index: int, param_name_value: str) -> dict[str, Any]:
    round_dir = outdir / f"round_{index:02d}"
    round_dir.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    conn = connect_retry(args.port, source_system=args.source_system + index - 1)
    try:
        drain(conn, args.round_drain_seconds)
        values: list[float] = []
        latencies: list[float] = []
        gaps: list[float] = []
        errors: list[str] = []
        last_rx: float | None = None
        for _ in range(args.reads_per_round):
            read_start = time.monotonic()
            try:
                value = read_param(conn, param_name_value, timeout_s=args.read_timeout, drain_seconds=0.0)
                read_end = time.monotonic()
                values.append(float(value))
                latencies.append(read_end - read_start)
                if last_rx is not None:
                    gaps.append(read_end - last_rx)
                last_rx = read_end
            except Exception as exc:
                errors.append(str(exc))
                break
        payload = {
            "round": index,
            "param": param_name_value,
            "reads_requested": args.reads_per_round,
            "reads_completed": len(values),
            "values": values[:5],
            "errors": errors,
            "elapsed_s": round(time.monotonic() - start, 3),
            "first_response_latency_s": round_float(latencies[0] if latencies else None),
            "max_gap_s_observed": round_float(max(gaps) if gaps else 0.0),
            "p95_gap_s": round_float(percentile(gaps, 95)),
            "read_latency_s": {
                "min": round_float(min(latencies) if latencies else None),
                "p50": round_float(percentile(latencies, 50)),
                "p95": round_float(percentile(latencies, 95)),
                "max": round_float(max(latencies) if latencies else None),
            },
            "value_unique_count": len(set(values)),
            "json_path": str(round_dir / "param_request_read_round.json"),
        }
        payload["verdict"] = "GREEN" if not errors and len(values) == args.reads_per_round else "RED"
        payload["reason"] = "repeat_read_ok" if payload["verdict"] == "GREEN" else "repeat_read_failed"
        (round_dir / "param_request_read_round.json").write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return payload
    finally:
        conn.close()


def classify(payload: dict[str, Any], args: argparse.Namespace) -> tuple[str, str]:
    failed_rounds = payload["failed_rounds"]
    lat = payload["metrics"]["read_latency_s"]
    gap = payload["metrics"]["max_gap_s_observed"]

    if failed_rounds:
        return "RED", "round_failed"
    if lat["p95"] is None or lat["max"] is None:
        return "RED", "missing_latency_metrics"
    if lat["p95"] > args.max_p95_read_s:
        return "RED", "p95_read_slow"
    if lat["max"] > args.max_max_read_s:
        return "RED", "max_read_slow"
    if gap["p95"] is not None and gap["p95"] > args.max_p95_gap_s:
        return "RED", "p95_gap_too_large"
    if gap["max"] is not None and gap["max"] > args.max_max_gap_s:
        return "RED", "max_gap_too_large"
    return "GREEN", "param_request_read_benchmark_ok"


def run_benchmark(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    candidates = tuple(p.strip() for p in args.param.split(",") if p.strip()) or DEFAULT_PARAM_CANDIDATES
    param_name_value = candidates[0]
    port = resolve_port(args.port)
    args = argparse.Namespace(**{**vars(args), "port": port})

    rounds: list[dict[str, Any]] = []
    for index in range(1, args.rounds + 1):
        rounds.append(run_round(args, outdir, index, param_name_value))
        if args.inter_round_sleep > 0 and index != args.rounds:
            time.sleep(args.inter_round_sleep)

    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "param": param_name_value,
        "rounds_requested": args.rounds,
        "rounds_completed": len(rounds),
        "rounds": rounds,
        "failed_rounds": [item.get("round") for item in rounds if item.get("verdict") != "GREEN"],
        "thresholds": {
            "max_p95_read_s": args.max_p95_read_s,
            "max_max_read_s": args.max_max_read_s,
            "max_p95_gap_s": args.max_p95_gap_s,
            "max_max_gap_s": args.max_max_gap_s,
        },
        "metrics": {
            "elapsed_s": {
                "min": round_float(min(item["elapsed_s"] for item in rounds) if rounds else None),
                "p50": round_float(percentile([float(item["elapsed_s"]) for item in rounds], 50)),
                "p95": round_float(percentile([float(item["elapsed_s"]) for item in rounds], 95)),
                "max": round_float(max(item["elapsed_s"] for item in rounds) if rounds else None),
            },
            "first_response_latency_s": {
                "min": round_float(min(item["first_response_latency_s"] for item in rounds if item["first_response_latency_s"] is not None) if any(item["first_response_latency_s"] is not None for item in rounds) else None),
                "p50": round_float(percentile([float(item["first_response_latency_s"]) for item in rounds if item["first_response_latency_s"] is not None], 50)),
                "p95": round_float(percentile([float(item["first_response_latency_s"]) for item in rounds if item["first_response_latency_s"] is not None], 95)),
                "max": round_float(max(item["first_response_latency_s"] for item in rounds if item["first_response_latency_s"] is not None) if any(item["first_response_latency_s"] is not None for item in rounds) else None),
            },
            "max_gap_s_observed": {
                "min": round_float(min(item["max_gap_s_observed"] for item in rounds) if rounds else None),
                "p50": round_float(percentile([float(item["max_gap_s_observed"]) for item in rounds], 50)),
                "p95": round_float(percentile([float(item["max_gap_s_observed"]) for item in rounds], 95)),
                "max": round_float(max(item["max_gap_s_observed"] for item in rounds) if rounds else None),
            },
            "read_latency_s": {
                "min": round_float(min(item["read_latency_s"]["min"] for item in rounds if item["read_latency_s"]["min"] is not None) if any(item["read_latency_s"]["min"] is not None for item in rounds) else None),
                "p50": round_float(percentile([float(item["read_latency_s"]["p50"]) for item in rounds if item["read_latency_s"]["p50"] is not None], 50)),
                "p95": round_float(percentile([float(item["read_latency_s"]["p95"]) for item in rounds if item["read_latency_s"]["p95"] is not None], 95)),
                "max": round_float(max(item["read_latency_s"]["max"] for item in rounds if item["read_latency_s"]["max"] is not None) if any(item["read_latency_s"]["max"] is not None for item in rounds) else None),
            },
            "value_unique_count": {
                "min": min(item["value_unique_count"] for item in rounds) if rounds else None,
                "max": max(item["value_unique_count"] for item in rounds) if rounds else None,
            },
        },
    }
    payload["verdict"], payload["reason"] = classify(payload, args)
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--param", default=",".join(DEFAULT_PARAM_CANDIDATES))
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--reads-per-round", type=int, default=20)
    parser.add_argument("--inter-round-sleep", type=float, default=0.1)
    parser.add_argument("--round-drain-seconds", type=float, default=0.3)
    parser.add_argument("--read-timeout", type=float, default=8.0)
    parser.add_argument("--round-timeout-s", type=float, default=120.0)
    parser.add_argument("--max-p95-read-s", type=float, default=0.05)
    parser.add_argument("--max-max-read-s", type=float, default=0.10)
    parser.add_argument("--max-p95-gap-s", type=float, default=0.05)
    parser.add_argument("--max-max-gap-s", type=float, default=0.15)
    parser.add_argument("--source-system", type=int, default=244)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be >= 1")
    if args.reads_per_round < 1:
        parser.error("--reads-per-round must be >= 1")

    try:
        payload = run_benchmark(args)
        rc = 0 if payload.get("verdict") == "GREEN" else 2
    except Exception as exc:
        payload = {
            "timestamp_utc": iso_now(),
            "port": args.port,
            "verdict": "RED",
            "reason": str(exc),
        }
        rc = 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "param_request_read_benchmark.json"
    payload["json_path"] = str(json_path)
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
