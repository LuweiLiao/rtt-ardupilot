#!/usr/bin/env python3
"""RTT MAVLink DataFlash log list/download gate.

This gate uses normal ArduPilot MAVLink and AP_Logger paths:

1. request LOG_ENTRY via LOG_REQUEST_LIST;
2. if no log exists, temporarily set LOG_DISARMED=1 to make AP_Logger create
   a file-backed DataFlash log;
3. restore the original LOG_DISARMED value;
4. download one non-empty log via LOG_REQUEST_DATA.

No HAL-above firmware behavior is patched by this script.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pymavlink import mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"


class GateError(RuntimeError):
    def __init__(self, reason: str, payload: dict[str, Any]):
        super().__init__(reason)
        self.payload = payload


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


def connect(port: str, source_system: int = 246, timeout_s: float = 25.0) -> Any:
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
    last_error = None
    for _ in range(attempts):
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
        if msg is not None and param_name(msg) == name:
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


def read_param(conn: Any, name: str, timeout_s: float = 8.0) -> float:
    try:
        return request_param_direct(conn, name, timeout_s=timeout_s)
    except Exception:
        return request_param_from_list(conn, name)


def set_param(conn: Any, name: str, value: float, timeout_s: float = 10.0) -> float:
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
        if msg is not None and param_name(msg) == name:
            return float(msg.param_value)
    raise RuntimeError(f"param_set_timeout:{name}")


def unique_components(conn: Any) -> list[int]:
    components = [int(conn.target_component), 1, 0, 191]
    result: list[int] = []
    for component in components:
        if component not in result:
            result.append(component)
    return result


def request_log_list_once(conn: Any, component: int, timeout_s: float) -> dict[str, Any]:
    drain(conn, 0.2)
    conn.mav.log_request_list_send(conn.target_system, component, 0, 0xFFFF)
    deadline = time.monotonic() + timeout_s
    entries: dict[int, dict[str, int]] = {}
    statustext: list[str] = []
    last_entry_time = time.monotonic()

    while time.monotonic() < deadline:
        msg = conn.recv_match(blocking=True, timeout=1.0)
        if msg is None:
            if entries and time.monotonic() - last_entry_time > 1.5:
                break
            continue
        mtype = msg.get_type()
        if mtype == "LOG_ENTRY":
            entries[int(msg.id)] = {
                "id": int(msg.id),
                "num_logs": int(msg.num_logs),
                "last_log_num": int(msg.last_log_num),
                "time_utc": int(msg.time_utc),
                "size": int(msg.size),
            }
            last_entry_time = time.monotonic()
            if int(msg.num_logs) > 0 and int(msg.id) == int(msg.last_log_num):
                break
            if int(msg.num_logs) == 0:
                break
        elif mtype == "STATUSTEXT":
            text = msg.text if isinstance(msg.text, str) else msg.text.decode(errors="ignore")
            statustext.append(text)

    return {
        "component": component,
        "entries": sorted(entries.values(), key=lambda item: item["id"]),
        "statustext": statustext,
    }


def request_log_list(conn: Any, timeout_s: float = 12.0) -> dict[str, Any]:
    requests = [
        request_log_list_once(conn, component, timeout_s)
        for component in unique_components(conn)
    ]
    by_id: dict[int, dict[str, int]] = {}
    for request in requests:
        for entry in request["entries"]:
            by_id[int(entry["id"])] = entry
    entries = sorted(by_id.values(), key=lambda item: item["id"])
    nonzero = [entry for entry in entries if entry["size"] > 0 and entry["num_logs"] > 0]
    return {
        "requests": requests,
        "entries": entries,
        "nonzero_entries": nonzero,
    }


def wait_for_nonzero_log(conn: Any, timeout_s: float, poll_gap_s: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_s
    last_listing: dict[str, Any] | None = None
    while time.monotonic() < deadline:
        last_listing = request_log_list(conn)
        if last_listing["nonzero_entries"]:
            return last_listing
        time.sleep(poll_gap_s)
    raise RuntimeError(
        "no_nonzero_log_after_create:" + json.dumps(last_listing, sort_keys=True))


def cleanup_openocd() -> None:
    subprocess.run(["pkill", "-9", "-x", "openocd"], check=False)
    subprocess.run(["pkill", "-9", "-f", "openoccd"], check=False)


def openocd_reset(log_path: Path, timeout_s: int = 60) -> int:
    with log_path.open("w", encoding="utf-8") as log:
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
    return int(proc.returncode)


def wait_cdc(port: str, timeout_s: float = 35.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(port):
            return
        time.sleep(1.0)
    raise RuntimeError(f"cdc_not_back:{port}")


def download_log(conn: Any, log_id: int, log_size: int, out_path: Path,
                 max_rounds: int = 8) -> int:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    stall_timeout = 4.0
    overall_deadline = time.monotonic() + max(120, min(900, log_size // 20000 + 120))

    def request_from_offset(offset: int) -> None:
        conn.mav.log_request_data_send(
            conn.target_system,
            conn.target_component,
            log_id,
            offset,
            0xFFFFFFFF,
        )

    request_from_offset(0)
    rounds = 0
    while len(data) < log_size and time.monotonic() < overall_deadline and rounds < max_rounds:
        expected_offset = len(data)
        last_progress = time.monotonic()
        rounds += 1

        while len(data) < log_size and time.monotonic() < overall_deadline:
            msg = conn.recv_match(type="LOG_DATA", blocking=True, timeout=2.0)
            if msg is None:
                if time.monotonic() - last_progress > stall_timeout:
                    break
                continue
            if int(msg.id) != log_id:
                continue
            if int(msg.ofs) != expected_offset:
                if int(msg.ofs) < expected_offset:
                    continue
                raise RuntimeError(f"unexpected_log_offset:{expected_offset}!={int(msg.ofs)}")
            if int(msg.count) <= 0:
                raise RuntimeError("zero_length_log_data")

            remaining = log_size - expected_offset
            take = min(int(msg.count), remaining)
            data.extend(bytes(msg.data[:take]))
            expected_offset = len(data)
            last_progress = time.monotonic()

        if len(data) < log_size:
            request_from_offset(len(data))
            time.sleep(0.2)

    try:
        conn.mav.log_request_end_send(conn.target_system, conn.target_component)
    except Exception:
        pass

    if len(data) != log_size:
        raise RuntimeError(f"incomplete_download:{len(data)}!={log_size}")
    out_path.write_bytes(data)
    return len(data)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as infile:
        for block in iter(lambda: infile.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def pick_log(entries: list[dict[str, int]], max_size: int) -> dict[str, int]:
    candidates = [entry for entry in entries if 0 < entry["size"] <= max_size]
    if not candidates:
        candidates = [entry for entry in entries if entry["size"] > 0]
    if not candidates:
        raise RuntimeError("no_downloadable_log")
    return min(candidates, key=lambda entry: entry["size"])


def try_restore(port: str, original: float, save_wait: float) -> dict[str, Any]:
    result: dict[str, Any] = {"original": original}
    try:
        conn = connect_retry(port, source_system=245)
        try:
            result["set_restore"] = set_param(conn, "LOG_DISARMED", original)
            time.sleep(save_wait)
            result["readback"] = read_param(conn, "LOG_DISARMED")
        finally:
            conn.close()
    except Exception as exc:
        result["error"] = str(exc)
    return result


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    download_dir = outdir / "downloads"
    download_dir.mkdir(parents=True, exist_ok=True)

    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "steps": [],
    }

    changed_log_disarmed = False
    conn = connect(port)
    try:
        payload["target_system"] = int(conn.target_system)
        payload["target_component"] = int(conn.target_component)

        original = read_param(conn, "LOG_DISARMED")
        backend = read_param(conn, "LOG_BACKEND_TYPE")
        payload["params"] = {
            "LOG_DISARMED_original": original,
            "LOG_BACKEND_TYPE": backend,
        }

        initial_list = request_log_list(conn)
        payload["initial_log_list"] = initial_list

        final_list = initial_list
        if not initial_list["nonzero_entries"]:
            created_value = set_param(conn, "LOG_DISARMED", 1.0)
            changed_log_disarmed = True
            payload["steps"].append({"set_LOG_DISARMED_create": created_value})
            time.sleep(args.create_warmup)
            create_list = wait_for_nonzero_log(
                conn,
                timeout_s=args.create_timeout,
                poll_gap_s=args.poll_gap,
            )
            payload["created_log_list"] = create_list

            if abs(original - 1.0) > args.tolerance:
                restored = set_param(conn, "LOG_DISARMED", original)
                payload["steps"].append({"set_LOG_DISARMED_restore_before_download": restored})
                time.sleep(args.restore_wait)
                readback = read_param(conn, "LOG_DISARMED")
                payload["steps"].append({"LOG_DISARMED_restore_readback": readback})
                changed_log_disarmed = False

            final_list = request_log_list(conn)
            payload["post_restore_log_list"] = final_list

        selected = pick_log(final_list["nonzero_entries"], args.max_download_size)
        payload["selected_log"] = selected

        out_file = download_dir / f"log_{selected['id']:08d}.bin"
        bytes_written = download_log(conn, selected["id"], selected["size"], out_file)
        payload["download"] = {
            "path": str(out_file),
            "bytes": bytes_written,
            "sha256": sha256_file(out_file),
        }
    except Exception:
        if changed_log_disarmed and "params" in payload:
            payload["best_effort_restore"] = try_restore(
                port,
                float(payload["params"]["LOG_DISARMED_original"]),
                args.restore_wait,
            )
        payload["verdict"] = "RED"
        payload["reason"] = str(sys.exc_info()[1])
        raise GateError(payload["reason"], payload)
    finally:
        conn.close()

    if args.reset_after_restore:
        reset_log = outdir / "reset_after_restore.log"
        reset_rc = openocd_reset(reset_log)
        payload["steps"].append({"reset_after_restore_rc": reset_rc, "log": str(reset_log)})
        if reset_rc != 0:
            payload["verdict"] = "RED"
            payload["reason"] = f"reset_after_restore_failed:{reset_rc}"
            raise GateError(payload["reason"], payload)
        wait_cdc(port)
        conn2 = connect_retry(port, source_system=244)
        try:
            restored_after_reset = read_param(conn2, "LOG_DISARMED")
            payload["params"]["LOG_DISARMED_after_reset"] = restored_after_reset
            original = float(payload["params"]["LOG_DISARMED_original"])
            if abs(restored_after_reset - original) > args.tolerance:
                raise RuntimeError(f"restore_after_reset_mismatch:{restored_after_reset}!={original}")
        finally:
            conn2.close()

    payload["verdict"] = "GREEN"
    payload["reason"] = "log_list_download_restore_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="RTT DataFlash log download gate")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--create-warmup", type=float, default=8.0)
    parser.add_argument("--create-timeout", type=float, default=60.0)
    parser.add_argument("--poll-gap", type=float, default=2.0)
    parser.add_argument("--restore-wait", type=float, default=5.0)
    parser.add_argument("--max-download-size", type=int, default=1_000_000)
    parser.add_argument("--tolerance", type=float, default=0.01)
    parser.add_argument("--reset-after-restore", action="store_true")
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

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "log_download_gate.json"
    payload["json_path"] = str(json_path)
    with json_path.open("w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return rc


if __name__ == "__main__":
    sys.exit(main())
