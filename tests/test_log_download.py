#!/usr/bin/env python3
"""Minimal LOG_REQUEST_LIST/DATA test for AP_HAL_RTT on CUAV v5."""

import argparse
import os
import sys
import time
from pathlib import Path

from pymavlink import mavutil


DEFAULT_PORT = os.environ.get("MAVLOG_PORT", "/dev/ttyACM1")
TARGET_COMPONENT = 1


def connect(port: str, baud: int = 115200, retries: int = 6, timeout: int = 10):
    last_error = None
    for attempt in range(retries):
        conn = None
        try:
            conn = mavutil.mavlink_connection(port, baud=baud, source_system=255)
            hb = conn.wait_heartbeat(timeout=timeout)
            if hb is None or conn.target_system == 0:
                raise RuntimeError("no valid heartbeat")
            return conn
        except Exception as exc:
            last_error = exc
            try:
                if conn is not None:
                    conn.close()
            except Exception:
                pass
            if attempt < retries - 1:
                time.sleep(3)
                continue
            raise RuntimeError(f"connect failed on {port}: {last_error}") from exc


def request_log_list(conn):
    conn.mav.log_request_list_send(conn.target_system, TARGET_COMPONENT, 0, 0xFFFF)
    logs = {}
    deadline = time.time() + 8
    while time.time() < deadline:
        msg = conn.recv_match(type="LOG_ENTRY", blocking=True, timeout=1)
        if msg is None:
            continue
        logs[msg.id] = msg
        if msg.id == msg.last_log_num:
            break
    return logs


def download_log(conn, log_id: int, log_size: int, out_path: Path, max_rounds: int = 8):
    """
    Download with stall recovery: if no LOG_DATA for a while, re-issue
    LOG_REQUEST_DATA from current offset (common on long USB CDC transfers).
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    stall_timeout = 4.0
    overall_deadline = time.time() + max(120, min(900, log_size // 20000 + 120))

    def request_from_offset(ofs: int) -> None:
        conn.mav.log_request_data_send(
            conn.target_system, TARGET_COMPONENT, log_id, ofs, 0xFFFFFFFF
        )

    request_from_offset(0)
    round_num = 0

    while len(data) < log_size and time.time() < overall_deadline and round_num < max_rounds:
        expected_ofs = len(data)
        last_progress = time.time()
        round_num += 1

        while len(data) < log_size and time.time() < overall_deadline:
            msg = conn.recv_match(type="LOG_DATA", blocking=True, timeout=2)
            if msg is None:
                if time.time() - last_progress > stall_timeout:
                    break
                continue
            if msg.id != log_id:
                continue
            if msg.ofs != expected_ofs:
                if msg.ofs < expected_ofs:
                    continue
                raise RuntimeError(
                    f"unexpected offset: want={expected_ofs} got={msg.ofs}"
                )
            if msg.count == 0:
                raise RuntimeError("received zero-length LOG_DATA chunk")
            remaining = log_size - expected_ofs
            # Some firmwares may send a final chunk with padding/extra bytes.
            # Only accept up to the remaining expected size.
            take = msg.count if msg.count <= remaining else remaining
            chunk = bytes(msg.data[:take])
            data.extend(chunk)
            expected_ofs = len(data)
            last_progress = time.time()

            if len(data) >= log_size:
                break

        if len(data) >= log_size:
            break
        if len(data) < log_size:
            print(
                f"  stall at {len(data)}/{log_size} bytes, re-request from offset {len(data)} (round {round_num})"
            )
            request_from_offset(len(data))
            time.sleep(0.2)

    if len(data) != log_size:
        raise RuntimeError(f"incomplete download: got {len(data)} / {log_size} bytes")

    out_path.write_bytes(data)
    return len(data)


def main():
    parser = argparse.ArgumentParser(description="Download latest dataflash log via LOG_REQUEST_*")
    parser.add_argument("--port", default=DEFAULT_PORT, help="MAVLink serial port")
    parser.add_argument("--baud", type=int, default=115200, help="serial baudrate")
    parser.add_argument("--out", default="build/test_logs", help="download directory")
    args = parser.parse_args()

    print("=== MAVLink Log Download Test ===")
    print(f"Port: {args.port}")
    conn = connect(args.port, baud=args.baud)
    print(f"Connected: sys={conn.target_system} comp={conn.target_component}")

    logs = request_log_list(conn)
    if not logs:
        print("FAIL: no LOG_ENTRY response")
        conn.close()
        return 1

    first = logs[min(logs)]
    if first.num_logs == 0:
        print("FAIL: board reports 0 logs")
        conn.close()
        return 1

    print(f"Found {len(logs)} logs (reported total={first.num_logs}, last={first.last_log_num})")
    latest_id = max(logs)
    latest = logs[latest_id]
    print(f"Downloading log id={latest_id} size={latest.size}")

    out_dir = Path(args.out)
    out_file = out_dir / f"log_{latest_id:08d}.bin"
    bytes_written = download_log(conn, latest_id, latest.size, out_file)
    conn.close()

    print(f"PASS: downloaded {bytes_written} bytes -> {out_file}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
