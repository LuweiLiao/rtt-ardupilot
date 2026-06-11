#!/usr/bin/env python3
"""Sequential multi-parameter persistence gate for AP_Param save_queue.

This gate uses the normal MAVLink PARAM_SET handshake for 30 real MAV stream
parameters. It proves the changed values survive OpenOCD reset-run, then
restores the original values and proves the restoration survives another reset.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Any

from pymavlink import mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"
PARAMS = [
    f"MAV{port}_{suffix}"
    for port in (1, 2, 3)
    for suffix in (
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
    for args in (
        ["pkill", "-9", "-x", "openocd"],
        ["pkill", "-9", "-x", "openoccd"],
        ["pkill", "-9", "-f", "openocd"],
        ["pkill", "-9", "-f", "openoccd"],
    ):
        subprocess.run(args, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def openocd_reset(log_path: str, timeout_s: int = 70) -> int:
    with open(log_path, "w", encoding="utf-8") as log:
        rc = subprocess.run(
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
        ).returncode
    cleanup_openocd()
    return rc


def wait_cdc(port: str, timeout_s: float = 45.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(port) or os.path.exists(os.path.realpath(port)):
            return
        time.sleep(0.5)
    raise RuntimeError(f"cdc_not_back:{port}")


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def connect(port: str, source_system: int, timeout_s: float = 15.0) -> tuple[Any, dict[str, Any]]:
    last_error = "none"
    for _ in range(10):
        conn = None
        try:
            conn = mavutil.mavlink_connection(
                port,
                baud=115200,
                robust_parsing=True,
                source_system=source_system,
            )
            hb = conn.wait_heartbeat(timeout=timeout_s)
            if hb is None or conn.target_system == 0:
                raise RuntimeError("no_heartbeat")
            ready = wait_ready(conn)
            return conn, {"heartbeat": hb.to_dict(), "ready": ready}
        except Exception as exc:
            last_error = str(exc)
            if conn is not None:
                conn.close()
            time.sleep(1.5)
    raise RuntimeError(f"connect_failed:{last_error}")


def wait_ready(conn: Any, timeout_s: float = 25.0) -> dict[str, Any]:
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
    return {"ready_s": None, "status_histogram": hist, "last": last}


def drain(conn: Any, seconds: float = 0.12) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def request_param(conn: Any, name: str, timeout_s: float = 8.0) -> float:
    drain(conn)
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        -1,
    )
    deadline = time.monotonic() + timeout_s
    seen = []
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        got = param_name(msg)
        if len(seen) < 12:
            seen.append(got)
        if got == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_read_timeout:{name}:seen={seen}")


def set_param(conn: Any, name: str, value: float, timeout_s: float = 10.0) -> float:
    drain(conn)
    conn.mav.param_set_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        float(value),
        mavutil.mavlink.MAV_PARAM_TYPE_REAL32,
    )
    deadline = time.monotonic() + timeout_s
    seen = []
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        got = param_name(msg)
        if len(seen) < 12:
            seen.append(got)
        if got == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_set_timeout:{name}:seen={seen}")


def read_values(conn: Any, names: list[str]) -> dict[str, float]:
    return {name: request_param(conn, name) for name in names}


def set_values_sequential(conn: Any, values: dict[str, float]) -> dict[str, float]:
    written: dict[str, float] = {}
    for name, value in values.items():
        written[name] = set_param(conn, name, value)
        time.sleep(0.08)
    return written


def alternate_value(original: float) -> float:
    rounded = int(round(original))
    if rounded <= 0:
        return 1.0
    return float(max(0, rounded - 1))


def compare(actual: dict[str, float], expected: dict[str, float], tolerance: float) -> dict[str, str]:
    failures: dict[str, str] = {}
    for name, exp in expected.items():
        got = actual.get(name)
        if got is None:
            failures[name] = "missing"
        elif abs(got - exp) > tolerance:
            failures[name] = f"{got}!={exp}"
    return failures


def best_effort_restore(port: str, originals: dict[str, float], save_wait: float,
                        outdir: str) -> dict[str, Any]:
    result: dict[str, Any] = {"attempted": bool(originals)}
    if not originals:
        return result
    try:
        wait_cdc(port)
        conn, meta = connect(port, source_system=238)
        result["connect"] = meta
        try:
            result["set_restore"] = set_values_sequential(conn, originals)
            time.sleep(save_wait)
        finally:
            conn.close()
        log = os.path.join(outdir, "best_effort_restore_reset.log")
        result["reset_rc"] = openocd_reset(log)
        result["reset_log"] = log
    except Exception as exc:
        result["error"] = str(exc)
    return result


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    os.makedirs(args.outdir, exist_ok=True)
    port = resolve_port(args.port)
    selected = PARAMS[:args.count]
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "selected": selected,
        "selected_count": len(selected),
        "steps": [],
    }
    originals: dict[str, float] = {}
    try:
        conn, meta = connect(port, source_system=237)
        payload["connect_initial"] = meta
        try:
            originals = read_values(conn, selected)
            test_values = {name: alternate_value(value) for name, value in originals.items()}
            payload["originals"] = originals
            payload["test_values"] = test_values
            payload["set_test"] = set_values_sequential(conn, test_values)
            time.sleep(args.save_wait)
        finally:
            conn.close()

        reset_set_log = os.path.join(args.outdir, "reset_after_sequence_set.log")
        reset_set_rc = openocd_reset(reset_set_log)
        payload["steps"].append({"reset_after_sequence_set_rc": reset_set_rc, "log": reset_set_log})
        if reset_set_rc != 0:
            raise RuntimeError(f"reset_after_sequence_set_failed:{reset_set_rc}")

        wait_cdc(port)
        conn, meta = connect(port, source_system=236)
        payload["connect_after_set_reset"] = meta
        try:
            after_reset = read_values(conn, selected)
            payload["after_reset"] = after_reset
            failures = compare(after_reset, payload["test_values"], args.tolerance)
            if failures:
                payload["persist_failures"] = failures
                raise RuntimeError("persist_failures")
            payload["set_restore"] = set_values_sequential(conn, originals)
            time.sleep(args.save_wait)
        finally:
            conn.close()

        reset_restore_log = os.path.join(args.outdir, "reset_after_sequence_restore.log")
        reset_restore_rc = openocd_reset(reset_restore_log)
        payload["steps"].append({"reset_after_sequence_restore_rc": reset_restore_rc, "log": reset_restore_log})
        if reset_restore_rc != 0:
            raise RuntimeError(f"reset_after_sequence_restore_failed:{reset_restore_rc}")

        wait_cdc(port)
        conn, meta = connect(port, source_system=235)
        payload["connect_after_restore_reset"] = meta
        try:
            restored_after_reset = read_values(conn, selected)
            payload["restored_after_reset"] = restored_after_reset
            restore_failures = compare(restored_after_reset, originals, args.tolerance)
            if restore_failures:
                payload["restore_failures"] = restore_failures
                raise RuntimeError("restore_failures")
        finally:
            conn.close()
    except Exception as exc:
        payload["verdict"] = "RED"
        payload["reason"] = str(exc)
        payload["best_effort_restore"] = best_effort_restore(port, originals, args.save_wait, args.outdir)
        return payload

    payload["verdict"] = "GREEN"
    payload["reason"] = "sequential_param_persist_restore_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--save-wait", type=float, default=8.0)
    parser.add_argument("--tolerance", type=float, default=0.01)
    args = parser.parse_args()

    payload = run_gate(args)
    json_path = os.path.join(args.outdir, "param_sequence_persist.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    cleanup_openocd()
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
