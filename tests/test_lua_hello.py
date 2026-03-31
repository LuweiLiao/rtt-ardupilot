#!/usr/bin/env python3
"""Upload hello_world.lua, enable scripting, reboot, and wait for STATUSTEXT."""

import argparse
import errno
import os
import struct
import sys
import time
from pathlib import Path

from pymavlink import mavutil

sys.path.insert(0, os.path.dirname(__file__))
from test_mavftp import MavFTP, FTP_OP_Ack, FTP_OP_Nack  # noqa: E402


DEFAULT_PORT = os.environ.get("MAVFTP_PORT", "/dev/ttyACM1")
HELLO_SCRIPT = Path("libraries/AP_Scripting/examples/hello_world.lua")
SCRIPT_DIR = "/APM/scripts"
CDC_SYMLINK_PATTERN = "usb-ArduPilot_CUAVv5_RTT_"


def find_rtt_cdc_port():
    """
    After reboot, ttyACM index can change.
    Use /dev/serial/by-id for a stable identifier.
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


def ensure_dir(ftp: MavFTP, path: str):
    resp = ftp.create_dir(path)
    if resp is None:
        raise RuntimeError(f"timeout creating dir {path}")
    if resp["opcode"] == FTP_OP_Ack:
        return
    if resp["opcode"] == FTP_OP_Nack and resp["data"]:
        if resp["data"][0] in (8, 9):
            return
        if resp["data"][0] == 2 and len(resp["data"]) >= 2:
            err = resp["data"][1]
            if err >= 128:
                err -= 256
            if abs(err) == errno.EEXIST:
                return
    raise RuntimeError(f"create_dir {path} failed: {resp}")


def upload_file(ftp: MavFTP, remote_path: str, data: bytes):
    resp = ftp.remove_file(remote_path)
    if resp is not None and resp["opcode"] not in (FTP_OP_Ack, FTP_OP_Nack):
        raise RuntimeError(f"unexpected remove response: {resp}")

    resp = ftp.create_file(remote_path)
    if resp is None or resp["opcode"] != FTP_OP_Ack:
        raise RuntimeError(f"create_file failed: {resp}")
    session = resp["session"]

    offset = 0
    chunk_size = 200
    while offset < len(data):
        chunk = data[offset:offset + chunk_size]
        resp = ftp.write_file(session, offset, chunk)
        if resp is None or resp["opcode"] != FTP_OP_Ack:
            raise RuntimeError(f"write_file failed at {offset}: {resp}")
        offset += len(chunk)

    ftp.term_session(session)


def set_param(conn, name: str, value: float):
    conn.mav.param_set_send(conn.target_system, conn.target_component,
                            name.encode("ascii"), value,
                            mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
    deadline = time.time() + 8
    while time.time() < deadline:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
        if msg is None:
            continue
        pid = msg.param_id.decode(errors="ignore").rstrip("\x00") if isinstance(msg.param_id, bytes) else str(msg.param_id).rstrip("\x00")
        if pid == name:
            return msg.param_value
    raise RuntimeError(f"timeout setting {name}")


def wait_statustext(conn, text: str, timeout: int = 20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = conn.recv_match(type="STATUSTEXT", blocking=True, timeout=1)
        if msg is None:
            continue
        payload = msg.text
        if isinstance(payload, bytes):
            payload = payload.decode(errors="replace")
        if text in payload:
            return payload
    return None


def main():
    parser = argparse.ArgumentParser(description="Minimal Lua hello-world validation")
    parser.add_argument("--port", default=DEFAULT_PORT, help="MAVLink serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--script", default=str(HELLO_SCRIPT), help="lua source file to upload")
    args = parser.parse_args()

    script_data = Path(args.script).read_bytes()
    print("=== Lua Hello Test ===")
    print(f"Port: {args.port}")

    port = args.port
    stable_port = find_rtt_cdc_port()
    if stable_port:
        port = stable_port

    conn = connect(port, baud=args.baud)
    print(f"Connected: sys={conn.target_system} comp={conn.target_component}")
    ftp = MavFTP(conn)
    ensure_dir(ftp, "/APM")
    ensure_dir(ftp, SCRIPT_DIR)
    upload_file(ftp, f"{SCRIPT_DIR}/hello_world.lua", script_data)
    print(f"Uploaded {SCRIPT_DIR}/hello_world.lua")

    val = set_param(conn, "SCR_ENABLE", 1.0)
    print(f"SCR_ENABLE={val}")

    # Lua 启动后发送 STATUSTEXT 可能存在较强启动时序抖动。
    # 为了让“两小时不停机自测”阶段 B 尽量一次通过，这里做最多 2 次重启+等待收口。
    conn.close()

    hello_timeout_s = 90
    for reboot_try in (1, 2):
        port = find_rtt_cdc_port() or args.port
        conn = connect(port, baud=args.baud, retries=10, timeout=15)
        print(f"Reboot attempt {reboot_try}/2: sys={conn.target_system} comp={conn.target_component}")
        conn.mav.command_long_send(
            conn.target_system,
            conn.target_component,
            mavutil.mavlink.MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN,
            0, 1, 0, 0, 0, 0, 0, 0,
        )
        conn.close()
        print("Reboot requested, waiting for reconnect...")
        time.sleep(8)

        port = find_rtt_cdc_port() or args.port
        conn = connect(port, baud=args.baud, retries=10, timeout=15)
        print(f"Reconnected: sys={conn.target_system} comp={conn.target_component}")

        # 启动窗口稍加裕量，避免“刚枚举/刚连上还没开始运行 Lua”的竞态。
        time.sleep(3)
        text = wait_statustext(conn, "hello, world", timeout=hello_timeout_s)
        conn.close()

        if text is not None:
            print(f"PASS: received STATUSTEXT: {text}")
            return 0

        print(f"FAIL: attempt {reboot_try}/2 no Lua hello statustext within {hello_timeout_s}s")

    print("FAIL: did not receive Lua hello statustext after 2 reboot attempts")
    return 1


if __name__ == "__main__":
    sys.exit(main())
