#!/usr/bin/env python3
"""Collect RTT compass/AHRS parameter and status evidence over MAVLink."""

from __future__ import annotations

import argparse
import glob
import json
import os
import traceback
import time
from datetime import datetime, timezone
from typing import Any
import copy

from pymavlink import mavutil

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port

DEFAULT_PORT = MAVLINK_PORT

PARAM_NAMES = (
    "AHRS_EKF_TYPE",
    "EK3_ENABLE",
    "COMPASS_ENABLE",
    "COMPASS_USE",
    "COMPASS_USE2",
    "COMPASS_USE3",
    "COMPASS_DEV_ID",
    "COMPASS_DEV_ID2",
    "COMPASS_DEV_ID3",
    "COMPASS_TYPEMASK",
    "COMPASS_AUTO_ROT",
    "COMPASS_ORIENT",
    "COMPASS_ORIENT2",
    "COMPASS_ORIENT3",
    "COMPASS_OFS_X",
    "COMPASS_OFS_Y",
    "COMPASS_OFS_Z",
    "COMPASS_DIA_X",
    "COMPASS_DIA_Y",
    "COMPASS_DIA_Z",
    "COMPASS_ODI_X",
    "COMPASS_ODI_Y",
    "COMPASS_ODI_Z",
    "INS_GYR_CAL",
    "INS_ACC_BODYFIX",
    "INS_ACC_ID",
    "INS_ACC2_ID",
    "INS_ACC3_ID",
    "INS_GYR_ID",
    "INS_GYR2_ID",
    "INS_GYR3_ID",
    "SCHED_LOOP_RATE",
    "LOG_BACKEND_TYPE",
    "LOG_DISARMED",
)

MSG_INTERVALS = {
    mavutil.mavlink.MAVLINK_MSG_ID_SYS_STATUS: 250000,
    mavutil.mavlink.MAVLINK_MSG_ID_RAW_IMU: 100000,
    mavutil.mavlink.MAVLINK_MSG_ID_EKF_STATUS_REPORT: 250000,
    mavutil.mavlink.MAVLINK_MSG_ID_VIBRATION: 250000,
}

SENSOR_BITS = {
    "gyro": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_GYRO,
    "accel": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_ACCEL,
    "mag": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_MAG,
    "baro": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_ABSOLUTE_PRESSURE,
    "ahrs": mavutil.mavlink.MAV_SYS_STATUS_AHRS,
    "logging": mavutil.mavlink.MAV_SYS_STATUS_LOGGING,
}


def install_pymavlink_instance_cache_guard() -> None:
    """Keep pymavlink instance-message caching from tripping on RAW_IMU.

    Some pymavlink versions can keep a message type in the per-system cache
    with ``_instances`` still set to ``None``.  When a later instance-bearing
    message such as RAW_IMU arrives, ``add_message`` then tries to assign into
    that ``None`` value and the diagnostic aborts before collecting compass
    evidence.  This local guard preserves pymavlink's normal behavior while
    initializing the missing dictionary on that narrow path.
    """
    if getattr(mavutil, "_rtt_instance_cache_guard", False):
        return

    original_add_message = mavutil.add_message

    def guarded_add_message(messages: dict[str, Any], mtype: str, msg: Any) -> None:
        if msg._instance_field is None or getattr(msg, msg._instance_field, None) is None:
            messages[mtype] = msg
            return
        instance_value = getattr(msg, msg._instance_field)
        if mtype not in messages:
            messages[mtype] = copy.copy(msg)
            messages[mtype]._instances = {}
            messages[mtype]._instances[instance_value] = msg
            messages["%s[%s]" % (mtype, str(instance_value))] = copy.copy(msg)
            return
        if getattr(messages[mtype], "_instances", None) is None:
            messages[mtype]._instances = {}
        messages[mtype]._instances[instance_value] = msg
        prev_instances = messages[mtype]._instances
        messages[mtype] = copy.copy(msg)
        messages[mtype]._instances = prev_instances
        messages["%s[%s]" % (mtype, str(instance_value))] = copy.copy(msg)

    guarded_add_message._original_add_message = original_add_message  # type: ignore[attr-defined]
    mavutil.add_message = guarded_add_message
    mavutil._rtt_instance_cache_guard = True


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(port_arg: str) -> str:
    return resolve_mavlink_port(port_arg)


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def read_param(conn: Any, name: str, timeout_s: float) -> float | None:
    conn.mav.param_request_read_send(conn.target_system, conn.target_component, name.encode("ascii"), -1)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        if param_name(msg) == name:
            return float(msg.param_value)
    return None


def download_selected_params(conn: Any, wanted: tuple[str, ...], timeout_s: float, max_gap_s: float) -> dict[str, float | None]:
    values: dict[str, float | None] = {name: None for name in wanted}
    wanted_set = set(wanted)
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    deadline = time.monotonic() + timeout_s
    last_rx = time.monotonic()
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        now = time.monotonic()
        if msg is None:
            if now - last_rx > max_gap_s:
                break
            continue
        last_rx = now
        name = param_name(msg)
        if name in wanted_set:
            values[name] = float(msg.param_value)
        if all(value is not None for value in values.values()):
            break
    return values


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


def sensor_bits(sys_status: Any | None) -> dict[str, dict[str, bool]]:
    if sys_status is None:
        return {}
    if isinstance(sys_status, dict):
        present = int(sys_status.get("onboard_control_sensors_present", 0))
        enabled = int(sys_status.get("onboard_control_sensors_enabled", 0))
        health = int(sys_status.get("onboard_control_sensors_health", 0))
    else:
        present = int(sys_status.onboard_control_sensors_present)
        enabled = int(sys_status.onboard_control_sensors_enabled)
        health = int(sys_status.onboard_control_sensors_health)
    return {
        name: {
            "present": bool(present & bit),
            "enabled": bool(enabled & bit),
            "healthy": bool(health & bit),
        }
        for name, bit in SENSOR_BITS.items()
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    install_pymavlink_instance_cache_guard()
    port = resolve_port(args.port)
    conn = mavutil.mavlink_connection(port, baud=115200, robust_parsing=True, source_system=args.source_system)
    try:
        hb = conn.wait_heartbeat(timeout=args.heartbeat_timeout)
        if hb is None:
            return {"timestamp_utc": iso_now(), "port": port, "verdict": "RED", "reason": "no_heartbeat"}

        for msg_id, interval_us in MSG_INTERVALS.items():
            set_interval(conn, msg_id, interval_us)

        params = download_selected_params(conn, PARAM_NAMES, args.param_timeout, args.max_param_gap)

        last: dict[str, Any] = {}
        counts: dict[str, int] = {}
        statustext: list[dict[str, Any]] = []
        deadline = time.monotonic() + args.sample
        while time.monotonic() < deadline:
            msg = conn.recv_match(blocking=True, timeout=0.5)
            if msg is None:
                continue
            msg_type = msg.get_type()
            counts[msg_type] = counts.get(msg_type, 0) + 1
            if msg_type in ("SYS_STATUS", "RAW_IMU", "EKF_STATUS_REPORT", "VIBRATION", "HEARTBEAT"):
                last[msg_type] = msg.to_dict()
            elif msg_type == "STATUSTEXT":
                text = msg.text if isinstance(msg.text, str) else msg.text.decode(errors="ignore")
                statustext.append({"severity": int(msg.severity), "text": text.rstrip("\x00")})

        return {
            "timestamp_utc": iso_now(),
            "port": port,
            "target_system": int(conn.target_system),
            "target_component": int(conn.target_component),
            "params": params,
            "counts": counts,
            "last": last,
            "sensor_bits": sensor_bits(last.get("SYS_STATUS")),
            "statustext": statustext,
            "verdict": "GREEN",
            "reason": "diag_collected",
        }
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--heartbeat-timeout", type=float, default=20.0)
    parser.add_argument("--param-timeout", type=float, default=20.0)
    parser.add_argument("--max-param-gap", type=float, default=1.5)
    parser.add_argument("--sample", type=float, default=12.0)
    parser.add_argument("--source-system", type=int, default=244)
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
            "traceback": traceback.format_exc(),
        }

    path = os.path.join(args.outdir, "compass_ahrs_param_diag.json")
    payload["json_path"] = path
    with open(path, "w", encoding="utf-8") as fp:
        json.dump(payload, fp, indent=2, sort_keys=True)
        fp.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
