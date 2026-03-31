#!/usr/bin/env python3
"""Dedicated SET_MESSAGE_INTERVAL regression."""

import argparse
import os
import time

from pymavlink import mavutil


DEFAULT_PORT = "/dev/ttyACM1"
CDC_SYMLINK_PATTERN = "usb-ArduPilot_CUAVv5_RTT_"


def find_rtt_cdc_port():
    by_id = "/dev/serial/by-id"
    if not os.path.isdir(by_id):
        return None
    for name in os.listdir(by_id):
        if CDC_SYMLINK_PATTERN in name:
            return os.path.join(by_id, name)
    return None


def connect(port: str, baud: int = 115200, retries: int = 6, timeout: int = 10):
    last_error = None
    for attempt in range(retries):
        conn = None
        try:
            port = find_rtt_cdc_port() or port
            conn = mavutil.mavlink_connection(port, baud=baud, source_system=245)
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
                time.sleep(2)
                continue
            raise RuntimeError(f"connect failed on {port}: {last_error}") from exc


def set_message_interval(conn, msg_id: int, interval_us: int):
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
    deadline = time.time() + 5
    while time.time() < deadline:
        ack = conn.recv_match(type="COMMAND_ACK", blocking=True, timeout=1)
        if ack and ack.command == mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL:
            return ack.result
    return None


def measure(conn, duration: float = 8.0):
    tracked = {"ATTITUDE": 0, "RAW_IMU": 0, "SYS_STATUS": 0, "HEARTBEAT": 0}
    start = time.time()
    while time.time() - start < duration:
        msg = conn.recv_match(blocking=True, timeout=1)
        if msg is None:
            continue
        mtype = msg.get_type()
        if mtype in tracked:
            tracked[mtype] += 1
    elapsed = time.time() - start
    return {name: count / elapsed for name, count in tracked.items()}


def main():
    parser = argparse.ArgumentParser(description="SET_MESSAGE_INTERVAL regression")
    parser.add_argument("--port", default=DEFAULT_PORT, help="MAVLink serial port")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    conn = connect(args.port, baud=args.baud)
    print("=== SET_MESSAGE_INTERVAL Regression ===")
    print(f"Port: {args.port}")
    print(f"Connected: sys={conn.target_system} comp={conn.target_component}")

    end = time.time() + 1
    while time.time() < end:
        conn.recv_match(blocking=False)

    results = {
        "ATTITUDE": set_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_ATTITUDE, 100000),
        "RAW_IMU": set_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_RAW_IMU, 100000),
        "SYS_STATUS": set_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_SYS_STATUS, 100000),
    }
    ok = True
    print("\n[ACK]")
    for name, result in results.items():
        passed = result == mavutil.mavlink.MAV_RESULT_ACCEPTED
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name:<12s} result={result}")
        ok = ok and passed

    rates = measure(conn)
    print("\n[RATES]")
    for name, threshold in {"ATTITUDE": 6.0, "RAW_IMU": 6.0, "SYS_STATUS": 6.0}.items():
        actual = rates.get(name, 0.0)
        passed = actual >= threshold
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name:<12s} >= {threshold:.1f} Hz (got {actual:.2f} Hz)")
        ok = ok and passed

    heartbeat = rates.get("HEARTBEAT", 0.0)
    hb_ok = 0.8 <= heartbeat <= 1.5
    print(f"  [{'PASS' if hb_ok else 'FAIL'}] HEARTBEAT    in [0.8, 1.5] Hz (got {heartbeat:.2f} Hz)")
    ok = ok and hb_ok

    conn.close()
    print(f"\nRESULT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
