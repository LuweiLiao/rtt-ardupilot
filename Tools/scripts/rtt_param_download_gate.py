#!/usr/bin/env python3
"""RTT MAVLink full-parameter download speed gate.

This gate measures the normal PARAM_REQUEST_LIST/PARAM_VALUE path used by GCS
parameter downloads.  It is intentionally read-only: no parameters are changed
and no firmware debug hooks are required.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time
from datetime import datetime, timezone
from typing import Any

from pymavlink import mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(port_arg: str) -> str:
    if port_arg != "auto":
        return port_arg
    if os.path.exists(DEFAULT_PORT):
        return DEFAULT_PORT
    for pattern in (
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC*",
        "/dev/serial/by-id/*CUAV*CDC*",
        "/dev/serial/by-id/*ArduPilot*",
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    acms = sorted(glob.glob("/dev/ttyACM*"))
    if acms:
        return acms[-1]
    raise RuntimeError("no_cdc_port")


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def drain(conn: Any, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def wait_standby(conn: Any, timeout_s: float) -> tuple[bool, float, int | None]:
    start = time.monotonic()
    last_status = None
    while time.monotonic() - start < timeout_s:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if msg is None:
            continue
        last_status = int(msg.system_status)
        if last_status == 3:
            return True, time.monotonic() - start, last_status
    return False, time.monotonic() - start, last_status


def percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    idx = min(len(ordered) - 1, max(0, int(round((pct / 100.0) * (len(ordered) - 1)))))
    return ordered[idx]


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "timeout_s": args.timeout,
        "max_total_s": args.max_total,
        "max_gap_s": args.max_gap,
        "min_rate_params_s": args.min_rate,
    }

    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=args.source_system,
    )
    try:
        standby_ok, settle_s, last_status = wait_standby(conn, args.settle_timeout)
        payload.update({
            "standby_ok": standby_ok,
            "settle_s": round(settle_s, 3),
            "last_system_status": last_status,
            "target_system": int(conn.target_system),
            "target_component": int(conn.target_component),
        })
        if not standby_ok or conn.target_system == 0:
            payload.update({"verdict": "RED", "reason": "no_standby"})
            return payload

        drain(conn, args.drain)
        start = time.monotonic()
        conn.mav.param_request_list_send(conn.target_system, conn.target_component)

        first_t: float | None = None
        last_t: float | None = None
        reported_count: int | None = None
        total_messages = 0
        invalid_indices = 0
        duplicate_indices = 0
        seen_indices: set[int] = set()
        seen_names: set[str] = set()
        gaps: list[float] = []
        last_msg_summary: dict[str, Any] | None = None

        deadline = start + args.timeout
        while time.monotonic() < deadline:
            msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1.0)
            if msg is None:
                continue

            now = time.monotonic()
            if first_t is None:
                first_t = now
            if last_t is not None:
                gaps.append(now - last_t)
            last_t = now

            total_messages += 1
            idx = int(msg.param_index)
            count = int(msg.param_count)
            name = param_name(msg)
            seen_names.add(name)
            if count > 0:
                reported_count = count
            if idx < 0 or (reported_count is not None and idx >= reported_count):
                invalid_indices += 1
            elif idx in seen_indices:
                duplicate_indices += 1
            else:
                seen_indices.add(idx)

            last_msg_summary = {
                "param_id": name,
                "param_index": idx,
                "param_count": count,
            }

            if reported_count is not None and len(seen_indices) >= reported_count:
                break

        end = last_t if last_t is not None else time.monotonic()
        elapsed = end - start
        first_latency = None if first_t is None else first_t - start
        unique_indices = len(seen_indices)
        missing_count = None
        missing_first: list[int] = []
        if reported_count is not None:
            missing = [idx for idx in range(reported_count) if idx not in seen_indices]
            missing_count = len(missing)
            missing_first = missing[:20]

        rate = unique_indices / elapsed if elapsed > 0 else 0.0
        max_gap = max(gaps) if gaps else 0.0
        approx_payload_bytes = total_messages * 25
        approx_wire_bytes = total_messages * 33
        payload.update({
            "reported_count": reported_count,
            "unique_indices": unique_indices,
            "unique_names": len(seen_names),
            "total_param_value_messages": total_messages,
            "duplicate_indices": duplicate_indices,
            "invalid_indices": invalid_indices,
            "missing_count": missing_count,
            "missing_first_indices": missing_first,
            "elapsed_s": round(elapsed, 3),
            "first_response_latency_s": None if first_latency is None else round(first_latency, 3),
            "max_gap_s_observed": round(max_gap, 3),
            "p95_gap_s": None if percentile(gaps, 95) is None else round(float(percentile(gaps, 95)), 3),
            "rate_params_s": round(rate, 1),
            "approx_payload_bytes": approx_payload_bytes,
            "approx_wire_bytes": approx_wire_bytes,
            "approx_wire_bytes_s": round(approx_wire_bytes / elapsed, 1) if elapsed > 0 else 0.0,
            "last_msg": last_msg_summary,
        })

        complete = reported_count is not None and unique_indices >= reported_count and missing_count == 0
        fast_enough = elapsed <= args.max_total and max_gap <= args.max_gap and rate >= args.min_rate
        if not complete:
            payload.update({"verdict": "RED", "reason": "incomplete"})
        elif not fast_enough:
            payload.update({"verdict": "RED", "reason": "slow_or_gappy"})
        else:
            payload.update({"verdict": "GREEN", "reason": "complete_fast"})
        return payload
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="RTT full parameter download speed gate")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--settle-timeout", type=float, default=30.0)
    parser.add_argument("--drain", type=float, default=0.5)
    parser.add_argument("--max-total", type=float, default=12.0)
    parser.add_argument("--max-gap", type=float, default=1.0)
    parser.add_argument("--min-rate", type=float, default=90.0)
    parser.add_argument("--source-system", type=int, default=245)
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    try:
        payload = run_gate(args)
    except Exception as exc:
        payload = {
            "timestamp_utc": iso_now(),
            "port": args.port,
            "verdict": "RED",
            "reason": str(exc),
        }

    json_path = os.path.join(args.outdir, "param_download.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
