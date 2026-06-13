#!/usr/bin/env python3
"""Collect AHRS/EKF/prearm evidence over MAVLink for RTT bring-up."""

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


def wait_for_port(port_arg: str, timeout_s: float, retry_s: float) -> tuple[str, float]:
    start = time.monotonic()
    deadline = start + timeout_s
    last_error = "no_cdc_port"
    while True:
        try:
            port = resolve_port(port_arg)
            if os.path.exists(port):
                return port, time.monotonic() - start
            last_error = f"resolved_port_absent:{port}"
        except Exception as exc:  # noqa: BLE001 - report the last resolver failure verbatim
            last_error = str(exc)

        if time.monotonic() >= deadline:
            raise RuntimeError(f"port_wait_timeout:{last_error}")
        time.sleep(retry_s)


def wait_heartbeat(conn: Any, timeout_s: float) -> Any | None:
    return conn.wait_heartbeat(timeout=timeout_s)


def drain(conn: Any, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def request_message(conn: Any, msg_id: int) -> None:
    conn.mav.command_long_send(
        conn.target_system,
        conn.target_component,
        mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE,
        0,
        msg_id,
        0,
        0,
        0,
        0,
        0,
        0,
    )


def set_interval(conn: Any, msg_id: int, interval_us: int) -> None:
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


def read_param(conn: Any, name: str, timeout_s: float = 5.0) -> float | None:
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
        pid = msg.param_id
        pname = pid.decode(errors="ignore").rstrip("\x00") if isinstance(pid, bytes) else str(pid).rstrip("\x00")
        if pname == name:
            return float(msg.param_value)
    return None


def preflight_arm_check(conn: Any) -> None:
    conn.mav.command_long_send(
        conn.target_system,
        conn.target_component,
        mavutil.mavlink.MAV_CMD_RUN_PREARM_CHECKS,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )


def run_diag(args: argparse.Namespace) -> dict[str, Any]:
    port, port_wait_s = wait_for_port(args.port, args.port_wait_timeout, args.port_wait_retry)
    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=args.source_system,
    )
    try:
        hb = wait_heartbeat(conn, args.heartbeat_timeout)
        if hb is None:
            return {"timestamp_utc": iso_now(), "port": port, "verdict": "RED", "reason": "no_heartbeat"}
        drain(conn, 0.4)

        for msg_id in (
            mavutil.mavlink.MAVLINK_MSG_ID_SYS_STATUS,
            mavutil.mavlink.MAVLINK_MSG_ID_EKF_STATUS_REPORT,
            mavutil.mavlink.MAVLINK_MSG_ID_ATTITUDE,
            mavutil.mavlink.MAVLINK_MSG_ID_VIBRATION,
        ):
            set_interval(conn, msg_id, 250000)

        params = {
            name: read_param(conn, name)
            for name in ("AHRS_EKF_TYPE", "EK3_ENABLE", "INS_ACC2OFFS_X", "INS_ACC3OFFS_X", "COMPASS_USE")
        }
        drain(conn, 0.3)
        preflight_arm_check(conn)
        request_message(conn, mavutil.mavlink.MAVLINK_MSG_ID_SYS_STATUS)
        request_message(conn, mavutil.mavlink.MAVLINK_MSG_ID_EKF_STATUS_REPORT)

        counts: dict[str, int] = {}
        last: dict[str, Any] = {}
        statustext: list[dict[str, Any]] = []
        command_ack: list[dict[str, Any]] = []
        deadline = time.monotonic() + args.sample
        while time.monotonic() < deadline:
            msg = conn.recv_match(blocking=True, timeout=1.0)
            if msg is None:
                continue
            msg_type = msg.get_type()
            counts[msg_type] = counts.get(msg_type, 0) + 1
            if msg_type in ("SYS_STATUS", "EKF_STATUS_REPORT", "ATTITUDE", "VIBRATION", "HEARTBEAT"):
                last[msg_type] = msg.to_dict()
            elif msg_type == "STATUSTEXT":
                text = msg.text if isinstance(msg.text, str) else msg.text.decode(errors="ignore")
                statustext.append({"severity": int(msg.severity), "text": text.rstrip("\x00")})
            elif msg_type == "COMMAND_ACK":
                command_ack.append(msg.to_dict())

        return {
            "timestamp_utc": iso_now(),
            "port": port,
            "port_wait_s": round(port_wait_s, 3),
            "target_system": int(conn.target_system),
            "target_component": int(conn.target_component),
            "sample_s": args.sample,
            "counts": counts,
            "last": last,
            "params": params,
            "statustext": statustext,
            "command_ack": command_ack,
            "verdict": "GREEN",
            "reason": "diag_collected",
        }
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect AHRS/EKF diagnostics over MAVLink")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--port-wait-timeout", type=float, default=30.0)
    parser.add_argument("--port-wait-retry", type=float, default=0.1)
    parser.add_argument("--heartbeat-timeout", type=float, default=25.0)
    parser.add_argument("--sample", type=float, default=25.0)
    parser.add_argument("--source-system", type=int, default=243)
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    try:
        payload = run_diag(args)
    except Exception as exc:
        payload = {
            "timestamp_utc": iso_now(),
            "port": args.port,
            "verdict": "RED",
            "reason": str(exc),
        }

    json_path = os.path.join(args.outdir, "ahrs_health_diag.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
