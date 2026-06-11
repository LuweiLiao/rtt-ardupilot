#!/usr/bin/env python3
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
        "/dev/ttyACM*",
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    raise RuntimeError("no_cdc_port")


def cleanup_openocd() -> None:
    subprocess.run(["pkill", "-9", "-x", "openocd"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-x", "openoccd"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


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
        if os.path.exists(port):
            return
        real = os.path.realpath(port)
        if real != port and os.path.exists(real):
            return
        time.sleep(0.5)
    raise RuntimeError(f"cdc_not_back:{port}")


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def drain(conn: Any, seconds: float = 0.15) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def connect(port: str, source_system: int) -> Any:
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
            hb = conn.wait_heartbeat(timeout=15)
            if hb is None or conn.target_system == 0:
                raise RuntimeError("no_heartbeat")
            return conn
        except Exception as exc:
            last_error = str(exc)
            if conn is not None:
                conn.close()
            time.sleep(1.5)
    raise RuntimeError(f"connect_failed:{last_error}")


def request_param(conn: Any, name: str, timeout_s: float = 8.0) -> float:
    drain(conn)
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        -1,
    )
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        if param_name(msg) == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_read_timeout:{name}")


def set_param(conn: Any, name: str, value: float, timeout_s: float = 10.0) -> float | None:
    drain(conn)
    conn.mav.param_set_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        float(value),
        mavutil.mavlink.MAV_PARAM_TYPE_REAL32,
    )
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if msg is None:
            continue
        if param_name(msg) == name:
            return float(msg.param_value)
    return None


def read_values(conn: Any, names: list[str]) -> dict[str, float]:
    return {name: request_param(conn, name) for name in names}


def compare(actual: dict[str, float], expected: dict[str, float],
            tolerance: float) -> dict[str, str]:
    failures: dict[str, str] = {}
    for name, exp in expected.items():
        got = actual.get(name)
        if got is None:
            failures[name] = "missing"
        elif abs(got - exp) > tolerance:
            failures[name] = f"{got}!={exp}"
    return failures


def run(args: argparse.Namespace) -> dict[str, Any]:
    os.makedirs(args.outdir, exist_ok=True)
    with open(args.burst_json, "r", encoding="utf-8") as infile:
        burst = json.load(infile)
    originals = {name: float(value) for name, value in burst["originals"].items()}
    names = list(burst["selected"])
    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "burst_json": args.burst_json,
        "selected_count": len(names),
        "steps": [],
    }

    wait_cdc(port)
    conn = connect(port, source_system=232)
    try:
        first = read_values(conn, names)
    finally:
        conn.close()
    payload["first_readback"] = first
    first_failures = compare(first, originals, args.tolerance)
    payload["first_failures"] = first_failures

    if first_failures:
        wait_cdc(port)
        conn = connect(port, source_system=231)
        restore_results: dict[str, Any] = {}
        try:
            for name in names:
                restore_results[name] = set_param(conn, name, originals[name])
                time.sleep(0.08)
        finally:
            conn.close()
        payload["sequential_restore"] = restore_results
        time.sleep(args.save_wait)

        reset_log = os.path.join(args.outdir, "reset_after_confirm_restore.log")
        reset_rc = openocd_reset(reset_log)
        payload["steps"].append({"reset_after_confirm_restore_rc": reset_rc, "log": reset_log})
        if reset_rc != 0:
            payload["verdict"] = "RED"
            payload["reason"] = f"reset_after_confirm_restore_failed:{reset_rc}"
            return payload

        wait_cdc(port)
        conn = connect(port, source_system=230)
        try:
            final = read_values(conn, names)
        finally:
            conn.close()
        payload["final_readback"] = final
        final_failures = compare(final, originals, args.tolerance)
        payload["final_failures"] = final_failures
        if final_failures:
            payload["verdict"] = "RED"
            payload["reason"] = "restore_confirm_failed"
            return payload

    payload["verdict"] = "GREEN"
    payload["reason"] = "burst_red_ack_window_state_restored"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="auto")
    parser.add_argument("--burst-json", required=True)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--save-wait", type=float, default=8.0)
    parser.add_argument("--tolerance", type=float, default=0.01)
    args = parser.parse_args()

    payload = run(args)
    json_path = os.path.join(args.outdir, "param_restore_confirm.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    cleanup_openocd()
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    sys.exit(main())
