#!/usr/bin/env python3
"""Stress RTT USB CDC open/close behavior around interrupted MAVLink traffic.

This gate mimics the failure shape seen on Windows/Mission Planner: open the
CDC ACM port, wait for MAVLink, start a parameter burst, close the port early,
and repeat.  A final full parameter download proves the CDC TX state machine
recovers after interrupted hosts.
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

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port

DEFAULT_PORT = MAVLINK_PORT


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(port_arg: str) -> str:
    if port_arg != "auto":
        return port_arg
    for pattern in (
        "/dev/serial/by-id/usb-ArduPilot_CUAV_V5_MAVLink_CDC_RTT5741M-if00",
        "/dev/serial/by-id/usb-ArduPilot_*_RTT5741*-if00",
        "/dev/serial/by-id/usb-ArduPilot_CUAVv5_*if00",
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[-1]
    return resolve_mavlink_port(port_arg)


def close_conn(conn: Any | None) -> None:
    if conn is None:
        return
    try:
        conn.close()
    except Exception:
        pass


def wait_heartbeat(conn: Any, timeout_s: float) -> dict[str, Any]:
    start = time.monotonic()
    msg = conn.wait_heartbeat(timeout=timeout_s)
    elapsed = time.monotonic() - start
    if msg is None:
        raise RuntimeError("heartbeat_timeout")
    return {
        "elapsed_s": round(elapsed, 3),
        "system_status": int(msg.system_status),
        "target_system": int(conn.target_system),
        "target_component": int(conn.target_component),
    }


def interrupted_param_round(port: str, args: argparse.Namespace, idx: int) -> dict[str, Any]:
    conn = None
    result: dict[str, Any] = {
        "round": idx,
        "opened": False,
        "heartbeat": None,
        "param_values_before_close": 0,
        "bytes_read_hint": 0,
        "closed_cleanly": False,
        "error": None,
    }
    try:
        conn = mavutil.mavlink_connection(
            port,
            baud=args.baud,
            robust_parsing=True,
            autoreconnect=False,
            source_system=args.source_system + idx,
        )
        result["opened"] = True
        hb = wait_heartbeat(conn, args.heartbeat_timeout)
        result["heartbeat"] = hb
        conn.mav.param_request_list_send(conn.target_system, conn.target_component)
        if args.no_read_after_request:
            time.sleep(args.hold_s)
            return result
        deadline = time.monotonic() + args.hold_s
        while time.monotonic() < deadline:
            msg = conn.recv_match(blocking=True, timeout=0.05)
            if msg is None:
                continue
            mtype = msg.get_type()
            if mtype == "PARAM_VALUE":
                result["param_values_before_close"] += 1
            result["bytes_read_hint"] += len(msg.get_msgbuf() or b"")
        return result
    except Exception as exc:  # noqa: BLE001
        result["error"] = repr(exc)
        return result
    finally:
        close_conn(conn)
        result["closed_cleanly"] = True
        time.sleep(args.gap_s)


def final_param_download(port: str, args: argparse.Namespace) -> dict[str, Any]:
    conn = mavutil.mavlink_connection(
        port,
        baud=args.baud,
        robust_parsing=True,
        autoreconnect=False,
        source_system=args.source_system + 90,
    )
    try:
        hb = wait_heartbeat(conn, args.heartbeat_timeout)
        start = time.monotonic()
        conn.mav.param_request_list_send(conn.target_system, conn.target_component)
        reported_count: int | None = None
        seen: set[int] = set()
        deadline = start + args.final_timeout
        while time.monotonic() < deadline:
            msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
            if msg is None:
                continue
            idx = int(msg.param_index)
            count = int(msg.param_count)
            if count > 0:
                reported_count = count
            if idx >= 0:
                seen.add(idx)
            if reported_count is not None and len(seen) >= reported_count:
                break
        elapsed = time.monotonic() - start
        complete = reported_count is not None and len(seen) >= reported_count
        return {
            "heartbeat": hb,
            "reported_count": reported_count,
            "unique_indices": len(seen),
            "elapsed_s": round(elapsed, 3),
            "rate_params_s": round(len(seen) / elapsed, 1) if elapsed > 0 else 0.0,
            "complete": complete,
        }
    finally:
        close_conn(conn)


def run(args: argparse.Namespace) -> dict[str, Any]:
    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "rounds_requested": args.rounds,
        "hold_s": args.hold_s,
        "gap_s": args.gap_s,
        "no_read_after_request": args.no_read_after_request,
        "rounds": [],
    }

    for idx in range(args.rounds):
        payload["rounds"].append(interrupted_param_round(port, args, idx))

    payload["final_param_download"] = final_param_download(port, args)
    errors = [r for r in payload["rounds"] if r.get("error")]
    final_ok = bool(payload["final_param_download"].get("complete"))
    fast_ok = float(payload["final_param_download"].get("rate_params_s") or 0.0) >= args.min_final_rate
    if errors:
        payload.update({"verdict": "RED", "reason": "interrupted_round_errors"})
    elif not final_ok:
        payload.update({"verdict": "RED", "reason": "final_param_incomplete"})
    elif not fast_ok:
        payload.update({"verdict": "RED", "reason": "final_param_slow"})
    else:
        payload.update({"verdict": "GREEN", "reason": "open_close_recovered"})
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--hold-s", type=float, default=0.2)
    parser.add_argument("--gap-s", type=float, default=0.2)
    parser.add_argument("--no-read-after-request", action="store_true")
    parser.add_argument("--heartbeat-timeout", type=float, default=10.0)
    parser.add_argument("--final-timeout", type=float, default=45.0)
    parser.add_argument("--min-final-rate", type=float, default=200.0)
    parser.add_argument("--source-system", type=int, default=150)
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    try:
        payload = run(args)
    except Exception as exc:  # noqa: BLE001
        payload = {
            "timestamp_utc": iso_now(),
            "port": args.port,
            "verdict": "RED",
            "reason": repr(exc),
        }

    json_path = os.path.join(args.outdir, "cdc_open_close_stress.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
