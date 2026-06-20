#!/usr/bin/env python3
"""RTT CUAV v5 peripheral health gate over MAVLink.

The gate checks the user-visible health of core peripherals without changing
parameters.  It reports two layers:
- driver_verdict: hardware/data/logging paths that can be validated on a fixed
  bench setup;
- calibration_verdict: AHRS/pre-arm health bits that also depend on physical
  accel/compass calibration state.

- SYS_STATUS present/enabled/health bits for gyro, accel, compass, barometer,
  AHRS and logging;
- RAW_IMU acceleration and magnetic-field fields are non-zero/sane;
- SCALED_PRESSURE is in a plausible atmospheric range;
- ATTITUDE and EKF_STATUS_REPORT are present.
"""

from __future__ import annotations

import argparse
import glob
import json
import math
import os
import sys
import time
from datetime import datetime, timezone
from typing import Any
import copy

from pymavlink import mavutil

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port

DEFAULT_PORT = MAVLINK_PORT

MSG_INTERVALS = {
    mavutil.mavlink.MAVLINK_MSG_ID_SYS_STATUS: 250000,
    mavutil.mavlink.MAVLINK_MSG_ID_RAW_IMU: 100000,
    mavutil.mavlink.MAVLINK_MSG_ID_ATTITUDE: 100000,
    mavutil.mavlink.MAVLINK_MSG_ID_SCALED_PRESSURE: 100000,
    mavutil.mavlink.MAVLINK_MSG_ID_EKF_STATUS_REPORT: 250000,
}

REQUIRED_SENSOR_BITS = {
    "gyro": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_GYRO,
    "accel": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_ACCEL,
    "mag": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_MAG,
    "baro": mavutil.mavlink.MAV_SYS_STATUS_SENSOR_ABSOLUTE_PRESSURE,
    "ahrs": mavutil.mavlink.MAV_SYS_STATUS_AHRS,
    "logging": mavutil.mavlink.MAV_SYS_STATUS_LOGGING,
}

DRIVER_SENSOR_BITS = ("gyro", "accel", "mag", "baro", "logging")
CALIBRATION_SENSOR_BITS = ("ahrs",)


def install_pymavlink_instance_cache_guard() -> None:
    """Keep pymavlink instance-message caching from tripping on RAW_IMU.

    The RTT board streams instance-bearing messages such as RAW_IMU.  Some
    pymavlink builds can leave the cached message object with ``_instances``
    unset, which later causes ``add_message`` to crash while handling the
    stream.  This local guard initializes the missing cache dictionary only on
    that narrow path.
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
        except Exception as exc:  # noqa: BLE001 - gate reports the last resolver failure
            last_error = str(exc)

        if time.monotonic() >= deadline:
            raise RuntimeError(f"port_wait_timeout:{last_error}")
        time.sleep(retry_s)


def wait_accepted_status(
    conn: Any,
    timeout_s: float,
    accepted_statuses: set[int],
) -> tuple[bool, float, int | None]:
    start = time.monotonic()
    last_status = None
    while time.monotonic() - start < timeout_s:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if msg is None:
            continue
        last_status = int(msg.system_status)
        if last_status in accepted_statuses:
            return True, time.monotonic() - start, last_status
    return False, time.monotonic() - start, last_status


def connect_and_wait_status(args: argparse.Namespace) -> tuple[Any | None, str, float, bool, float, int | None, str | None]:
    start = time.monotonic()
    deadline = start + args.port_wait_timeout + args.settle_timeout
    last_error: str | None = None
    accumulated_port_wait = 0.0

    while time.monotonic() < deadline:
        try:
            remaining_port_wait = max(0.1, min(args.port_wait_timeout, deadline - time.monotonic()))
            port, port_wait_s = wait_for_port(args.port, remaining_port_wait, args.port_wait_retry)
            accumulated_port_wait += port_wait_s
            conn = mavutil.mavlink_connection(
                port,
                baud=115200,
                robust_parsing=True,
                source_system=args.source_system,
            )
        except Exception as exc:  # noqa: BLE001 - report and retry connection setup
            last_error = str(exc)
            time.sleep(args.reconnect_retry)
            continue

        try:
            remaining_settle = max(0.1, deadline - time.monotonic())
            status_ok, settle_s, last_status = wait_accepted_status(
                conn,
                remaining_settle,
                set(args.accept_status),
            )
            return conn, port, accumulated_port_wait, status_ok, time.monotonic() - start, last_status, None
        except Exception as exc:  # noqa: BLE001 - CDC may be mid re-enumeration after reset
            last_error = str(exc)
            try:
                conn.close()
            except Exception:
                pass
            time.sleep(args.reconnect_retry)

    return None, args.port, accumulated_port_wait, False, time.monotonic() - start, None, last_error


def drain(conn: Any, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def request_intervals(conn: Any) -> None:
    for msg_id, interval_us in MSG_INTERVALS.items():
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


def bit_state(sys_status: Any | None) -> dict[str, dict[str, bool]]:
    if sys_status is None:
        return {}
    present = int(sys_status.onboard_control_sensors_present)
    enabled = int(sys_status.onboard_control_sensors_enabled)
    health = int(sys_status.onboard_control_sensors_health)
    return {
        name: {
            "present": bool(present & bit),
            "enabled": bool(enabled & bit),
            "healthy": bool(health & bit),
        }
        for name, bit in REQUIRED_SENSOR_BITS.items()
    }


def raw_imu_summary(raw_imu: Any | None) -> dict[str, Any]:
    if raw_imu is None:
        return {}
    accel = [int(raw_imu.xacc), int(raw_imu.yacc), int(raw_imu.zacc)]
    gyro = [int(raw_imu.xgyro), int(raw_imu.ygyro), int(raw_imu.zgyro)]
    mag = [int(raw_imu.xmag), int(raw_imu.ymag), int(raw_imu.zmag)]
    return {
        "accel_mg": accel,
        "accel_norm_mg": round(math.sqrt(sum(axis * axis for axis in accel)), 1),
        "gyro": gyro,
        "mag_mgauss": mag,
        "mag_norm_mgauss": round(math.sqrt(sum(axis * axis for axis in mag)), 1),
    }


def sensor_observation_summary(observations: dict[str, dict[str, int]]) -> dict[str, dict[str, Any]]:
    summary: dict[str, dict[str, Any]] = {}
    for name, counters in observations.items():
        total = counters.get("total", 0)
        summary[name] = {
            "total": total,
            "present": counters.get("present", 0),
            "enabled": counters.get("enabled", 0),
            "healthy": counters.get("healthy", 0),
            "healthy_ratio": None if total == 0 else round(counters.get("healthy", 0) / total, 3),
        }
    return summary


def observe_sensor_bits(observations: dict[str, dict[str, int]], sys_status: Any) -> None:
    present = int(sys_status.onboard_control_sensors_present)
    enabled = int(sys_status.onboard_control_sensors_enabled)
    health = int(sys_status.onboard_control_sensors_health)
    for name, bit in REQUIRED_SENSOR_BITS.items():
        counters = observations.setdefault(
            name,
            {"total": 0, "present": 0, "enabled": 0, "healthy": 0},
        )
        counters["total"] += 1
        counters["present"] += 1 if present & bit else 0
        counters["enabled"] += 1 if enabled & bit else 0
        counters["healthy"] += 1 if health & bit else 0


def evaluate(payload: dict[str, Any], strict_prearm_health: bool) -> tuple[str, str, dict[str, str]]:
    driver_failures: dict[str, str] = {}
    calibration_failures: dict[str, str] = {}
    counts = payload["counts"]
    for msg_type in ("SYS_STATUS", "RAW_IMU", "ATTITUDE", "SCALED_PRESSURE", "EKF_STATUS_REPORT"):
        if counts.get(msg_type, 0) < 2:
            driver_failures[msg_type] = f"too_few:{counts.get(msg_type, 0)}"

    for name, state in payload.get("sensor_bits", {}).items():
        target = calibration_failures if name in CALIBRATION_SENSOR_BITS else driver_failures
        if name in DRIVER_SENSOR_BITS or name in CALIBRATION_SENSOR_BITS:
            if not state.get("present", False):
                target[f"{name}.present"] = "false"
            if not state.get("enabled", False):
                target[f"{name}.enabled"] = "false"
            if not state.get("healthy", False):
                target[f"{name}.healthy"] = "false"

    raw = payload.get("raw_imu", {})
    accel_norm = float(raw.get("accel_norm_mg", 0.0))
    if not 800.0 <= accel_norm <= 1200.0:
        driver_failures["raw_imu.accel_norm_mg"] = str(accel_norm)
    mag_norm = float(raw.get("mag_norm_mgauss", 0.0))
    if mag_norm <= 1.0:
        driver_failures["raw_imu.mag_norm_mgauss"] = str(mag_norm)

    pressure = payload.get("scaled_pressure", {})
    press_abs = pressure.get("press_abs_hpa")
    if press_abs is None or not 900.0 <= float(press_abs) <= 1100.0:
        driver_failures["scaled_pressure.press_abs_hpa"] = str(press_abs)

    ekf = payload.get("ekf_status", {})
    flags = int(ekf.get("flags", 0))
    if (flags & 0x07) != 0x07:
        driver_failures["ekf.flags"] = f"0x{flags:x}"

    driver_verdict = "RED" if driver_failures else "GREEN"
    calibration_verdict = "RED" if calibration_failures else "GREEN"
    verdict = "RED" if driver_failures or (strict_prearm_health and calibration_failures) else "GREEN"
    if driver_failures:
        reason = "peripheral_driver_bad"
    elif strict_prearm_health and calibration_failures:
        reason = "peripheral_prearm_health_bad"
    elif calibration_failures:
        reason = "peripheral_driver_ok_calibration_pending"
    else:
        reason = "peripheral_health_ok"

    return verdict, reason, {
        "driver_verdict": driver_verdict,
        "calibration_verdict": calibration_verdict,
        "driver_failures": driver_failures,
        "calibration_failures": calibration_failures,
    }


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    install_pymavlink_instance_cache_guard()
    conn, port, port_wait_s, status_ok, settle_s, last_status, connect_error = connect_and_wait_status(args)
    if conn is None:
        return {
            "timestamp_utc": iso_now(),
            "port": port,
            "port_wait_s": round(port_wait_s, 3),
            "verdict": "RED",
            "reason": "no_connection" if connect_error is None else f"no_connection:{connect_error}",
            "settle_s": round(settle_s, 3),
            "last_system_status": last_status,
        }
    try:
        if not status_ok:
            return {
                "timestamp_utc": iso_now(),
                "port": port,
                "port_wait_s": round(port_wait_s, 3),
                "verdict": "RED",
                "reason": "no_accepted_status",
                "settle_s": round(settle_s, 3),
                "last_system_status": last_status,
                "accepted_statuses": sorted(set(args.accept_status)),
            }

        drain(conn, 0.4)
        request_intervals(conn)
        drain(conn, 0.6)

        counts = {
            "HEARTBEAT": 0,
            "SYS_STATUS": 0,
            "RAW_IMU": 0,
            "ATTITUDE": 0,
            "SCALED_PRESSURE": 0,
            "EKF_STATUS_REPORT": 0,
        }
        last: dict[str, Any] = {}
        sensor_observations: dict[str, dict[str, int]] = {}
        deadline = time.monotonic() + args.sample
        while time.monotonic() < deadline:
            msg = conn.recv_match(blocking=True, timeout=1.0)
            if msg is None:
                continue
            msg_type = msg.get_type()
            if msg_type not in counts:
                continue
            counts[msg_type] += 1
            last[msg_type] = msg
            if msg_type == "SYS_STATUS":
                observe_sensor_bits(sensor_observations, msg)

        sys_status = last.get("SYS_STATUS")
        scaled_pressure = last.get("SCALED_PRESSURE")
        attitude = last.get("ATTITUDE")
        ekf = last.get("EKF_STATUS_REPORT")
        payload: dict[str, Any] = {
            "timestamp_utc": iso_now(),
            "port": port,
            "port_wait_s": round(port_wait_s, 3),
            "target_system": int(conn.target_system),
            "target_component": int(conn.target_component),
            "settle_s": round(settle_s, 3),
            "sample_s": args.sample,
            "last_system_status": last_status,
            "standby_ok": status_ok,
            "status_ok": status_ok,
            "accepted_statuses": sorted(set(args.accept_status)),
            "counts": counts,
            "sensor_bits": bit_state(sys_status),
            "sensor_observations": sensor_observation_summary(sensor_observations),
            "raw_imu": raw_imu_summary(last.get("RAW_IMU")),
            "scaled_pressure": {
                "press_abs_hpa": None if scaled_pressure is None else round(float(scaled_pressure.press_abs), 2),
                "temperature_cdeg": None if scaled_pressure is None else int(scaled_pressure.temperature),
            },
            "attitude": {
                "roll": None if attitude is None else round(float(attitude.roll), 5),
                "pitch": None if attitude is None else round(float(attitude.pitch), 5),
                "yaw": None if attitude is None else round(float(attitude.yaw), 5),
            },
            "ekf_status": {
                "flags": 0 if ekf is None else int(ekf.flags),
                "flags_hex": "0x0" if ekf is None else f"0x{int(ekf.flags):x}",
            },
        }
        verdict, reason, evaluation = evaluate(payload, args.strict_prearm_health)
        payload.update({
            "verdict": verdict,
            "reason": reason,
            "driver_verdict": evaluation["driver_verdict"],
            "calibration_verdict": evaluation["calibration_verdict"],
            "failures": evaluation["driver_failures"],
            "calibration_failures": evaluation["calibration_failures"],
            "strict_prearm_health": args.strict_prearm_health,
        })
        return payload
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="RTT peripheral health gate")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--port-wait-timeout", type=float, default=30.0)
    parser.add_argument("--port-wait-retry", type=float, default=0.1)
    parser.add_argument("--settle-timeout", type=float, default=30.0)
    parser.add_argument("--reconnect-retry", type=float, default=0.25)
    parser.add_argument("--sample", type=float, default=15.0)
    parser.add_argument("--source-system", type=int, default=244)
    parser.add_argument("--accept-status", type=int, action="append", default=[3, 4, 5])
    parser.add_argument("--strict-prearm-health", action="store_true",
                        help="fail the gate when AHRS/pre-arm calibration health is not green")
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

    json_path = os.path.join(args.outdir, "peripheral_health.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
