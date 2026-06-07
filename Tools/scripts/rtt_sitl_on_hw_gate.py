#!/usr/bin/env python3
"""RTT SITL-on-hardware MAVLink gate.

This validates the behaviour introduced by the RTT SCons SITL-on-hardware
path: ROMFS default parameters must be visible through normal MAVLink PARAM
reads, and the board must publish the expected simulated sensor/vehicle
messages while running on real hardware.
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

EXPECTED_PARAMS: dict[str, float] = {
    "AHRS_EKF_TYPE": 10.0,
    "GPS1_TYPE": 100.0,
    "SIM_RATE_HZ": 400.0,
    "SCHED_LOOP_RATE": 400.0,
}

MSG_IDS = {
    "SYS_STATUS": 1,
    "GPS_RAW_INT": 24,
    "RAW_IMU": 27,
    "SCALED_PRESSURE": 29,
    "ATTITUDE": 30,
    "GLOBAL_POSITION_INT": 33,
    "SIMSTATE": 164,
}


class GateError(RuntimeError):
    def __init__(self, reason: str, payload: dict[str, Any] | None = None):
        super().__init__(reason)
        self.reason = reason
        self.payload = payload or {}


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def realpath_or_self(path: str) -> str:
    try:
        return os.path.realpath(path)
    except OSError:
        return path


def resolve_port(port_arg: str) -> str:
    if port_arg != "auto":
        if not os.path.exists(port_arg):
            raise GateError("port_not_found", {"port": port_arg})
        return port_arg

    if os.path.exists(DEFAULT_PORT):
        return DEFAULT_PORT

    by_id_dir = "/dev/serial/by-id"
    excluded_realpaths: set[str] = set()
    if os.path.isdir(by_id_dir):
        for name in os.listdir(by_id_dir):
            if "1a86_USB_Single_Serial" in name or "STLink" in name or "ST-LINK" in name:
                excluded_realpaths.add(realpath_or_self(os.path.join(by_id_dir, name)))

    patterns = (
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC*",
        "/dev/serial/by-id/*CUAV*CDC*",
        "/dev/serial/by-id/*ArduPilot*",
    )
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]

    acms = [
        dev for dev in sorted(glob.glob("/dev/ttyACM*"))
        if realpath_or_self(dev) not in excluded_realpaths
    ]
    if acms:
        return acms[0]

    raise GateError("no_cdc_port")


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def drain(conn: Any, seconds: float = 0.25) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def connect(port: str, heartbeat_timeout: float) -> tuple[Any, Any]:
    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=247,
    )
    heartbeat = conn.wait_heartbeat(timeout=heartbeat_timeout)
    if heartbeat is None or conn.target_system == 0:
        conn.close()
        raise GateError("no_heartbeat", {"port": port})
    return conn, heartbeat


def read_param_direct(conn: Any, name: str, timeout_s: float = 8.0) -> float:
    drain(conn)
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        -1,
    )
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1.0)
        if msg is None:
            continue
        if param_name(msg) == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_direct_timeout:{name}")


def read_param_from_list(conn: Any, name: str, timeout_s: float = 50.0) -> float:
    drain(conn, 0.5)
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    deadline = time.monotonic() + timeout_s
    seen = 0
    count: int | None = None
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1.0)
        if msg is None:
            continue
        seen += 1
        count = int(msg.param_count)
        if param_name(msg) == name:
            return float(msg.param_value)
        if count > 0 and seen >= count:
            break
    raise RuntimeError(f"param_list_timeout:{name}:seen={seen}:count={count}")


def read_param(conn: Any, name: str) -> float:
    try:
        return read_param_direct(conn, name)
    except Exception:
        return read_param_from_list(conn, name)


def verify_params(conn: Any, tolerance: float) -> dict[str, float]:
    values: dict[str, float] = {}
    mismatches: dict[str, dict[str, float]] = {}
    for name, expected in EXPECTED_PARAMS.items():
        value = read_param(conn, name)
        values[name] = value
        if abs(value - expected) > tolerance:
            mismatches[name] = {"expected": expected, "actual": value}
    if mismatches:
        raise GateError("param_mismatch", {"params": values, "mismatches": mismatches})
    return values


def request_streams(conn: Any, rate_hz: int) -> None:
    for stream_id in (
        mavutil.mavlink.MAV_DATA_STREAM_RAW_SENSORS,
        mavutil.mavlink.MAV_DATA_STREAM_EXTRA1,
        mavutil.mavlink.MAV_DATA_STREAM_POSITION,
        mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS,
    ):
        conn.mav.request_data_stream_send(
            conn.target_system,
            conn.target_component,
            stream_id,
            rate_hz,
            1,
        )

    interval_us = int(1_000_000 / rate_hz)
    for msg_id in MSG_IDS.values():
        conn.mav.command_long_send(
            conn.target_system,
            conn.target_component,
            mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,
            0,
            msg_id,
            interval_us,
            0,
            0,
            0,
            0,
            0,
        )


def sample_messages(conn: Any, sample_s: float, rate_hz: int) -> dict[str, Any]:
    request_streams(conn, rate_hz)
    counts = {
        "HEARTBEAT": 0,
        "RAW_IMU": 0,
        "ATTITUDE": 0,
        "GPS_RAW_INT": 0,
        "GLOBAL_POSITION_INT": 0,
        "SIMSTATE": 0,
        "SYS_STATUS": 0,
        "SCALED_PRESSURE": 0,
    }
    last_values: dict[str, dict[str, Any]] = {}
    max_silence = 0.0
    last_msg_time = time.monotonic()
    deadline = last_msg_time + sample_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(blocking=True, timeout=1.0)
        now = time.monotonic()
        if msg is None:
            max_silence = max(max_silence, now - last_msg_time)
            continue
        last_msg_time = now
        msg_type = msg.get_type()
        if msg_type in counts:
            counts[msg_type] += 1
            last_values[msg_type] = msg.to_dict()

    failures: dict[str, str] = {}
    if counts["HEARTBEAT"] < max(3, int(sample_s * 0.5)):
        failures["HEARTBEAT"] = "too_few"
    if counts["RAW_IMU"] < max(10, int(sample_s * 2)):
        failures["RAW_IMU"] = "too_few"
    if counts["ATTITUDE"] < max(10, int(sample_s * 2)):
        failures["ATTITUDE"] = "too_few"
    if counts["GPS_RAW_INT"] < max(3, int(sample_s * 0.5)):
        failures["GPS_RAW_INT"] = "too_few"
    if max_silence > 5.0:
        failures["max_silence"] = f"{max_silence:.3f}s"

    gps = last_values.get("GPS_RAW_INT")
    if gps is not None and int(gps.get("fix_type", 0)) < 3:
        failures["GPS_RAW_INT.fix_type"] = str(gps.get("fix_type"))

    if failures:
        raise GateError(
            "message_stream_red",
            {
                "counts": counts,
                "failures": failures,
                "last_values": last_values,
                "max_silence": round(max_silence, 3),
            },
        )

    return {
        "counts": counts,
        "last_values": last_values,
        "max_silence": round(max_silence, 3),
    }


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    port = resolve_port(args.port)
    conn, heartbeat = connect(port, args.heartbeat_timeout)
    try:
        params = verify_params(conn, args.param_tolerance)
        sample = sample_messages(conn, args.sample, args.rate)
    finally:
        conn.close()

    return {
        "timestamp_utc": iso_now(),
        "verdict": "GREEN",
        "reason": "ok",
        "port": port,
        "heartbeat": heartbeat.to_dict(),
        "params": params,
        "sample_seconds": args.sample,
        **sample,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--heartbeat-timeout", type=float, default=25.0)
    parser.add_argument("--sample", type=float, default=30.0)
    parser.add_argument("--rate", type=int, default=5)
    parser.add_argument("--param-tolerance", type=float, default=0.01)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        result = run_gate(args)
    except GateError as exc:
        result = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": exc.reason,
            **exc.payload,
        }
        print(json.dumps(result, indent=2, sort_keys=True), file=sys.stderr)
        return 1
    except Exception as exc:
        result = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": type(exc).__name__,
            "detail": str(exc),
        }
        print(json.dumps(result, indent=2, sort_keys=True), file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "GREEN "
            f"params={result['params']} "
            f"counts={result['counts']} "
            f"max_silence={result['max_silence']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
