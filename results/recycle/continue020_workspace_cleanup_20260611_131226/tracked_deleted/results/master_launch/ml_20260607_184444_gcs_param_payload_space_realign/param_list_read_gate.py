#!/usr/bin/env python3
from __future__ import annotations

import glob
import json
import os
import sys
import time
from datetime import datetime, timezone
from typing import Any

from pymavlink import mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"
READ_PARAMS = ("LOG_DISARMED", "MAV_SYSID", "AHRS_EKF_TYPE")


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port() -> str:
    if os.path.exists(DEFAULT_PORT):
        return DEFAULT_PORT
    for pattern in (
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC*",
        "/dev/serial/by-id/*CUAV*CDC*",
        "/dev/serial/by-id/*ArduPilot*",
        "/dev/ttyACM*",
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    raise RuntimeError("no_cdc_port")


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def drain(conn: Any, seconds: float = 0.2) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def wait_ready(conn: Any, timeout_s: float = 45.0) -> dict[str, Any]:
    start = time.monotonic()
    hist: dict[str, int] = {}
    last = None
    while time.monotonic() - start < timeout_s:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1)
        if msg is None:
            continue
        last = msg.to_dict()
        status = int(getattr(msg, "system_status", -1))
        hist[str(status)] = hist.get(str(status), 0) + 1
        if status in (3, 4):
            return {"ready_s": round(time.monotonic() - start, 3), "status_histogram": hist, "last": last}
    raise RuntimeError(json.dumps({"reason": "not_ready", "hist": hist, "last": last}))


def request_param(conn: Any, name: str, timeout_s: float = 8.0) -> dict[str, Any]:
    drain(conn)
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        -1,
    )
    deadline = time.monotonic() + timeout_s
    seen: list[str] = []
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        got = param_name(msg)
        if len(seen) < 20:
            seen.append(got)
        if got == name:
            return {
                "name": got,
                "value": float(msg.param_value),
                "type": int(msg.param_type),
                "index": int(msg.param_index),
                "count": int(msg.param_count),
                "seen_before_match": seen,
            }
    raise RuntimeError(f"param_read_timeout:{name}:seen={seen}")


def request_list(conn: Any, timeout_s: float = 120.0, quiet_timeout_s: float = 8.0) -> dict[str, Any]:
    drain(conn, 0.5)
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)

    start = time.monotonic()
    deadline = start + timeout_s
    last_rx = start
    total = 0
    expected_count = None
    by_index: dict[int, str] = {}
    duplicate_indexes = 0
    names_seen: set[str] = set()
    first_values: list[dict[str, Any]] = []
    last_values: list[dict[str, Any]] = []

    while time.monotonic() < deadline:
        if time.monotonic() - last_rx > quiet_timeout_s:
            break
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        last_rx = time.monotonic()
        total += 1
        name = param_name(msg)
        index = int(msg.param_index)
        count = int(msg.param_count)
        expected_count = count if expected_count is None else max(expected_count, count)
        if index in by_index:
            duplicate_indexes += 1
        by_index[index] = name
        names_seen.add(name)

        record = {
            "name": name,
            "index": index,
            "count": count,
            "type": int(msg.param_type),
            "value": float(msg.param_value),
        }
        if len(first_values) < 12:
            first_values.append(record)
        last_values.append(record)
        if len(last_values) > 12:
            last_values.pop(0)

        if expected_count is not None and len(by_index) >= expected_count:
            break

    missing_count = None
    missing_sample: list[int] = []
    coverage = 0.0
    if expected_count:
        missing = [idx for idx in range(expected_count) if idx not in by_index]
        missing_count = len(missing)
        missing_sample = missing[:30]
        coverage = len(by_index) / expected_count

    return {
        "duration_s": round(time.monotonic() - start, 3),
        "total_param_value_messages": total,
        "expected_count": expected_count,
        "unique_indexes": len(by_index),
        "unique_names": len(names_seen),
        "duplicate_indexes": duplicate_indexes,
        "coverage": round(coverage, 5),
        "missing_count": missing_count,
        "missing_sample": missing_sample,
        "first_values": first_values,
        "last_values": last_values,
    }


def main() -> int:
    port = resolve_port()
    conn = mavutil.mavlink_connection(port, baud=115200, robust_parsing=True, source_system=246)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "read_params": READ_PARAMS,
    }
    try:
        hb = conn.wait_heartbeat(timeout=25)
        if hb is None:
            raise RuntimeError("no_heartbeat")
        payload["heartbeat"] = hb.to_dict()
        payload["ready"] = wait_ready(conn)
        payload["reads"] = {name: request_param(conn, name) for name in READ_PARAMS}
        payload["list"] = request_list(conn)

        failures: dict[str, Any] = {}
        list_result = payload["list"]
        expected = list_result.get("expected_count") or 0
        unique_indexes = list_result.get("unique_indexes") or 0
        coverage = list_result.get("coverage") or 0.0
        if expected < 100:
            failures["expected_count"] = expected
        if unique_indexes < 100:
            failures["unique_indexes"] = unique_indexes
        if coverage < 0.90:
            failures["coverage"] = coverage
        if list_result.get("total_param_value_messages", 0) < unique_indexes:
            failures["message_count"] = list_result.get("total_param_value_messages", 0)

        payload["failures"] = failures
        payload["verdict"] = "GREEN" if not failures else "RED"
        payload["reason"] = "param_read_and_list_ok" if not failures else "param_read_or_list_failed"
    except Exception as exc:
        payload["verdict"] = "RED"
        payload["reason"] = str(exc)
    finally:
        conn.close()

    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
