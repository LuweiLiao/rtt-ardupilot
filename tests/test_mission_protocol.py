#!/usr/bin/env python3
"""Basic MAVLink mission upload/download/clear smoke test."""

import argparse
import time

from pymavlink import mavutil


DEFAULT_PORT = "/dev/ttyACM1"


def connect(port: str, baud: int = 115200, retries: int = 6, timeout: int = 10):
    last_error = None
    for attempt in range(retries):
        conn = None
        try:
            conn = mavutil.mavlink_connection(port, baud=baud, source_system=251)
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


def recv_match_any(conn, types, timeout=5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = conn.recv_match(type=types, blocking=True, timeout=1)
        if msg is not None:
            return msg
    return None


def send_mission_clear_all(conn):
    try:
        conn.mav.mission_clear_all_send(conn.target_system, conn.target_component, 0)
    except TypeError:
        conn.mav.mission_clear_all_send(conn.target_system, conn.target_component)


def send_mission_count(conn, count):
    try:
        conn.mav.mission_count_send(conn.target_system, conn.target_component, count, 0)
    except TypeError:
        conn.mav.mission_count_send(conn.target_system, conn.target_component, count)


def send_mission_request_list(conn):
    try:
        conn.mav.mission_request_list_send(conn.target_system, conn.target_component, 0)
    except TypeError:
        conn.mav.mission_request_list_send(conn.target_system, conn.target_component)


def send_mission_request_int(conn, seq):
    try:
        conn.mav.mission_request_int_send(conn.target_system, conn.target_component, seq, 0)
    except TypeError:
        conn.mav.mission_request_int_send(conn.target_system, conn.target_component, seq)


def mission_item_int(conn, seq, current, lat, lon, alt):
    try:
        conn.mav.mission_item_int_send(
            conn.target_system,
            conn.target_component,
            seq,
            mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT_INT,
            mavutil.mavlink.MAV_CMD_NAV_WAYPOINT,
            current,
            1,
            0,
            0,
            0,
            0,
            lat,
            lon,
            alt,
            0,
        )
    except TypeError:
        conn.mav.mission_item_int_send(
            conn.target_system,
            conn.target_component,
            seq,
            mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT_INT,
            mavutil.mavlink.MAV_CMD_NAV_WAYPOINT,
            current,
            1,
            0,
            0,
            0,
            0,
            lat,
            lon,
            alt,
        )


def mission_item(conn, seq, current, lat, lon, alt):
    try:
        conn.mav.mission_item_send(
            conn.target_system,
            conn.target_component,
            seq,
            mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
            mavutil.mavlink.MAV_CMD_NAV_WAYPOINT,
            current,
            1,
            0,
            0,
            0,
            0,
            lat / 1e7,
            lon / 1e7,
            alt,
            0,
        )
    except TypeError:
        conn.mav.mission_item_send(
            conn.target_system,
            conn.target_component,
            seq,
            mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
            mavutil.mavlink.MAV_CMD_NAV_WAYPOINT,
            current,
            1,
            0,
            0,
            0,
            0,
            lat / 1e7,
            lon / 1e7,
            alt,
        )


def print_check(label, passed, detail=""):
    status = "PASS" if passed else "FAIL"
    suffix = f" ({detail})" if detail else ""
    print(f"  [{status}] {label}{suffix}")
    return passed


def main():
    parser = argparse.ArgumentParser(description="Mission protocol smoke test")
    parser.add_argument("--port", default=DEFAULT_PORT, help="MAVLink serial port")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    conn = connect(args.port, baud=args.baud)
    print("=== Mission Protocol Smoke Test ===")
    print(f"Port: {args.port}")
    print(f"Connected: sys={conn.target_system} comp={conn.target_component}")

    base_lat = 374221234
    base_lon = -1220845678
    items = [
        {"seq": 0, "current": 1, "lat": base_lat, "lon": base_lon, "alt": 20.0},
        {"seq": 1, "current": 0, "lat": base_lat + 1000, "lon": base_lon + 1000, "alt": 25.0},
    ]

    ok = True

    send_mission_clear_all(conn)
    ack = recv_match_any(conn, ["MISSION_ACK"], timeout=5)
    print_check("MISSION_CLEAR_ALL baseline", ack is not None, getattr(ack, "type", None))

    send_mission_count(conn, len(items))
    print_check("MISSION_COUNT sent", True, len(items))

    requested = set()
    deadline = time.time() + 15
    while len(requested) < len(items) and time.time() < deadline:
        msg = recv_match_any(conn, ["MISSION_REQUEST_INT", "MISSION_REQUEST"], timeout=2)
        if msg is None:
            continue
        seq = msg.seq
        requested.add(seq)
        item = items[seq]
        if msg.get_type() == "MISSION_REQUEST_INT":
            mission_item_int(conn, item["seq"], item["current"], item["lat"], item["lon"], item["alt"])
        else:
            mission_item(conn, item["seq"], item["current"], item["lat"], item["lon"], item["alt"])
    ok &= print_check("MISSION_REQUEST loop", requested == {0, 1}, sorted(requested))

    ack = recv_match_any(conn, ["MISSION_ACK"], timeout=8)
    ok &= print_check(
        "MISSION_ACK after upload",
        ack is not None and ack.type == mavutil.mavlink.MAV_MISSION_ACCEPTED,
        getattr(ack, "type", None),
    )

    send_mission_request_list(conn)
    count_msg = recv_match_any(conn, ["MISSION_COUNT"], timeout=5)
    ok &= print_check(
        "MISSION_COUNT after upload",
        count_msg is not None and count_msg.count == len(items),
        getattr(count_msg, "count", None),
    )

    downloaded = []
    for seq in range(len(items)):
        send_mission_request_int(conn, seq)
        item_msg = recv_match_any(conn, ["MISSION_ITEM_INT", "MISSION_ITEM"], timeout=5)
        if item_msg is not None:
            downloaded.append((item_msg.seq, item_msg.command))
    ok &= print_check(
        "MISSION_ITEM download",
        len(downloaded) == len(items) and [seq for seq, _ in downloaded] == [0, 1],
        downloaded,
    )

    send_mission_clear_all(conn)
    ack = recv_match_any(conn, ["MISSION_ACK"], timeout=5)
    ok &= print_check("MISSION_CLEAR_ALL final ACK", ack is not None, getattr(ack, "type", None))

    send_mission_request_list(conn)
    count_msg = recv_match_any(conn, ["MISSION_COUNT"], timeout=5)
    ok &= print_check(
        "MISSION_COUNT after clear",
        count_msg is not None and count_msg.count == 0,
        getattr(count_msg, "count", None),
    )

    conn.close()
    print(f"\nRESULT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
