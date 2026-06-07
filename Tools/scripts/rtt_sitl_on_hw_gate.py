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
import math
import os
import subprocess
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

MSG_ID_TO_NAME = {msg_id: name for name, msg_id in MSG_IDS.items()}

EXCLUDED_PORT_NAME_FRAGMENTS = (
    "1a86_USB_Single_Serial",
    "STLink",
    "ST-LINK",
    "CUAVv5-BL",
    "-BL_",
)


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


def tty_acm_snapshot() -> list[str]:
    return sorted(glob.glob("/dev/ttyACM*"))


def by_id_snapshot() -> list[str]:
    return sorted(glob.glob("/dev/serial/by-id/*"))


def port_name_is_excluded(path: str) -> bool:
    name = os.path.basename(path).lower()
    return any(fragment.lower() in name for fragment in EXCLUDED_PORT_NAME_FRAGMENTS)


def excluded_port_realpaths() -> set[str]:
    by_id_dir = "/dev/serial/by-id"
    excluded: set[str] = set()
    if os.path.isdir(by_id_dir):
        for name in os.listdir(by_id_dir):
            if any(fragment.lower() in name.lower() for fragment in EXCLUDED_PORT_NAME_FRAGMENTS):
                excluded.add(realpath_or_self(os.path.join(by_id_dir, name)))
    return excluded


def auto_port_is_excluded(path: str, excluded_realpaths: set[str]) -> bool:
    return port_name_is_excluded(path) or realpath_or_self(path) in excluded_realpaths


def resolve_port(port_arg: str) -> str:
    if port_arg != "auto":
        if not os.path.exists(port_arg):
            raise GateError("port_not_found", {"port": port_arg})
        return port_arg

    excluded_realpaths = excluded_port_realpaths()

    if os.path.exists(DEFAULT_PORT) and not auto_port_is_excluded(DEFAULT_PORT, excluded_realpaths):
        return DEFAULT_PORT

    patterns = (
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC*",
        "/dev/serial/by-id/*CUAV*CDC*",
        "/dev/serial/by-id/*ArduPilot*",
    )
    for pattern in patterns:
        matches = [
            path for path in sorted(glob.glob(pattern))
            if not auto_port_is_excluded(path, excluded_realpaths)
        ]
        if matches:
            return matches[0]

    acms = [
        dev for dev in sorted(glob.glob("/dev/ttyACM*"))
        if realpath_or_self(dev) not in excluded_realpaths
    ]
    if acms:
        return acms[0]

    raise GateError("no_cdc_port")


def wait_cdc_resolve(port_arg: str, timeout_s: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_s
    last_error = ""
    while time.monotonic() < deadline:
        try:
            port = resolve_port(port_arg)
            if os.path.exists(port):
                return {
                    "port": port,
                    "realpath": realpath_or_self(port),
                    "tty_acm": tty_acm_snapshot(),
                    "by_id": by_id_snapshot(),
                }
        except Exception as exc:
            last_error = str(exc)
        time.sleep(1.0)
    raise GateError(
        "cdc_not_back",
        {
            "port_arg": port_arg,
            "timeout_s": timeout_s,
            "last_error": last_error,
            "tty_acm": tty_acm_snapshot(),
            "by_id": by_id_snapshot(),
        },
    )


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


def connect(port: str, heartbeat_timeout: float, source_system: int = 247) -> tuple[Any, Any]:
    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=source_system,
    )
    heartbeat = conn.wait_heartbeat(timeout=heartbeat_timeout)
    if heartbeat is None or conn.target_system == 0:
        conn.close()
        raise GateError("no_heartbeat", {"port": port})
    return conn, heartbeat


def wait_ready_heartbeat(conn: Any, timeout_s: float) -> dict[str, Any]:
    start = time.monotonic()
    statuses: dict[str, int] = {}
    first_heartbeat_s: float | None = None
    last_heartbeat = None
    while time.monotonic() - start < timeout_s:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if msg is None:
            continue
        if first_heartbeat_s is None:
            first_heartbeat_s = time.monotonic() - start
        last_heartbeat = msg
        status = int(getattr(msg, "system_status", -1))
        statuses[str(status)] = statuses.get(str(status), 0) + 1
        if status in (3, 4):
            return {
                "first_heartbeat_s": round(first_heartbeat_s, 3) if first_heartbeat_s is not None else None,
                "ready_heartbeat_s": round(time.monotonic() - start, 3),
                "status_histogram": statuses,
                "last": msg.to_dict(),
            }
    raise GateError(
        "heartbeat_not_ready",
        {
            "timeout_s": timeout_s,
            "status_histogram": statuses,
            "last_heartbeat": last_heartbeat.to_dict() if last_heartbeat is not None else None,
        },
    )


def connect_retry(port: str, heartbeat_timeout: float, source_system: int,
                  attempts: int, gap_s: float) -> tuple[Any, Any]:
    last_error = "unknown"
    for _ in range(attempts):
        try:
            return connect(port, heartbeat_timeout, source_system=source_system)
        except Exception as exc:
            last_error = str(exc)
            time.sleep(gap_s)
    raise GateError("connect_retry_failed", {"port": port, "last_error": last_error})


def source_system_for_round(round_index: int) -> int:
    # Keep the gate itself stable; this is a GCS system id, not a DUT variable.
    return 247


def wait_cdc(port: str, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(port):
            return
        time.sleep(1.0)
    raise GateError("cdc_not_back", {"port": port, "timeout_s": timeout_s})


def cleanup_openocd() -> None:
    for name in ("openocd", "openoccd"):
        subprocess.run(
            ["pkill", "-9", "-x", name],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )


def openocd_reset(log_path: str, config: str, timeout_s: int) -> int:
    with open(log_path, "w", encoding="utf-8") as log:
        proc = subprocess.run(
            [
                "timeout",
                str(timeout_s),
                "openocd",
                "-f",
                config,
                "-c",
                "init",
                "-c",
                "reset run",
                "-c",
                "shutdown",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    cleanup_openocd()
    return proc.returncode


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


def read_param(conn: Any, name: str, allow_param_list_fallback: bool) -> float:
    try:
        return read_param_direct(conn, name)
    except Exception:
        if not allow_param_list_fallback:
            raise
        return read_param_from_list(conn, name)


def verify_params(conn: Any, tolerance: float,
                  allow_param_list_fallback: bool) -> dict[str, float]:
    values: dict[str, float] = {}
    mismatches: dict[str, dict[str, float]] = {}
    for name, expected in EXPECTED_PARAMS.items():
        try:
            value = read_param(conn, name, allow_param_list_fallback)
        except Exception as exc:
            raise GateError(
                "param_read_failed",
                {
                    "param": name,
                    "params": values,
                    "detail": str(exc),
                    "allow_param_list_fallback": allow_param_list_fallback,
                },
            ) from exc
        values[name] = value
        if abs(value - expected) > tolerance:
            mismatches[name] = {"expected": expected, "actual": value}
    if mismatches:
        raise GateError("param_mismatch", {"params": values, "mismatches": mismatches})
    return values


def request_data_streams(conn: Any, rate_hz: int) -> None:
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


def wait_command_ack(conn: Any, command: int, timeout_s: float) -> dict[str, Any] | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="COMMAND_ACK", blocking=True, timeout=0.25)
        if msg is None:
            continue
        if int(getattr(msg, "command", -1)) == command:
            return msg.to_dict()
    return None


def set_message_interval(conn: Any, msg_id: int, interval_us: int,
                         timeout_s: float) -> dict[str, Any]:
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
    ack = wait_command_ack(conn, mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL, timeout_s)
    if ack is None:
        raise GateError(
            "message_interval_ack_missing",
            {"msg_id": msg_id, "msg_name": MSG_ID_TO_NAME.get(msg_id, str(msg_id))},
        )
    if int(ack.get("result", -1)) != 0:
        raise GateError(
            "message_interval_ack_rejected",
            {
                "msg_id": msg_id,
                "msg_name": MSG_ID_TO_NAME.get(msg_id, str(msg_id)),
                "ack": ack,
            },
        )
    return ack


def get_message_interval(conn: Any, msg_id: int,
                         timeout_s: float) -> tuple[dict[str, Any] | None, list[dict[str, Any]]]:
    conn.mav.command_long_send(
        conn.target_system,
        conn.target_component,
        mavutil.mavlink.MAV_CMD_GET_MESSAGE_INTERVAL,
        0,
        msg_id,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    observed: list[dict[str, Any]] = []
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type=["MESSAGE_INTERVAL", "COMMAND_ACK"], blocking=True, timeout=0.25)
        if msg is None:
            continue
        msg_dict = msg.to_dict()
        if len(observed) < 32:
            observed.append(msg_dict)
        if msg.get_type() == "MESSAGE_INTERVAL" and int(getattr(msg, "message_id", -1)) == msg_id:
            return msg_dict, observed
    return None, observed


def configure_message_intervals(args: argparse.Namespace, conn: Any) -> list[dict[str, Any]]:
    interval_us = int(1_000_000 / args.rate)
    configured = []
    request_data_streams(conn, args.rate)
    for name, msg_id in MSG_IDS.items():
        ack = set_message_interval(conn, msg_id, interval_us, args.interval_ack_timeout)
        interval, observed = get_message_interval(conn, msg_id, args.interval_get_timeout)
        if interval is None:
            raise GateError(
                "message_interval_get_missing",
                {"msg_id": msg_id, "msg_name": name, "ack": ack, "observed": observed},
            )
        if int(interval.get("interval_us", -1)) != interval_us:
            raise GateError(
                "message_interval_get_mismatch",
                {
                    "msg_id": msg_id,
                    "msg_name": name,
                    "expected_interval_us": interval_us,
                    "actual": interval,
                    "ack": ack,
                    "observed": observed,
                },
            )
        configured.append({
            "name": name,
            "msg_id": msg_id,
            "ack": ack,
            "interval": interval,
            "observed": observed,
        })
    return configured


def collect_command_feedback(conn: Any, seconds: float) -> dict[str, list[dict[str, Any]]]:
    feedback = {"COMMAND_ACK": [], "STATUSTEXT": [], "MESSAGE_INTERVAL": []}
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        msg = conn.recv_match(
            type=["COMMAND_ACK", "STATUSTEXT", "MESSAGE_INTERVAL"],
            blocking=True,
            timeout=0.25,
        )
        if msg is None:
            continue
        msg_type = msg.get_type()
        if msg_type in feedback:
            feedback[msg_type].append(msg.to_dict())
    return feedback


def warm_streams(conn: Any, warmup_s: float, rate_hz: int, retries: int) -> dict[str, Any]:
    feedback = {"COMMAND_ACK": [], "STATUSTEXT": [], "MESSAGE_INTERVAL": []}
    if warmup_s <= 0:
        request_data_streams(conn, rate_hz)
        round_feedback = collect_command_feedback(conn, 0.25)
        for key, values in round_feedback.items():
            feedback[key].extend(values)
        return feedback

    retries = max(1, retries)
    slice_s = warmup_s / retries
    for _ in range(retries):
        request_data_streams(conn, rate_hz)
        round_feedback = collect_command_feedback(conn, slice_s)
        for key, values in round_feedback.items():
            feedback[key].extend(values)
    return feedback


def min_count(sample_s: float, rate_hz: float, floor: int) -> int:
    return max(floor, int(math.ceil(sample_s * rate_hz)))


def message_rates(counts: dict[str, int], sample_s: float) -> dict[str, float]:
    if sample_s <= 0:
        return {name: 0.0 for name in counts}
    return {name: round(count / sample_s, 3) for name, count in counts.items()}


def sample_messages(args: argparse.Namespace, conn: Any) -> dict[str, Any]:
    sample_s = args.sample
    interval_config = configure_message_intervals(args, conn)
    warmup_started = time.monotonic()
    command_feedback = warm_streams(conn, args.stream_warmup, args.rate, args.stream_request_retries)
    sample_started = time.monotonic()
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
    required_counts = {
        "HEARTBEAT": min_count(sample_s, args.min_heartbeat_rate, 3),
        "RAW_IMU": min_count(sample_s, args.min_sensor_rate, 10),
        "ATTITUDE": min_count(sample_s, args.min_sensor_rate, 10),
        "GPS_RAW_INT": min_count(sample_s, args.min_position_rate, 3),
        "GLOBAL_POSITION_INT": min_count(sample_s, args.min_position_rate, 3),
        "SIMSTATE": min_count(sample_s, args.min_position_rate, 3),
    }
    for msg_type, required in required_counts.items():
        if counts[msg_type] < required:
            failures[msg_type] = f"too_few:{counts[msg_type]}<{required}"
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
                "rates_hz": message_rates(counts, sample_s),
                "required_counts": required_counts,
                "max_silence": round(max_silence, 3),
                "timing": {
                    "stream_warmup_s": round(sample_started - warmup_started, 3),
                    "sample_s": round(time.monotonic() - sample_started, 3),
                },
                "command_feedback": command_feedback,
                "interval_config": interval_config,
            },
        )

    return {
        "counts": counts,
        "rates_hz": message_rates(counts, sample_s),
        "required_counts": required_counts,
        "last_values": last_values,
        "max_silence": round(max_silence, 3),
        "timing": {
            "stream_warmup_s": round(sample_started - warmup_started, 3),
            "sample_s": round(time.monotonic() - sample_started, 3),
        },
        "command_feedback": command_feedback,
        "interval_config": interval_config,
    }


def run_one_round(args: argparse.Namespace, port: str, round_index: int,
                  reset: dict[str, Any] | None) -> dict[str, Any]:
    round_started = time.monotonic()
    source_system = source_system_for_round(round_index)
    conn, heartbeat = connect_retry(
        port,
        heartbeat_timeout=args.heartbeat_timeout,
        source_system=source_system,
        attempts=args.connect_attempts,
        gap_s=args.connect_gap,
    )
    connected_at = time.monotonic()
    round_context: dict[str, Any] = {
        "round": round_index,
        "port": port,
        "source_system": source_system,
        "allow_param_list_fallback": args.allow_param_list_fallback,
        "heartbeat": heartbeat.to_dict(),
        "sample_seconds": args.sample,
        "round_timing_partial": {
            "connect_s": round(connected_at - round_started, 3),
        },
    }
    if reset is not None:
        round_context["reset"] = reset

    try:
        ready = wait_ready_heartbeat(conn, args.standby_timeout)
        ready_at = time.monotonic()
        round_context["heartbeat_ready"] = ready
        round_context["round_timing_partial"]["ready_heartbeat_s"] = round(ready_at - connected_at, 3)

        params = verify_params(
            conn,
            args.param_tolerance,
            args.allow_param_list_fallback,
        )
        params_done_at = time.monotonic()
        round_context["params"] = params
        round_context["round_timing_partial"]["param_verify_s"] = round(params_done_at - ready_at, 3)
        round_context["round_timing_partial"]["ready_delay_after_params_s"] = args.ready_delay_after_params

        if args.ready_delay_after_params > 0:
            time.sleep(args.ready_delay_after_params)
        sample = sample_messages(args, conn)
    except GateError as exc:
        round_context["round_timing_partial"]["total_s"] = round(time.monotonic() - round_started, 3)
        payload = dict(round_context)
        payload.update(exc.payload)
        raise GateError(exc.reason, payload) from exc
    finally:
        conn.close()
    round_done_at = time.monotonic()

    result = {
        "timestamp_utc": iso_now(),
        "verdict": "GREEN",
        "reason": "ok",
        "round": round_index,
        "port": port,
        "source_system": source_system,
        "allow_param_list_fallback": args.allow_param_list_fallback,
        "heartbeat": heartbeat.to_dict(),
        "heartbeat_ready": ready,
        "params": params,
        "sample_seconds": args.sample,
        "round_timing": {
            "connect_s": round(connected_at - round_started, 3),
            "ready_heartbeat_s": round(ready_at - connected_at, 3),
            "param_verify_s": round(params_done_at - ready_at, 3),
            "ready_delay_after_params_s": args.ready_delay_after_params,
            "total_s": round(round_done_at - round_started, 3),
        },
        **sample,
    }
    if reset is not None:
        result["reset"] = reset
    return result


def aggregate_rounds(rounds: list[dict[str, Any]]) -> dict[str, Any]:
    aggregate_counts: dict[str, int] = {}
    max_silence = 0.0
    for round_result in rounds:
        for name, count in round_result.get("counts", {}).items():
            aggregate_counts[name] = aggregate_counts.get(name, 0) + int(count)
        max_silence = max(max_silence, float(round_result.get("max_silence", 0.0)))
    return {
        "aggregate_counts": aggregate_counts,
        "max_round_silence": round(max_silence, 3),
    }


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    if args.repetitions < 1:
        raise GateError("bad_repetitions", {"repetitions": args.repetitions})
    if args.outdir:
        os.makedirs(args.outdir, exist_ok=True)

    port = resolve_port(args.port)
    rounds: list[dict[str, Any]] = []

    for idx in range(1, args.repetitions + 1):
        reset_info = None
        if args.reset_each_round:
            log_path = os.path.join(args.outdir or ".", f"reset_round_{idx}.log")
            port_before_reset = {
                "port": port,
                "realpath": realpath_or_self(port),
                "tty_acm": tty_acm_snapshot(),
                "by_id": by_id_snapshot(),
            }
            reset_rc = openocd_reset(log_path, args.openocd_config, args.reset_timeout)
            reset_info = {"rc": reset_rc, "log": log_path, "port_before": port_before_reset}
            if reset_rc != 0:
                raise GateError(
                    "reset_failed",
                    {"round": idx, "reset": reset_info, "completed_rounds": rounds},
                )
            reset_info["cdc_wait_started_utc"] = iso_now()
            port_after_reset = wait_cdc_resolve(args.port, args.cdc_timeout)
            reset_info["port_after"] = port_after_reset
            port = port_after_reset["port"]
            time.sleep(args.post_reset_settle)

        try:
            rounds.append(run_one_round(args, port, idx, reset_info))
        except GateError as exc:
            payload = {
                "round": idx,
                "reset": reset_info,
                "completed_rounds": rounds,
            }
            payload.update(exc.payload)
            raise GateError(exc.reason, payload) from exc
        if args.outdir:
            write_json_result(os.path.join(args.outdir, f"round_{idx:03d}.json"), rounds[-1])
        if idx < args.repetitions and args.round_gap > 0:
            time.sleep(args.round_gap)

    if args.repetitions == 1 and not args.reset_each_round:
        return rounds[0]

    return {
        "timestamp_utc": iso_now(),
        "verdict": "GREEN",
        "reason": "ok",
        "port": port,
        "repetitions": args.repetitions,
        "reset_each_round": args.reset_each_round,
        "allow_param_list_fallback": args.allow_param_list_fallback,
        "rounds": rounds,
        **aggregate_rounds(rounds),
    }


def write_json_result(path: str, payload: dict[str, Any]) -> str:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    return path


def result_output_path(args: argparse.Namespace) -> str | None:
    if not args.outdir:
        return None
    return os.path.join(args.outdir, "sitl_on_hw_gate.json")


def print_text_result(result: dict[str, Any]) -> None:
    if "rounds" in result:
        last = result["rounds"][-1] if result["rounds"] else {}
        print(
            "GREEN "
            f"repetitions={result.get('repetitions')} "
            f"aggregate_counts={result.get('aggregate_counts')} "
            f"last_rates={last.get('rates_hz')} "
            f"max_round_silence={result.get('max_round_silence')}"
        )
        return
    print(
        "GREEN "
        f"params={result['params']} "
        f"counts={result['counts']} "
        f"rates={result.get('rates_hz')} "
        f"max_silence={result['max_silence']}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--heartbeat-timeout", type=float, default=25.0)
    parser.add_argument("--standby-timeout", type=float, default=45.0)
    parser.add_argument("--sample", type=float, default=30.0)
    parser.add_argument("--rate", type=int, default=5)
    parser.add_argument("--stream-warmup", type=float, default=8.0)
    parser.add_argument("--stream-request-retries", type=int, default=4)
    parser.add_argument("--interval-ack-timeout", type=float, default=3.0)
    parser.add_argument("--interval-get-timeout", type=float, default=5.0)
    parser.add_argument("--min-heartbeat-rate", type=float, default=0.8)
    parser.add_argument("--min-sensor-rate", type=float, default=2.0)
    parser.add_argument("--min-position-rate", type=float, default=1.0)
    parser.add_argument("--param-tolerance", type=float, default=0.01)
    parser.add_argument("--allow-param-list-fallback", action="store_true")
    parser.add_argument("--repetitions", type=int, default=1)
    parser.add_argument("--round-gap", type=float, default=2.0)
    parser.add_argument("--reset-each-round", action="store_true")
    parser.add_argument("--openocd-config", default="Tools/debug/openocd-f7.cfg")
    parser.add_argument("--reset-timeout", type=int, default=60)
    parser.add_argument("--cdc-timeout", type=float, default=45.0)
    parser.add_argument("--post-reset-settle", type=float, default=75.0)
    parser.add_argument("--ready-delay-after-params", type=float, default=0.0)
    parser.add_argument("--connect-attempts", type=int, default=8)
    parser.add_argument("--connect-gap", type=float, default=2.0)
    parser.add_argument("--outdir", default="")
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
        path = result_output_path(args)
        if path:
            result["json_path"] = write_json_result(path, result)
        print(json.dumps(result, indent=2, sort_keys=True), file=sys.stderr)
        return 1
    except Exception as exc:
        result = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": type(exc).__name__,
            "detail": str(exc),
        }
        path = result_output_path(args)
        if path:
            result["json_path"] = write_json_result(path, result)
        print(json.dumps(result, indent=2, sort_keys=True), file=sys.stderr)
        return 1

    path = result_output_path(args)
    if path:
        result["json_path"] = write_json_result(path, result)

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print_text_result(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
