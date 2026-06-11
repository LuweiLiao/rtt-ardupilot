#!/usr/bin/env python3
"""Burst parameter persistence gate for AP_Param save_queue changes.

This gate stresses the normal MAVLink PARAM_SET path by changing more than
30 low-risk stream-rate parameters in a quick burst, then proving the values
survive OpenOCD reset-run. It restores the original values before exit.
"""

from __future__ import annotations

import argparse
import glob
import json
import math
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Any

from pymavlink import mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"
STREAM_SUFFIXES = (
    "RAW_SENS",
    "EXT_STAT",
    "RC_CHAN",
    "RAW_CTRL",
    "POSITION",
    "EXTRA1",
    "EXTRA2",
    "EXTRA3",
    "PARAMS",
    "ADSB",
)
PREFERRED_PARAMS = [
    f"MAV{port}_{suffix}"
    for port in (1, 2, 3)
    for suffix in STREAM_SUFFIXES
    if suffix != "OPTIONS"
] + [
    f"RC{channel}_DZ"
    for channel in range(10, 17)
]


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
    matches = sorted(glob.glob("/dev/ttyACM*"))
    if matches:
        return matches[-1]
    raise RuntimeError("no_cdc_port")


def cleanup_openocd() -> None:
    subprocess.run(["pkill", "-9", "-x", "openocd"], check=False)
    subprocess.run(["pkill", "-9", "-x", "openoccd"], check=False)
    subprocess.run(["pkill", "-9", "-f", "openocd"], check=False)
    subprocess.run(["pkill", "-9", "-f", "openoccd"], check=False)


def openocd_reset(log_path: str, timeout_s: int = 70) -> int:
    with open(log_path, "w", encoding="utf-8") as log:
        proc = subprocess.run(
            [
                "timeout",
                str(timeout_s),
                "openocd",
                "-f",
                "interface/stlink.cfg",
                "-f",
                "target/stm32f7x.cfg",
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


def wait_cdc(port: str, timeout_s: float = 45.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(port):
            return
        real = os.path.realpath(port)
        if real != port and os.path.exists(real):
            return
        time.sleep(0.5)
    raise RuntimeError(f"cdc_not_back:{port}")


def connect(port: str, source_system: int, timeout_s: float = 25.0) -> Any:
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


def connect_retry(port: str, source_system: int) -> Any:
    last_error = "none"
    for _ in range(10):
        try:
            return connect(port, source_system=source_system, timeout_s=12.0)
        except Exception as exc:
            last_error = str(exc)
            time.sleep(1.5)
    raise RuntimeError(f"connect_retry_failed:{last_error}")


def drain(conn: Any, seconds: float = 0.2) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def request_param(conn: Any, name: str, timeout_s: float = 2.0) -> float | None:
    drain(conn, 0.05)
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        -1,
    )
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.25)
        if msg is None:
            continue
        if param_name(msg) == name:
            value = float(msg.param_value)
            if math.isfinite(value):
                return value
            return None
    return None


def discover_params(conn: Any, min_count: int, timeout_s: float) -> list[str]:
    rx = re.compile(r"^SR[0-9]+_(" + "|".join(STREAM_SUFFIXES) + r")$")
    found: set[str] = set()
    drain(conn, 0.5)
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    deadline = time.monotonic() + timeout_s
    total = None
    seen = 0
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        seen += 1
        total = int(msg.param_count)
        name = param_name(msg)
        if rx.match(name):
            found.add(name)
        if len(found) >= min_count:
            break
        if total and seen >= total:
            break
    return sorted(found)


def candidate_scan_names() -> list[str]:
    mav_and_rc = list(PREFERRED_PARAMS)
    sr_names = [
        f"SR{port}_{suffix}"
        for port in range(16)
        for suffix in STREAM_SUFFIXES
    ]
    return mav_and_rc + [name for name in sr_names if name not in mav_and_rc]


def alternate_value(original: float) -> float:
    rounded = int(round(original))
    if rounded <= 0:
        return 1.0
    return float(max(0, rounded - 1))


def send_param_set_burst(conn: Any, values: dict[str, float]) -> dict[str, Any]:
    drain(conn, 0.2)
    start = time.monotonic()
    for name, value in values.items():
        conn.mav.param_set_send(
            conn.target_system,
            conn.target_component,
            name.encode("ascii"),
            float(value),
            mavutil.mavlink.MAV_PARAM_TYPE_REAL32,
        )
    pending = set(values)
    observed: dict[str, float] = {}
    deadline = time.monotonic() + max(20.0, len(values) * 0.8)
    while pending and time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        name = param_name(msg)
        if name in pending:
            observed[name] = float(msg.param_value)
            pending.remove(name)
    return {
        "sent": len(values),
        "observed": len(observed),
        "missing": sorted(pending),
        "elapsed_s": round(time.monotonic() - start, 3),
    }


def read_values(conn: Any, names: list[str]) -> dict[str, float]:
    values: dict[str, float] = {}
    for name in names:
        value = request_param(conn, name, timeout_s=3.0)
        if value is None:
            raise RuntimeError(f"param_read_failed:{name}")
        values[name] = value
    return values


def compare_values(actual: dict[str, float], expected: dict[str, float],
                   tolerance: float) -> dict[str, str]:
    failures: dict[str, str] = {}
    for name, exp in expected.items():
        got = actual.get(name)
        if got is None:
            failures[name] = "missing"
        elif abs(got - exp) > tolerance:
            failures[name] = f"{got}!={exp}"
    return failures


def best_effort_restore(port: str, originals: dict[str, float],
                        save_wait: float, outdir: str) -> dict[str, Any]:
    result: dict[str, Any] = {"attempted": bool(originals)}
    if not originals:
        return result
    try:
        wait_cdc(port)
        conn = connect_retry(port, source_system=254)
        try:
            result["restore_burst"] = send_param_set_burst(conn, originals)
            time.sleep(save_wait)
        finally:
            conn.close()
        reset_log = os.path.join(outdir, "best_effort_restore_reset.log")
        result["reset_rc"] = openocd_reset(reset_log)
        result["reset_log"] = reset_log
    except Exception as exc:
        result["error"] = str(exc)
    return result


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    os.makedirs(args.outdir, exist_ok=True)
    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "target_count": args.count,
        "steps": [],
    }
    originals: dict[str, float] = {}
    try:
        conn = connect_retry(port, source_system=251)
        try:
            selected_names: list[str] = []
            payload["discovered"] = []
            if len(selected_names) < args.count:
                scanned = []
                for name in candidate_scan_names():
                    if name in selected_names:
                        continue
                    value = request_param(conn, name, timeout_s=3.0)
                    if value is None:
                        continue
                    scanned.append(name)
                    selected_names.append(name)
                    if len(selected_names) >= args.count:
                        break
                payload["direct_scanned_added"] = scanned
            if len(selected_names) < args.count:
                raise RuntimeError(
                    f"not_enough_params:{len(selected_names)}<{args.count}")
            originals = read_values(conn, selected_names)
            test_values = {
                name: alternate_value(value)
                for name, value in originals.items()
            }
            payload["selected"] = selected_names
            payload["originals"] = originals
            payload["test_values"] = test_values
            payload["set_test_burst"] = send_param_set_burst(conn, test_values)
            if payload["set_test_burst"]["missing"]:
                raise RuntimeError("test_burst_missing_ack")
            time.sleep(args.save_wait)
        finally:
            conn.close()

        reset_set_log = os.path.join(args.outdir, "reset_after_burst_set.log")
        reset_set_rc = openocd_reset(reset_set_log)
        payload["steps"].append({
            "reset_after_burst_set_rc": reset_set_rc,
            "log": reset_set_log,
        })
        if reset_set_rc != 0:
            raise RuntimeError(f"reset_after_burst_set_failed:{reset_set_rc}")

        wait_cdc(port)
        conn = connect_retry(port, source_system=252)
        try:
            after_reset = read_values(conn, payload["selected"])
            payload["after_reset"] = after_reset
            failures = compare_values(
                after_reset,
                payload["test_values"],
                args.tolerance,
            )
            if failures:
                payload["persist_failures"] = failures
                raise RuntimeError("persist_failures")
            payload["set_restore_burst"] = send_param_set_burst(conn, originals)
            if payload["set_restore_burst"]["missing"]:
                raise RuntimeError("restore_burst_missing_ack")
            time.sleep(args.save_wait)
        finally:
            conn.close()

        reset_restore_log = os.path.join(
            args.outdir,
            "reset_after_burst_restore.log",
        )
        reset_restore_rc = openocd_reset(reset_restore_log)
        payload["steps"].append({
            "reset_after_burst_restore_rc": reset_restore_rc,
            "log": reset_restore_log,
        })
        if reset_restore_rc != 0:
            raise RuntimeError(
                f"reset_after_burst_restore_failed:{reset_restore_rc}")

        wait_cdc(port)
        conn = connect_retry(port, source_system=253)
        try:
            restored_after_reset = read_values(conn, payload["selected"])
            payload["restored_after_reset"] = restored_after_reset
            restore_failures = compare_values(
                restored_after_reset,
                originals,
                args.tolerance,
            )
            if restore_failures:
                payload["restore_failures"] = restore_failures
                raise RuntimeError("restore_failures")
        finally:
            conn.close()
    except Exception as exc:
        payload["verdict"] = "RED"
        payload["reason"] = str(exc)
        payload["best_effort_restore"] = best_effort_restore(
            port,
            originals,
            args.save_wait,
            args.outdir,
        )
        return payload

    payload["verdict"] = "GREEN"
    payload["reason"] = "burst_param_persist_restore_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--count", type=int, default=36)
    parser.add_argument("--save-wait", type=float, default=8.0)
    parser.add_argument("--tolerance", type=float, default=0.01)
    parser.add_argument("--discovery-timeout", type=float, default=55.0)
    args = parser.parse_args()

    payload = run_gate(args)
    json_path = os.path.join(args.outdir, "param_burst_persist.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    cleanup_openocd()
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
