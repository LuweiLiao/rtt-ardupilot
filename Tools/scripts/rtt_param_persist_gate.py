#!/usr/bin/env python3
"""RTT MAVLink parameter persistence gate.

This gate exercises the normal ArduPilot MAVLink PARAM_SET path and then uses
OpenOCD reset-run to prove the saved value survives a software reset. It restores
the original value before exit.
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

from rtt_openocd_guard import cleanup_openocd_quiet

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port

DEFAULT_PORT = MAVLINK_PORT
DEFAULT_PARAM_CANDIDATES = ("LOG_DISARMED", "SR0_RAW_SENS", "SR0_EXT_STAT")
_MAVUTIL: Any | None = None


class GateError(RuntimeError):
    def __init__(self, reason: str, payload: dict[str, Any]):
        super().__init__(reason)
        self.payload = payload


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def realpath_or_self(path: str) -> str:
    try:
        return os.path.realpath(path)
    except OSError:
        return path


def resolve_port(port_arg: str) -> str:
    return resolve_mavlink_port(port_arg)


def load_mavutil() -> Any:
    global _MAVUTIL
    if _MAVUTIL is None:
        try:
            from pymavlink import mavutil  # type: ignore
        except ImportError as exc:
            raise RuntimeError("pymavlink_not_installed") from exc
        _MAVUTIL = mavutil
    return _MAVUTIL


def connect(port: str, source_system: int = 248, timeout_s: float = 25.0) -> Any:
    mavutil = load_mavutil()
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


def connect_retry(port: str, source_system: int, attempts: int = 8,
                  timeout_s: float = 12.0, gap_s: float = 2.0) -> Any:
    last_error: str | None = None
    for attempt in range(attempts):
        try:
            return connect(port, source_system=source_system, timeout_s=timeout_s)
        except Exception as exc:
            last_error = str(exc)
            time.sleep(gap_s)
    raise RuntimeError(f"connect_retry_failed:{last_error}")


def drain(conn: Any, seconds: float = 0.3) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def param_name(msg: Any) -> str:
    pid = msg.param_id
    if isinstance(pid, bytes):
        return pid.decode(errors="ignore").rstrip("\x00")
    return str(pid).rstrip("\x00")


def request_param_direct(conn: Any, name: str, timeout_s: float = 8.0) -> float:
    drain(conn, 0.2)
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
    raise RuntimeError(f"param_read_timeout:{name}")


def request_param_from_list(conn: Any, name: str, timeout_s: float = 45.0) -> float:
    drain(conn, 0.5)
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    deadline = time.monotonic() + timeout_s
    seen = 0
    reported_count: int | None = None
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1.0)
        if msg is None:
            continue
        seen += 1
        reported_count = int(msg.param_count)
        if param_name(msg) == name:
            return float(msg.param_value)
        if reported_count > 0 and seen >= reported_count:
            break
    raise RuntimeError(f"param_list_timeout:{name}:seen={seen}:count={reported_count}")


def request_param(conn: Any, name: str, timeout_s: float = 8.0) -> float:
    try:
        return request_param_direct(conn, name, timeout_s=timeout_s)
    except Exception:
        return request_param_from_list(conn, name)


def set_param(conn: Any, name: str, value: float, timeout_s: float = 10.0) -> float:
    mavutil = load_mavutil()
    drain(conn, 0.2)
    conn.mav.param_set_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii"),
        float(value),
        mavutil.mavlink.MAV_PARAM_TYPE_REAL32,
    )
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1.0)
        if msg is None:
            continue
        if param_name(msg) == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_set_timeout:{name}")


def choose_param(conn: Any, candidates: tuple[str, ...]) -> tuple[str, float]:
    errors: dict[str, str] = {}
    for name in candidates:
        try:
            return name, request_param(conn, name)
        except Exception as exc:
            errors[name] = str(exc)
    raise RuntimeError("no_candidate_param:" + json.dumps(errors, sort_keys=True))


def alternate_value(original: float) -> float:
    # Keep stream-rate perturbations small and reversible.
    orig_i = int(round(original))
    if orig_i == 0:
        return 1.0
    if orig_i == 1:
        return 2.0
    return 1.0


def wait_cdc(port: str, timeout_s: float = 35.0) -> None:
    deadline = time.monotonic() + timeout_s
    display = port
    while time.monotonic() < deadline:
        if os.path.exists(port):
            return
        # The by-id symlink can disappear during reset; also accept its real
        # target if the symlink has not been recreated yet.
        real = realpath_or_self(port)
        if real != display and os.path.exists(real):
            return
        time.sleep(1.0)
    raise RuntimeError(f"cdc_not_back:{port}")


def cleanup_openocd() -> None:
    cleanup_openocd_quiet()

def openocd_reset(log_path: str, timeout_s: int = 60) -> int:
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


def try_restore(port: str, name: str, original: float, save_wait: float) -> dict[str, Any]:
    """Best-effort restoration after a late failure."""
    result: dict[str, Any] = {"param": name, "original": original}
    try:
        wait_cdc(port)
        conn = connect_retry(port, source_system=253)
        try:
            restored = set_param(conn, name, original)
            result["set_restore"] = restored
            time.sleep(save_wait)
        finally:
            conn.close()
    except Exception as exc:
        result["restore_error"] = str(exc)
    return result


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    os.makedirs(args.outdir, exist_ok=True)
    port = resolve_port(args.port)
    candidates = tuple(p.strip() for p in args.param.split(",") if p.strip())
    if not candidates:
        candidates = DEFAULT_PARAM_CANDIDATES

    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "candidates": candidates,
        "steps": [],
    }

    conn = connect(port)
    try:
        name, original = choose_param(conn, candidates)
        test_value = alternate_value(original)
        payload.update({"param": name, "original": original, "test_value": test_value})

        written = set_param(conn, name, test_value)
        payload["steps"].append({"set_test": written})
        time.sleep(args.save_wait)
    finally:
        conn.close()

    reset1_log = os.path.join(args.outdir, "reset_after_set.log")
    try:
        reset1_rc = openocd_reset(reset1_log)
        payload["steps"].append({"reset_after_set_rc": reset1_rc, "log": reset1_log})
        if reset1_rc != 0:
            raise RuntimeError(f"reset_after_set_failed:{reset1_rc}")

        wait_cdc(port)
        conn = connect_retry(port, source_system=249)
        try:
            after_reset = request_param(conn, payload["param"])
            payload["after_reset"] = after_reset
            if abs(after_reset - payload["test_value"]) > args.tolerance:
                raise RuntimeError(f"persist_mismatch:{after_reset}!={payload['test_value']}")

            restored = set_param(conn, payload["param"], payload["original"])
            payload["steps"].append({"set_restore": restored})
            time.sleep(args.save_wait)
        finally:
            conn.close()

        reset2_log = os.path.join(args.outdir, "reset_after_restore.log")
        reset2_rc = openocd_reset(reset2_log)
        payload["steps"].append({"reset_after_restore_rc": reset2_rc, "log": reset2_log})
        if reset2_rc != 0:
            raise RuntimeError(f"reset_after_restore_failed:{reset2_rc}")

        wait_cdc(port)
        conn = connect_retry(port, source_system=250)
        try:
            restored_after_reset = request_param(conn, payload["param"])
            payload["restored_after_reset"] = restored_after_reset
            if abs(restored_after_reset - payload["original"]) > args.tolerance:
                raise RuntimeError(
                    f"restore_mismatch:{restored_after_reset}!={payload['original']}")
        finally:
            conn.close()
    except Exception:
        payload["best_effort_restore"] = try_restore(
            port, payload["param"], payload["original"], args.save_wait)
        payload["verdict"] = "RED"
        payload["reason"] = str(sys.exc_info()[1])
        raise GateError(payload["reason"], payload)

    payload["verdict"] = "GREEN"
    payload["reason"] = "param_persist_restore_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="RTT parameter persistence gate")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--param", default=",".join(DEFAULT_PARAM_CANDIDATES))
    parser.add_argument("--save-wait", type=float, default=3.0)
    parser.add_argument("--tolerance", type=float, default=0.01)
    args = parser.parse_args()

    resolved_port = args.port
    try:
        resolved_port = resolve_port(args.port)
    except Exception:
        pass

    try:
        payload = run_gate(args)
        rc = 0
    except GateError as exc:
        cleanup_openocd()
        payload = exc.payload
        rc = 2
    except Exception as exc:
        cleanup_openocd()
        payload = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": str(exc),
            "port": resolved_port,
        }
        rc = 2

    os.makedirs(args.outdir, exist_ok=True)
    json_path = os.path.join(args.outdir, "param_persist.json")
    payload["json_path"] = json_path
    with open(json_path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return rc


if __name__ == "__main__":
    sys.exit(main())
