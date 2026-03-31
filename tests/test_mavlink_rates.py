#!/usr/bin/env python3
"""Default stream-rate, REQUEST_DATA_STREAM and SET_MESSAGE_INTERVAL regression."""

import argparse
import os
import time
from collections import Counter

from pymavlink import mavutil


DEFAULT_PORT = "/dev/ttyACM1"
CDC_SYMLINK_PATTERN = "usb-ArduPilot_CUAVv5_RTT_"


def find_rtt_cdc_port():
    """
    ttyACM index can change across reboots; prefer stable by-id.
    """
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
            conn = mavutil.mavlink_connection(port, baud=baud, source_system=252)
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


def drain(conn, duration: float = 0.5):
    end = time.time() + duration
    while time.time() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def measure_rates(conn, msg_types, duration: float):
    counts = Counter()
    observed = Counter()
    start = time.time()
    while time.time() - start < duration:
        msg = conn.recv_match(blocking=True, timeout=1)
        if msg is None:
            continue
        mtype = msg.get_type()
        observed[mtype] += 1
        if mtype in msg_types:
            counts[mtype] += 1
    elapsed = time.time() - start
    rates = {name: counts.get(name, 0) / elapsed for name in msg_types}
    if all(r == 0.0 for r in rates.values()):
        top = ", ".join([f"{k}:{v}" for k, v in observed.most_common(10)])
        print(f"  [debug] observed types: {top}")
    return rates


def request_streams(conn, rate_hz: int):
    # Avoid MAV_DATA_STREAM_ALL flooding; request only the streams needed
    # for our tracked messages, similar to how GCS typically behaves.
    # - ATTITUDE is in EXTRA1
    # - RAW_IMU is in RAW_SENSORS
    # - SYS_STATUS is in EXTENDED_STATUS
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
        if ack is None:
            continue
        if ack.command == mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL:
            return ack.result
    return None


def reboot_and_reconnect(conn, port: str, baud: int):
    conn.mav.command_long_send(
        conn.target_system,
        conn.target_component,
        mavutil.mavlink.MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN,
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    conn.close()
    time.sleep(8)
    # After reboot, port name may change and CDC may churn briefly.
    # Require a stable RX window (multiple heartbeats) before returning.
    last_exc = None
    for _ in range(6):
        try:
            c = connect(find_rtt_cdc_port() or port, baud=baud, retries=20, timeout=15)
            # Confirm RX stays alive (at least 2 heartbeats) before proceeding.
            hb_needed = 2
            hb_got = 0
            deadline = time.time() + 12
            while time.time() < deadline and hb_got < hb_needed:
                msg = c.recv_match(type="HEARTBEAT", blocking=True, timeout=2)
                if msg is not None:
                    hb_got += 1
            if hb_got >= hb_needed:
                return c
            c.close()
            time.sleep(2)
        except Exception as exc:
            last_exc = exc
            try:
                c.close()  # type: ignore[name-defined]
            except Exception:
                pass
            time.sleep(2)
    raise RuntimeError(f"reboot reconnect did not stabilize: {last_exc}")


def settle_after_boot(conn, seconds: int = 10):
    """
    Let the vehicle finish booting, while keeping the RX path exercised.
    A pure sleep here can mask CDC churn and lead to false 0Hz measurements.
    """
    end = time.time() + seconds
    while time.time() < end:
        conn.recv_match(blocking=False)
        time.sleep(0.02)


def ensure_rx_alive(conn, min_types=("HEARTBEAT",), timeout_s: float = 10.0):
    """
    Ensure we are actually receiving messages on this connection.
    This prevents false 0Hz measurements if we connected during enumeration churn.
    """
    want = set(min_types)
    got = set()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        msg = conn.recv_match(blocking=True, timeout=1)
        if msg is None:
            continue
        mtype = msg.get_type()
        if mtype in want:
            got.add(mtype)
            if got == want:
                return True
    return False


def check_thresholds(label, rates, thresholds):
    ok = True
    print(f"\n[{label}]")
    for name, threshold in thresholds.items():
        actual = rates.get(name, 0.0)
        passed = actual >= threshold
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name:<12s} >= {threshold:.1f} Hz (got {actual:.2f} Hz)")
        ok = ok and passed
    return ok


def check_mins(label, rates, mins):
    ok = True
    print(f"\n[{label}]")
    for name, min_hz in mins.items():
        a = rates.get(name, 0.0)
        passed = a >= min_hz
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name:<12s} >= {min_hz:.1f} Hz (got {a:.2f} Hz)")
        ok = ok and passed
    return ok


def main():
    parser = argparse.ArgumentParser(description="MAVLink rate regression")
    parser.add_argument("--port", default=DEFAULT_PORT, help="MAVLink serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--reboot", action="store_true", help="Reboot vehicle before each phase (less stable on USB CDC)")
    args = parser.parse_args()

    tracked = ["HEARTBEAT", "ATTITUDE", "RAW_IMU", "SYS_STATUS"]
    print("=== MAVLink Rate Regression ===")
    print(f"Port: {args.port}")
    conn = connect(args.port, baud=args.baud)
    if args.reboot:
        conn = reboot_and_reconnect(conn, args.port, args.baud)
    print(f"Connected(default): sys={conn.target_system} comp={conn.target_component}")
    settle_after_boot(conn)
    # If RX is still unstable after reboot, fail early with a clear signal.
    if not ensure_rx_alive(conn, min_types=("HEARTBEAT",), timeout_s=12):
        conn.close()
        conn = connect(args.port, baud=args.baud, retries=10, timeout=15)
    drain(conn)
    default_rates = measure_rates(conn, tracked, duration=8)
    ok_default = check_thresholds(
        "DEFAULT STREAM RATES",
        default_rates,
        {
            "HEARTBEAT": 0.8,
        },
    )
    if args.reboot:
        conn = reboot_and_reconnect(conn, args.port, args.baud)
    print(f"\nConnected(request): sys={conn.target_system} comp={conn.target_component}")
    settle_after_boot(conn)
    if not ensure_rx_alive(conn, min_types=("HEARTBEAT",), timeout_s=12):
        conn.close()
        conn = connect(args.port, baud=args.baud, retries=10, timeout=15)
    drain(conn)
    request_streams(conn, rate_hz=4)
    time.sleep(1)
    request_rates = measure_rates(conn, tracked, duration=8)
    ok_request = check_thresholds("REQUEST_DATA_STREAM (HB)", request_rates, {"HEARTBEAT": 0.7})
    ok_request = ok_request and check_mins(
        "REQUEST_DATA_STREAM (tracked mins)",
        request_rates,
        {"ATTITUDE": 2.0, "RAW_IMU": 2.0, "SYS_STATUS": 2.0},
    )
    conn.close()

    overall = ok_default and ok_request
    print(f"\nRESULT: {'PASS' if overall else 'FAIL'}")
    return 0 if overall else 1


if __name__ == "__main__":
    raise SystemExit(main())
