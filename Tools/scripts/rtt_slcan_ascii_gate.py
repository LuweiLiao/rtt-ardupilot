#!/usr/bin/env python3
"""Direct ASCII SLCAN gate for RTT USB SLCAN validation."""

from __future__ import annotations

import argparse
import glob
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import serial

from rtt_usb_port_select import SLCAN_PORT, resolve_slcan_port

DEFAULT_BOARD = SLCAN_PORT
DEFAULT_DEBUGGER = "/dev/serial/by-id/usb-STMicroelectronics_STM32_Virtual_ComPort_206E395C5446-if00"


def resolve_board_port(port_arg: str) -> str:
    return resolve_slcan_port(port_arg)


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def read_available(port: serial.Serial, wait_s: float = 0.25) -> bytes:
    deadline = time.monotonic() + wait_s
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        n = port.in_waiting
        if n:
            chunks.append(port.read(n))
            deadline = time.monotonic() + 0.05
        else:
            time.sleep(0.01)
    return b"".join(chunks)


def write_cmd(port: serial.Serial, cmd: str, wait_s: float = 0.25) -> dict[str, Any]:
    before = read_available(port, 0.05)
    port.write(cmd.encode("ascii") + b"\r")
    port.flush()
    reply = read_available(port, wait_s)
    return {
        "cmd": cmd,
        "pre_drain_hex": before.hex(),
        "reply_ascii": reply.decode("ascii", errors="replace"),
        "reply_hex": reply.hex(),
    }


def setup_slcan(port: serial.Serial, name: str) -> dict[str, Any]:
    results: dict[str, Any] = {"name": name, "commands": []}
    port.reset_input_buffer()
    port.reset_output_buffer()
    for cmd in ("C", "S8", "O", "F", "V", "N"):
        results["commands"].append(write_cmd(port, cmd))
    return results


def command_layer_ok(setup: dict[str, Any]) -> bool:
    replies = {cmd["cmd"]: cmd for cmd in setup.get("commands", [])}
    all_replies = "".join(cmd.get("reply_ascii", "") for cmd in setup.get("commands", []))
    for cmd in setup.get("commands", []):
        try:
            all_replies += bytes.fromhex(cmd.get("pre_drain_hex", "")).decode("ascii", errors="replace")
        except ValueError:
            pass
    version = replies.get("V", {})
    serial_number = replies.get("N", {})
    flags = replies.get("F", {})
    version_ok = bool(version.get("reply_hex")) or "V" in all_replies
    serial_ok = bool(serial_number.get("reply_hex")) or "N" in all_replies
    flags_ok = bool(flags.get("reply_hex")) or "F" in all_replies
    return version_ok and serial_ok and flags_ok


def send_and_listen(sender: serial.Serial, receiver: serial.Serial, frame: str,
                    listen_s: float) -> dict[str, Any]:
    receiver.reset_input_buffer()
    sender.reset_input_buffer()
    send_reply = write_cmd(sender, frame, 0.15)
    start = time.monotonic()
    chunks: list[bytes] = []
    while time.monotonic() - start < listen_s:
        data = read_available(receiver, 0.05)
        if data:
            chunks.append(data)
        time.sleep(0.01)
    received = b"".join(chunks)
    return {
        "frame": frame,
        "send_reply": send_reply,
        "listen_s": listen_s,
        "received_ascii": received.decode("ascii", errors="replace"),
        "received_hex": received.hex(),
        "received_contains_id": frame[1:4] in received.decode("ascii", errors="replace"),
    }


def passive_listen(port: serial.Serial, listen_s: float) -> dict[str, Any]:
    port.reset_input_buffer()
    start = time.monotonic()
    chunks: list[bytes] = []
    while time.monotonic() - start < listen_s:
        data = read_available(port, 0.05)
        if data:
            chunks.append(data)
        time.sleep(0.01)
    received = b"".join(chunks)
    ascii_text = received.decode("ascii", errors="replace")
    return {
        "listen_s": listen_s,
        "received_ascii": ascii_text,
        "received_hex": received.hex(),
        "standard_frame_count": ascii_text.count("t"),
        "extended_frame_count": ascii_text.count("T"),
        "frame_seen": ("t" in ascii_text) or ("T" in ascii_text),
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    board_port = resolve_board_port(args.board_port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "board_port": board_port,
        "debugger_port": args.debugger_port,
        "bitrate_code": "S8",
        "listen_s": args.listen_s,
        "board_only": args.board_only,
    }

    if not os.path.exists(board_port):
        raise RuntimeError(f"board_port_missing:{board_port}")
    if not os.path.exists(args.debugger_port):
        if args.board_only:
            with serial.Serial(board_port, args.baud, timeout=0, write_timeout=1) as board:
                time.sleep(0.2)
                payload["board_setup"] = setup_slcan(board, "board")
                if args.passive_listen_s > 0:
                    payload["passive_listen"] = passive_listen(board, args.passive_listen_s)
                payload["standard_send"] = write_cmd(board, "t12381122334455667788")
                payload["extended_send"] = write_cmd(board, "T1F0155081122334455667788")
                payload["board_close"] = write_cmd(board, "C")
            board_cmd_ok = command_layer_ok(payload["board_setup"])
            std_reply = payload["standard_send"].get("reply_ascii", "")
            ext_reply = payload["extended_send"].get("reply_ascii", "")
            std_ack = "\a" not in std_reply
            ext_ack = "\a" not in ext_reply
            passive_ok = (
                args.passive_listen_s <= 0 or
                bool(payload.get("passive_listen", {}).get("frame_seen"))
            )
            payload["verdict"] = "GREEN" if board_cmd_ok and std_ack and ext_ack and passive_ok else "RED"
            payload["reason"] = (
                "board_slcan_command_layer_ok" if payload["verdict"] == "GREEN" and args.passive_listen_s <= 0
                else "board_slcan_command_layer_and_passive_rx_ok" if payload["verdict"] == "GREEN"
                else f"board_cmd_ok={board_cmd_ok},std_ack={std_ack},ext_ack={ext_ack},passive_ok={passive_ok}"
            )
            return payload
        raise RuntimeError(f"debugger_port_missing:{args.debugger_port}")

    with serial.Serial(board_port, args.baud, timeout=0, write_timeout=1) as board, \
            serial.Serial(args.debugger_port, args.baud, timeout=0, write_timeout=1) as debugger:
        time.sleep(0.2)
        payload["board_setup"] = setup_slcan(board, "board")
        payload["debugger_setup"] = setup_slcan(debugger, "debugger")

        payload["board_to_debugger"] = []
        payload["debugger_to_board"] = []
        for idx in range(args.frames):
            payload["board_to_debugger"].append(
                send_and_listen(
                    board,
                    debugger,
                    f"t{0x123 + idx:03X}81122334455667788",
                    args.listen_s,
                )
            )
            payload["debugger_to_board"].append(
                send_and_listen(
                    debugger,
                    board,
                    f"t{0x321 + idx:03X}88877665544332211",
                    args.listen_s,
                )
            )

        payload["board_close"] = write_cmd(board, "C")
        payload["debugger_close"] = write_cmd(debugger, "C")

    board_rx = any(item["received_contains_id"] for item in payload["debugger_to_board"])
    debugger_rx = any(item["received_contains_id"] for item in payload["board_to_debugger"])
    board_cmd_ok = command_layer_ok(payload["board_setup"])
    payload["verdict"] = "GREEN" if board_cmd_ok and board_rx and debugger_rx else "RED"
    payload["reason"] = (
        "slcan_ascii_bidir_ok" if payload["verdict"] == "GREEN"
        else f"board_cmd_ok={board_cmd_ok},board_rx={board_rx},debugger_rx={debugger_rx}"
    )
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="RTT direct SLCAN ASCII gate")
    parser.add_argument("--board-port", "--port", default="auto")
    parser.add_argument("--debugger-port", default=DEFAULT_DEBUGGER)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--listen-s", type=float, default=1.0)
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--board-only", action="store_true",
                        help="validate only the flight-controller SLCAN ASCII command layer")
    parser.add_argument("--passive-listen-s", type=float, default=0.0,
                        help="with --board-only, also require passive CAN frame reception for this many seconds")
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    try:
        payload = run(args)
        rc = 0 if payload["verdict"] == "GREEN" else 2
    except Exception as exc:
        payload = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": str(exc),
            "board_port": args.board_port,
            "debugger_port": args.debugger_port,
        }
        rc = 2
    json_path = outdir / "slcan_ascii_gate.json"
    payload["json_path"] = str(json_path)
    with json_path.open("w", encoding="utf-8") as fp:
        json.dump(payload, fp, indent=2, sort_keys=True)
        fp.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
