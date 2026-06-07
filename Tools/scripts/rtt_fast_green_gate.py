#!/usr/bin/env python3
"""
Deterministic AP_HAL_RTT fast GREEN/RED gate.

This is intentionally non-destructive:
- no flash writes
- no OpenOCD/GDB during the sample window
- no raw serial byte-count verdicts
- one exclusive CDC/MAVLink connection per run

Anti-patterns this fixture is meant to avoid:
- Sampling while OpenOCD/GDB has halted the MCU.
- Treating fixed sleep as boot readiness; use a STANDBY heartbeat gate instead.
- Treating raw serial bytes as evidence of vehicle health.
- Reusing one long-lived connection across meta-runs.
"""

from __future__ import annotations

import argparse
import fcntl
import glob
import json
import os
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Any


LOCK_PATH = "/tmp/fast_green_gate.lock"
DEFAULT_SETTLE_MAX_S = 40.0
DEFAULT_SAMPLE_S = 20.0
META_RUN_GAP_S = 8.0

GREEN_HEARTBEAT_MIN = 15
GREEN_MAX_SILENCE_S = 5.0
GREEN_RAW_IMU_MIN = 60
GREEN_ATTITUDE_MIN = 60
GREEN_SCALED_PRESSURE_MIN = 40
GREEN_ZACC_ABS_MIN_MG = 800
GREEN_ZACC_ABS_MAX_MG = 1200
GREEN_BARO_MIN_HPA = 900.0
GREEN_BARO_MAX_HPA = 1100.0
GREEN_EKF_MASK = 0x07
GREEN_EKF_EXPECT = 0x07

MSG_RAW_IMU = 27
MSG_SCALED_PRESSURE = 29
MSG_ATTITUDE = 30
MSG_EKF_STATUS_REPORT = 193


def repo_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def output_dir() -> str:
    return os.path.join(repo_root(), "results", "validation", "fast_green")


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def log(message: str, quiet: bool) -> None:
    if not quiet:
        print(message, flush=True)


def load_mavutil():
    try:
        from pymavlink import mavutil  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pymavlink_not_installed") from exc
    return mavutil


def resolve_port(port_arg: str) -> tuple[str | None, str | None]:
    """Return (port_for_open, display_port). Prefer stable by-id names."""
    if port_arg != "auto":
        return (port_arg, port_arg) if os.path.exists(port_arg) else (None, port_arg)

    by_id_dir = "/dev/serial/by-id"
    excluded_realpaths: set[str] = set()
    if os.path.isdir(by_id_dir):
        for name in os.listdir(by_id_dir):
            # CH343/CH341 USB-TTL is the UART7 RT-Thread console, not MAVLink CDC.
            if "1a86_USB_Single_Serial" in name or "STLink" in name or "ST-LINK" in name:
                excluded_realpaths.add(realpath_or_self(os.path.join(by_id_dir, name)))

    patterns = [
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00",
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC*",
        "/dev/serial/by-id/*CUAV*CDC*",
        "/dev/serial/by-id/*ArduPilot*CUAV*",
        "/dev/serial/by-id/*ArduPilot*",
    ]
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0], matches[0]

    acms = [
        dev for dev in sorted(glob.glob("/dev/ttyACM*"))
        if realpath_or_self(dev) not in excluded_realpaths
    ]
    return (acms[0], acms[0]) if acms else (None, "auto")


def pgrep_exact(name: str) -> list[str]:
    try:
        output = subprocess.check_output(["pgrep", "-x", name], text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    return [line.strip() for line in output.splitlines() if line.strip()]


def pgrep_cmdline(pattern: str) -> list[str]:
    try:
        output = subprocess.check_output(["pgrep", "-f", pattern], text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    my_pid = str(os.getpid())
    return [line.strip() for line in output.splitlines() if line.strip() and line.strip() != my_pid]


def tty_acm_devices() -> list[str]:
    return sorted(glob.glob("/dev/ttyACM*"))


def realpath_or_self(path: str) -> str:
    try:
        return os.path.realpath(path)
    except OSError:
        return path


def proc_fd_holders(paths: list[str]) -> list[str]:
    targets = {realpath_or_self(path) for path in paths if os.path.exists(path)}
    if not targets:
        return []

    holders: list[str] = []
    my_pid = str(os.getpid())
    for proc_pid in glob.glob("/proc/[0-9]*"):
        pid = os.path.basename(proc_pid)
        if pid == my_pid:
            continue

        fd_dir = os.path.join(proc_pid, "fd")
        try:
            fds = os.listdir(fd_dir)
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue

        matched = False
        for fd in fds:
            fd_path = os.path.join(fd_dir, fd)
            try:
                if os.path.realpath(fd_path) in targets:
                    matched = True
                    break
            except (FileNotFoundError, PermissionError, ProcessLookupError, OSError):
                continue

        if matched:
            command = pid
            try:
                with open(os.path.join(proc_pid, "comm"), "r", encoding="utf-8") as comm:
                    command = comm.read().strip() or pid
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                pass
            holders.append(f"{pid}:{command}")

    return holders


def check_fixture_dirty(port: str | None = None) -> tuple[bool, str, dict[str, Any]]:
    openocd = pgrep_exact("openocd")
    if openocd:
        return True, "dirty_openocd", {"pids": openocd}

    gdb = pgrep_exact("gdb") + pgrep_cmdline("arm-none-eabi-gdb")
    if gdb:
        return True, "dirty_gdb", {"pids": gdb}

    paths = tty_acm_devices()
    if port and os.path.exists(port):
        paths.append(port)

    holders = proc_fd_holders(sorted(set(paths)))
    if holders:
        return True, "dirty_ttyACM", {"holders": holders}

    return False, "", {}


def request_stabilizing_streams(conn: Any, mavutil: Any, rate_hz: int = 5) -> None:
    """Request only L1-health streams. This is runtime-only and not persisted."""
    for stream_id in (
        mavutil.mavlink.MAV_DATA_STREAM_EXTRA1,
        mavutil.mavlink.MAV_DATA_STREAM_RAW_SENSORS,
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
    for msg_id in (
        MSG_RAW_IMU,
        MSG_ATTITUDE,
        MSG_EKF_STATUS_REPORT,
        MSG_SCALED_PRESSURE,
    ):
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


def wait_standby(conn: Any, settle_max_s: float) -> tuple[bool, float, Any | None]:
    start = time.monotonic()
    last_hb = None
    while time.monotonic() - start < settle_max_s:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if msg is None:
            continue
        last_hb = msg
        if getattr(msg, "system_status", None) == 3:
            return True, time.monotonic() - start, last_hb
    return False, time.monotonic() - start, last_hb


def drain(conn: Any, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.02)


def sample_window(conn: Any, sample_s: float) -> dict[str, Any]:
    counts = {
        "HEARTBEAT": 0,
        "RAW_IMU": 0,
        "ATTITUDE": 0,
        "EKF_STATUS_REPORT": 0,
        "SCALED_PRESSURE": 0,
    }
    heartbeat_times: list[float] = []
    zacc_mg: int | None = None
    ekf_flags: int | None = None
    baro_hpa: float | None = None

    end = time.monotonic() + sample_s
    while time.monotonic() < end:
        msg = conn.recv_match(blocking=True, timeout=1.0)
        if msg is None:
            continue

        msg_type = msg.get_type()
        if msg_type not in counts:
            continue

        counts[msg_type] += 1
        if msg_type == "HEARTBEAT":
            heartbeat_times.append(time.monotonic())
        elif msg_type == "RAW_IMU":
            zacc_mg = int(msg.zacc)
        elif msg_type == "EKF_STATUS_REPORT":
            ekf_flags = int(msg.flags)
        elif msg_type == "SCALED_PRESSURE":
            baro_hpa = float(msg.press_abs)

    heartbeat_gaps = [
        heartbeat_times[i] - heartbeat_times[i - 1]
        for i in range(1, len(heartbeat_times))
    ]
    max_silence = max(heartbeat_gaps) if heartbeat_gaps else 0.0

    return {
        "counts": counts,
        "max_silence": round(max_silence, 3),
        "zacc_mg": zacc_mg,
        "ekf_flags": ekf_flags,
        "baro_hpa": baro_hpa,
    }


def firmware_hint(conn: Any, mavutil: Any) -> str:
    try:
        conn.mav.command_long_send(
            conn.target_system,
            conn.target_component,
            mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE,
            0,
            mavutil.mavlink.MAVLINK_MSG_ID_AUTOPILOT_VERSION,
            0,
            0,
            0,
            0,
            0,
            0,
        )
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            msg = conn.recv_match(type="AUTOPILOT_VERSION", blocking=True, timeout=0.5)
            if msg is None:
                continue
            version = getattr(msg, "flight_sw_version", 0)
            if version:
                return f"flight_sw_version=0x{version:08x}"
    except Exception:
        pass
    return os.environ.get("FAST_GREEN_FIRMWARE_HINT", "unknown")


def evaluate(standby_ok: bool, sample: dict[str, Any]) -> tuple[str, str]:
    if not standby_ok:
        return "RED", "no_standby"

    counts = sample["counts"]
    if counts["HEARTBEAT"] < GREEN_HEARTBEAT_MIN:
        return "RED", "hb_low"
    if sample["max_silence"] > GREEN_MAX_SILENCE_S:
        return "RED", "hb_gap"

    zacc = sample["zacc_mg"]
    zacc_ok = zacc is not None and GREEN_ZACC_ABS_MIN_MG <= abs(zacc) <= GREEN_ZACC_ABS_MAX_MG
    if counts["RAW_IMU"] < GREEN_RAW_IMU_MIN or not zacc_ok:
        return "RED", "imu_bad"

    if counts["ATTITUDE"] < GREEN_ATTITUDE_MIN:
        return "RED", "imu_bad"

    flags = sample["ekf_flags"]
    ekf_ok = flags is not None and (flags & GREEN_EKF_MASK) == GREEN_EKF_EXPECT
    if counts["EKF_STATUS_REPORT"] < 1 or not ekf_ok:
        return "RED", "no_ekf"

    baro = sample["baro_hpa"]
    baro_ok = baro is not None and GREEN_BARO_MIN_HPA <= baro <= GREEN_BARO_MAX_HPA
    if counts["SCALED_PRESSURE"] < GREEN_SCALED_PRESSURE_MIN or not baro_ok:
        return "RED", "baro_bad"

    return "GREEN", "ok"


def run_once(port: str, display_port: str, settle_max_s: float, sample_s: float, quiet: bool) -> dict[str, Any]:
    mavutil = load_mavutil()
    conn = None
    try:
        conn = mavutil.mavlink_connection(
            port,
            baud=115200,
            robust_parsing=True,
            source_system=251,
        )
        standby_ok, settle_seconds, _heartbeat = wait_standby(conn, settle_max_s)
        if not standby_ok:
            return {
                "verdict": "RED",
                "reason": "no_standby",
                "port": display_port,
                "settle_seconds": round(settle_seconds, 2),
                "sample_seconds": 0,
                "counts": {},
                "max_silence": None,
                "zacc_mg": None,
                "ekf_flags": None,
                "baro_hpa": None,
                "firmware_hint": "unknown",
            }

        drain(conn, 0.5)
        request_stabilizing_streams(conn, mavutil)
        drain(conn, 1.0)
        sample = sample_window(conn, sample_s)
        verdict, reason = evaluate(True, sample)
        fw_hint = firmware_hint(conn, mavutil)

        payload = {
            "verdict": verdict,
            "reason": reason,
            "port": display_port,
            "settle_seconds": round(settle_seconds, 2),
            "sample_seconds": sample_s,
            "counts": sample["counts"],
            "max_silence": sample["max_silence"],
            "zacc_mg": sample["zacc_mg"],
            "ekf_flags": f"0x{sample['ekf_flags']:x}" if sample["ekf_flags"] is not None else None,
            "baro_hpa": round(sample["baro_hpa"], 2) if sample["baro_hpa"] is not None else None,
            "firmware_hint": fw_hint,
        }
        log(f"{verdict} reason={reason} counts={payload['counts']}", quiet)
        return payload
    except RuntimeError as exc:
        return {
            "verdict": "RED",
            "reason": "no_standby",
            "port": display_port,
            "settle_seconds": 0,
            "sample_seconds": 0,
            "counts": {},
            "max_silence": None,
            "zacc_mg": None,
            "ekf_flags": None,
            "baro_hpa": None,
            "firmware_hint": "unknown",
            "error": str(exc),
        }
    finally:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass


def coefficient_of_variation(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    mean = statistics.mean(values)
    if mean == 0:
        return 0.0
    return statistics.stdev(values) / mean


def meta_summary(runs: list[dict[str, Any]]) -> dict[str, Any]:
    verdicts = [run["verdict"] for run in runs]
    reasons = [run.get("reason", "") for run in runs]
    settles = [float(run.get("settle_seconds", 0.0)) for run in runs]
    settle_std = statistics.stdev(settles) if len(settles) > 1 else 0.0

    count_cv: dict[str, float] = {}
    for key in ("HEARTBEAT", "RAW_IMU", "ATTITUDE", "EKF_STATUS_REPORT", "SCALED_PRESSURE"):
        values = [float(run.get("counts", {}).get(key, 0)) for run in runs]
        count_cv[key] = round(coefficient_of_variation(values), 4)

    fixture_stable = (
        len(set(verdicts)) == 1
        and len(set(reasons)) == 1
        and settle_std < 5.0
        and all(value < 0.15 for value in count_cv.values())
    )

    return {
        "runs": len(runs),
        "verdicts": verdicts,
        "reasons": reasons,
        "settle_std": round(settle_std, 3),
        "count_cv": count_cv,
        "fixture_stable": fixture_stable,
    }


def write_result(payload: dict[str, Any]) -> str:
    os.makedirs(output_dir(), exist_ok=True)
    path = os.path.join(output_dir(), f"{utc_stamp()}.json")
    with open(path, "w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    return path


def print_json_if_requested(payload: dict[str, Any], enabled: bool) -> None:
    if enabled:
        print(json.dumps(payload, indent=2, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description="AP_HAL_RTT deterministic fast GREEN/RED CDC gate")
    parser.add_argument("--port", default="auto", help="CDC port path, or 'auto'")
    parser.add_argument("--settle-max", type=float, default=DEFAULT_SETTLE_MAX_S, dest="settle_max")
    parser.add_argument("--sample", type=float, default=DEFAULT_SAMPLE_S)
    parser.add_argument("--json", action="store_true", help="Print JSON result to stdout")
    parser.add_argument("--meta-runs", type=int, default=0, dest="meta_runs")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    lock_fd = os.open(LOCK_PATH, os.O_CREAT | os.O_RDWR, 0o644)
    try:
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            payload = {
                "verdict": "FIXTURE_DIRTY",
                "reason": "dirty_lock",
                "timestamp_utc": iso_now(),
            }
            payload["json_path"] = write_result(payload)
            print_json_if_requested(payload, args.json)
            return 3

        dirty, reason, details = check_fixture_dirty()
        if dirty:
            payload = {
                "verdict": "FIXTURE_DIRTY",
                "reason": reason,
                "port": None,
                "details": details,
                "timestamp_utc": iso_now(),
            }
            payload["json_path"] = write_result(payload)
            log(f"FIXTURE_DIRTY reason={reason}", args.quiet)
            print_json_if_requested(payload, args.json)
            return 3

        port, display_port = resolve_port(args.port)
        if port is None or display_port is None:
            payload = {
                "verdict": "RED",
                "reason": "no_port",
                "port": args.port,
                "timestamp_utc": iso_now(),
            }
            payload["json_path"] = write_result(payload)
            print_json_if_requested(payload, args.json)
            return 2

        resolved_real_port = realpath_or_self(port)
        dirty, reason, details = check_fixture_dirty(resolved_real_port)
        if dirty:
            payload = {
                "verdict": "FIXTURE_DIRTY",
                "reason": reason,
                "port": display_port,
                "details": details,
                "timestamp_utc": iso_now(),
            }
            payload["json_path"] = write_result(payload)
            log(f"FIXTURE_DIRTY reason={reason}", args.quiet)
            print_json_if_requested(payload, args.json)
            return 3

        runs = max(1, args.meta_runs) if args.meta_runs > 0 else 1
        run_results: list[dict[str, Any]] = []
        for index in range(runs):
            if index > 0:
                time.sleep(META_RUN_GAP_S)
                dirty, reason, details = check_fixture_dirty(resolved_real_port)
                if dirty:
                    payload = {
                        "verdict": "FIXTURE_DIRTY",
                        "reason": reason,
                        "port": display_port,
                        "details": details,
                        "meta_run": index + 1,
                        "timestamp_utc": iso_now(),
                    }
                    payload["json_path"] = write_result(payload)
                    print_json_if_requested(payload, args.json)
                    return 3

            log(f"fast_green_gate run {index + 1}/{runs} port={display_port}", args.quiet)
            result = run_once(resolved_real_port, display_port, args.settle_max, args.sample, args.quiet)
            result["run_index"] = index + 1
            result["timestamp_utc"] = iso_now()
            run_results.append(result)

        if runs == 1:
            payload = dict(run_results[0])
            payload.pop("run_index", None)
        else:
            summary = meta_summary(run_results)
            last = run_results[-1]
            payload = {
                "verdict": last["verdict"],
                "reason": last["reason"],
                "port": display_port,
                "settle_seconds": last["settle_seconds"],
                "sample_seconds": args.sample,
                "counts": last["counts"],
                "max_silence": last["max_silence"],
                "zacc_mg": last["zacc_mg"],
                "ekf_flags": last["ekf_flags"],
                "baro_hpa": last["baro_hpa"],
                "firmware_hint": last["firmware_hint"],
                "meta_summary": summary,
                "runs_detail": run_results,
                "timestamp_utc": iso_now(),
            }

        payload["json_path"] = write_result(payload)
        print_json_if_requested(payload, args.json)

        if payload["verdict"] == "FIXTURE_DIRTY":
            return 3
        if runs > 1 and not payload["meta_summary"]["fixture_stable"]:
            return 2
        return 0 if payload["verdict"] == "GREEN" else 2
    finally:
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_UN)
        finally:
            os.close(lock_fd)


if __name__ == "__main__":
    raise SystemExit(main())
