#!/usr/bin/env python3
"""RTT SLCAN -> SocketCAN -> DroneCAN bench gate.

This script attaches the flight-controller SLCAN CDC port to a Linux
SocketCAN interface with slcand, sends standard/extended CAN frames with
cansend, captures traffic with candump, and can passively listen for DroneCAN
NodeStatus messages through the official ``dronecan`` Python package.

It intentionally separates host/SLCAN plumbing from physical-bus proof:
``cansend`` success alone is not enough for a GREEN verdict unless the caller
explicitly disables receive requirements.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import serial

from rtt_usb_port_select import SLCAN_PORT, resolve_slcan_port as select_slcan_port

DEFAULT_SLCAN_PORT = SLCAN_PORT


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_slcan_port(port_arg: str) -> str:
    return select_slcan_port(port_arg)


def sudo_wrap(argv: list[str], args: argparse.Namespace | None = None) -> list[str]:
    if args is not None and args.sudo_system_tools:
        return [args.sudo_binary] + argv
    return argv


def run_cmd(argv: list[str], *, timeout_s: float = 10.0,
            args: argparse.Namespace | None = None,
            check: bool = False) -> dict[str, Any]:
    actual_argv = sudo_wrap(argv, args)
    start = time.monotonic()
    try:
        proc = subprocess.run(
            actual_argv,
            check=check,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
        )
        return {
            "argv": actual_argv,
            "requested_argv": argv,
            "rc": proc.returncode,
            "elapsed_s": round(time.monotonic() - start, 3),
            "stdout": proc.stdout,
            "stderr": proc.stderr,
        }
    except Exception as exc:  # noqa: BLE001
        return {
            "argv": actual_argv,
            "requested_argv": argv,
            "rc": 127,
            "elapsed_s": round(time.monotonic() - start, 3),
            "stdout": "",
            "stderr": repr(exc),
        }


def tool_path(name: str) -> str | None:
    return shutil.which(name)


def require_tools() -> dict[str, Any]:
    tools = {name: tool_path(name) for name in ("slcand", "candump", "cansend", "ip")}
    missing = [name for name, path in tools.items() if path is None]
    dronecan_info: dict[str, Any] = {"available": False}
    try:
        import dronecan  # type: ignore
        dronecan_info = {
            "available": True,
            "version": str(getattr(dronecan, "__version__", "")),
        }
    except Exception as exc:  # noqa: BLE001
        dronecan_info = {
            "available": False,
            "error": repr(exc),
        }
    return {
        "tools": tools,
        "missing_tools": missing,
        "dronecan": dronecan_info,
        "ok": not missing and dronecan_info.get("available") is True,
    }


def interface_exists(iface: str, args: argparse.Namespace | None = None) -> bool:
    return run_cmd(["ip", "link", "show", iface], timeout_s=3.0, args=args)["rc"] == 0


def cleanup_interface(iface: str, args: argparse.Namespace | None = None) -> dict[str, Any]:
    actions: list[dict[str, Any]] = []
    if interface_exists(iface, args):
        actions.append(run_cmd(["ip", "link", "set", iface, "down"], timeout_s=5.0, args=args))
        actions.append(run_cmd(["ip", "link", "delete", iface], timeout_s=5.0, args=args))
    return {
        "iface": iface,
        "actions": actions,
        "exists_after": interface_exists(iface, args),
    }


def find_slcand_daemons(port: str, iface: str) -> list[dict[str, Any]]:
    proc = subprocess.run(
        ["ps", "-eo", "pid=,args="],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    matches: list[dict[str, Any]] = []
    for line in proc.stdout.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        parts = stripped.split(None, 1)
        if len(parts) != 2 or not parts[0].isdigit():
            continue
        pid = int(parts[0])
        argv = parts[1]
        argv_parts = argv.split()
        if not argv_parts:
            continue
        command_names = {os.path.basename(argv_parts[0])}
        if len(argv_parts) > 1 and command_names & {"sudo", "doas"}:
            command_names.add(os.path.basename(argv_parts[1]))
        if "slcand" not in command_names:
            continue
        if port in argv_parts or iface in argv_parts:
            matches.append({"pid": pid, "argv": argv})
    return matches


def stop_slcand_daemons(port: str, iface: str, args: argparse.Namespace) -> dict[str, Any]:
    before = find_slcand_daemons(port, iface)
    actions: list[dict[str, Any]] = []
    for proc in before:
        actions.append(run_cmd(["kill", str(proc["pid"])], timeout_s=3.0, args=args))
    if before:
        time.sleep(0.2)
    after = find_slcand_daemons(port, iface)
    if after:
        for proc in after:
            actions.append(run_cmd(["kill", "-9", str(proc["pid"])], timeout_s=3.0, args=args))
        time.sleep(0.2)
    return {
        "before": before,
        "actions": actions,
        "after": find_slcand_daemons(port, iface),
    }


def start_slcand(port: str, iface: str, args: argparse.Namespace) -> tuple[subprocess.Popen[str] | None, dict[str, Any]]:
    argv = [
        "slcand",
        "-o",
        f"-s{args.can_speed_code}",
        "-S",
        str(args.serial_baud),
        port,
        iface,
    ]
    actual_argv = sudo_wrap(argv, args)
    info: dict[str, Any] = {
        "argv": actual_argv,
        "requested_argv": argv,
        "port": port,
        "iface": iface,
    }
    try:
        proc = subprocess.Popen(
            actual_argv,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except Exception as exc:  # noqa: BLE001
        info.update({"started": False, "error": repr(exc)})
        return None, info

    time.sleep(args.attach_wait_s)
    info["pid"] = proc.pid
    info["poll"] = proc.poll()
    info["iface_exists"] = interface_exists(iface, args)
    if proc.poll() is not None:
        out, err = proc.communicate(timeout=1)
        info["stdout"] = out
        info["stderr"] = err
    return proc, info


def stop_process(proc: subprocess.Popen[str] | None) -> dict[str, Any]:
    if proc is None:
        return {"stopped": False, "reason": "not_started"}
    if proc.poll() is not None:
        return {"stopped": True, "rc": proc.returncode, "reason": "already_exited"}
    try:
        proc.terminate()
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)
    except Exception as exc:  # noqa: BLE001
        return {"stopped": False, "error": repr(exc)}
    return {"stopped": True, "rc": proc.returncode}


def close_board_slcan(port: str) -> dict[str, Any]:
    info: dict[str, Any] = {"port": port, "attempted": True}
    try:
        with serial.Serial(port, 115200, timeout=0.2, write_timeout=1) as ser:
            # Drain host-side buffered CAN ASCII first so the close command can
            # be observed in evidence without leaving stale bytes for the next
            # gate.  The device may still have more data queued; the important
            # action is sending C after slcand has released the TTY.
            drained = ser.read(8192)
            ser.write(b"C\r")
            ser.flush()
            time.sleep(0.2)
            reply = ser.read(4096)
        info.update({
            "drained_bytes": len(drained),
            "drained_prefix": drained[:120].decode("ascii", errors="replace"),
            "reply_ascii": reply.decode("ascii", errors="replace"),
            "reply_hex": reply.hex(),
            "closed": True,
        })
    except Exception as exc:  # noqa: BLE001
        info.update({"closed": False, "error": repr(exc)})
    return info


def set_iface_up(iface: str, args: argparse.Namespace) -> dict[str, Any]:
    return run_cmd(["ip", "link", "set", iface, "up"], timeout_s=5.0, args=args)


def candump_capture(iface: str, duration_s: float, out_path: Path) -> dict[str, Any]:
    argv = ["candump", "-L", iface]
    start = time.monotonic()
    proc = subprocess.Popen(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        out, err = proc.communicate(timeout=duration_s)
    except subprocess.TimeoutExpired:
        proc.send_signal(signal.SIGINT)
        try:
            out, err = proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate(timeout=2)
    out_path.write_text(out, encoding="utf-8")
    lines = [line for line in out.splitlines() if line.strip()]
    dronecan_like = classify_dronecan_like_lines(lines)
    return {
        "argv": argv,
        "rc": proc.returncode,
        "elapsed_s": round(time.monotonic() - start, 3),
        "stdout_path": str(out_path),
        "stderr": err,
        "line_count": len(lines),
        "first_lines": lines[:20],
        "dronecan_like": dronecan_like,
    }


def classify_dronecan_like_lines(lines: list[str]) -> dict[str, Any]:
    """Extract conservative DroneCAN v0 hints from candump -L output.

    DroneCAN/CAN v0 uses extended 29-bit identifiers. The source node id is in
    the low seven bits, so this is not a full protocol decoder; it is a fallback
    to prevent a python-can/dronecan backend issue from hiding that the bus is
    carrying extended DroneCAN-class traffic from the expected node.
    """
    frames: list[dict[str, Any]] = []
    for line in lines:
        parts = line.split()
        if len(parts) < 3 or "#" not in parts[2]:
            continue
        can_id_text, data_hex = parts[2].split("#", 1)
        try:
            can_id = int(can_id_text, 16)
        except ValueError:
            continue
        if can_id > 0x7FF:
            frames.append({
                "line": line,
                "can_id_hex": can_id_text.upper(),
                "source_node_id": can_id & 0x7F,
                "data_hex": data_hex.upper(),
            })
    node_ids = sorted({item["source_node_id"] for item in frames})
    return {
        "extended_frame_count": len(frames),
        "source_node_ids": node_ids,
        "first_frames": frames[:10],
    }


def cansend_frames(iface: str) -> dict[str, Any]:
    standard = run_cmd(["cansend", iface, "123#1122334455667788"], timeout_s=5.0)
    extended = run_cmd(["cansend", iface, "1F015508#1122334455667788"], timeout_s=5.0)
    return {
        "standard": standard,
        "extended": extended,
        "standard_ok": standard["rc"] == 0,
        "extended_ok": extended["rc"] == 0,
    }


def listen_dronecan(iface: str, args: argparse.Namespace) -> dict[str, Any]:
    dronecan_iface = iface
    result: dict[str, Any] = {
        "iface": iface,
        "dronecan_iface": dronecan_iface,
        "native_socketcan": args.dronecan_native_socketcan,
        "duration_s": args.dronecan_listen_s,
        "verdict": "RED",
        "events": [],
    }
    try:
        import dronecan  # type: ignore
    except Exception as exc:  # noqa: BLE001
        result["reason"] = f"dronecan_import_failed:{exc!r}"
        return result

    node = None
    try:
        if args.dronecan_native_socketcan:
            try:
                import dronecan.driver as dronecan_driver  # type: ignore
                dronecan_driver.PythonCAN = None
                result["driver_selection"] = "forced_native_socketcan"
            except Exception as exc:  # noqa: BLE001
                result["driver_selection_error"] = repr(exc)
        node = dronecan.make_node(dronecan_iface, node_id=args.dronecan_node_id, bitrate=args.bitrate)

        def handle_node_status(event: Any) -> None:
            msg = event.message
            transfer = event.transfer
            result["events"].append({
                "source_node_id": int(getattr(transfer, "source_node_id", -1)),
                "uptime_sec": int(getattr(msg, "uptime_sec", 0)),
                "health": int(getattr(msg, "health", -1)),
                "mode": int(getattr(msg, "mode", -1)),
                "sub_mode": int(getattr(msg, "sub_mode", -1)),
            })

        node.add_handler(dronecan.uavcan.protocol.NodeStatus, handle_node_status)
        deadline = time.monotonic() + args.dronecan_listen_s
        while time.monotonic() < deadline:
            node.spin(0.1)
        result["event_count"] = len(result["events"])
        result["verdict"] = "GREEN" if result["events"] else "RED"
        result["reason"] = "node_status_seen" if result["events"] else "node_status_missing"
    except Exception as exc:  # noqa: BLE001
        if result["events"]:
            result["event_count"] = len(result["events"])
            result["verdict"] = "GREEN"
            result["reason"] = "node_status_seen_before_exception"
            result["suppressed_exception"] = repr(exc)
            result["suppressed_traceback"] = traceback.format_exc()
        else:
            result["reason"] = repr(exc)
            result["traceback"] = traceback.format_exc()
    finally:
        if node is not None:
            try:
                node.close()
            except Exception as exc:  # noqa: BLE001 - some dronecan SocketCAN backends don't implement close()
                result["close_error"] = repr(exc)
    return result


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    port = resolve_slcan_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "iface": args.iface,
        "serial_baud": args.serial_baud,
        "can_speed_code": args.can_speed_code,
        "bitrate": args.bitrate,
        "sudo_system_tools": args.sudo_system_tools,
        "sudo_binary": args.sudo_binary if args.sudo_system_tools else None,
        "require_rx": args.require_rx,
        "require_dronecan": args.require_dronecan,
    }

    payload["requirements"] = require_tools()
    if payload["requirements"]["missing_tools"]:
        payload["verdict"] = "RED"
        payload["reason"] = "missing_tools"
        return payload
    if args.require_dronecan and not payload["requirements"]["dronecan"].get("available"):
        payload["verdict"] = "RED"
        payload["reason"] = "missing_dronecan_python_package"
        return payload
    if not os.path.exists(port):
        payload["verdict"] = "RED"
        payload["reason"] = f"slcan_port_missing:{port}"
        return payload

    if args.cleanup_existing:
        payload["slcand_daemon_cleanup_before"] = stop_slcand_daemons(port, args.iface, args)
        payload["cleanup_before"] = cleanup_interface(args.iface, args)
    else:
        payload["slcand_daemon_cleanup_before"] = None
        payload["cleanup_before"] = None
    slcand_proc: subprocess.Popen[str] | None = None
    try:
        slcand_proc, payload["slcand"] = start_slcand(port, args.iface, args)
        if not payload["slcand"].get("iface_exists"):
            payload["verdict"] = "RED"
            payload["reason"] = "slcand_interface_missing_after_attach"
            return payload
        payload["iface_up"] = set_iface_up(args.iface, args)
        if payload["iface_up"]["rc"] != 0:
            payload["verdict"] = "RED"
            payload["reason"] = "socketcan_interface_up_failed"
            return payload

        payload["candump_before"] = candump_capture(
            args.iface,
            args.pre_listen_s,
            outdir / "candump_before.log",
        ) if args.pre_listen_s > 0 else None
        payload["cansend"] = cansend_frames(args.iface)
        payload["candump_after"] = candump_capture(
            args.iface,
            args.listen_s,
            outdir / "candump_after.log",
        ) if args.listen_s > 0 else None
        payload["dronecan"] = listen_dronecan(args.iface, args) if args.dronecan_listen_s > 0 else None
    finally:
        payload["slcand_stop"] = stop_process(slcand_proc)
        if args.cleanup_after:
            payload["slcand_daemon_cleanup_after"] = stop_slcand_daemons(port, args.iface, args)
            payload["cleanup_after"] = cleanup_interface(args.iface, args)
        else:
            payload["slcand_daemon_cleanup_after"] = None
            payload["cleanup_after"] = None
        payload["board_slcan_close_after"] = close_board_slcan(port)

    sends_ok = bool(payload.get("cansend", {}).get("standard_ok")) and bool(payload.get("cansend", {}).get("extended_ok"))
    candump_lines = 0
    dronecan_like_frames = 0
    dronecan_like_node_ids: set[int] = set()
    for key in ("candump_before", "candump_after"):
        item = payload.get(key)
        if isinstance(item, dict):
            candump_lines += int(item.get("line_count") or 0)
            hint = item.get("dronecan_like")
            if isinstance(hint, dict):
                dronecan_like_frames += int(hint.get("extended_frame_count") or 0)
                for node_id in hint.get("source_node_ids") or []:
                    try:
                        dronecan_like_node_ids.add(int(node_id))
                    except (TypeError, ValueError):
                        pass
    rx_ok = (not args.require_rx) or candump_lines > 0
    dronecan_item = payload.get("dronecan")
    dronecan_library_ok = (
        isinstance(dronecan_item, dict) and dronecan_item.get("verdict") == "GREEN"
    )
    dronecan_fallback_ok = (
        dronecan_like_frames > 0 and
        (not args.dronecan_require_node_id or args.dronecan_required_source_node_id in dronecan_like_node_ids)
    )
    payload["dronecan_fallback"] = {
        "extended_frame_count": dronecan_like_frames,
        "source_node_ids": sorted(dronecan_like_node_ids),
        "required_source_node_id": args.dronecan_required_source_node_id if args.dronecan_require_node_id else None,
        "verdict": "GREEN" if dronecan_fallback_ok else "RED",
        "reason": "extended_dronecan_class_frames_seen" if dronecan_fallback_ok else "no_matching_extended_dronecan_class_frames",
    }
    dronecan_ok = (not args.require_dronecan) or dronecan_library_ok or dronecan_fallback_ok
    payload["verdict"] = "GREEN" if sends_ok and rx_ok and dronecan_ok else "RED"
    payload["reason"] = (
        "socketcan_slcan_dronecan_gate_ok" if payload["verdict"] == "GREEN"
        else f"sends_ok={sends_ok},rx_ok={rx_ok},dronecan_ok={dronecan_ok},candump_lines={candump_lines}"
    )
    return payload


def self_test() -> dict[str, Any]:
    req = require_tools()
    return {
        "timestamp_utc": iso_now(),
        "requirements": req,
        "verdict": "GREEN" if not req["missing_tools"] and req["dronecan"].get("available") else "RED",
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto", help="SLCAN CDC path or auto")
    parser.add_argument("--iface", default="can_rtt0", help="temporary SocketCAN interface name")
    parser.add_argument("--outdir", default="results/execution/rtt_socketcan_dronecan_gate")
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--can-speed-code", default="8", help="slcand -s code, 8 means 1Mbit/s")
    parser.add_argument("--bitrate", type=int, default=1000000, help="DroneCAN bitrate hint")
    parser.add_argument("--attach-wait-s", type=float, default=1.0)
    parser.add_argument("--pre-listen-s", type=float, default=0.0)
    parser.add_argument("--listen-s", type=float, default=5.0)
    parser.add_argument("--dronecan-listen-s", type=float, default=10.0)
    parser.add_argument("--dronecan-node-id", type=int, default=127)
    parser.add_argument("--dronecan-required-source-node-id", type=int, default=10)
    parser.add_argument("--dronecan-require-node-id", action="store_true", default=True,
                        help="fallback DroneCAN evidence must include the required source node id")
    parser.add_argument("--dronecan-any-node", dest="dronecan_require_node_id", action="store_false",
                        help="fallback DroneCAN evidence accepts any extended source node id")
    parser.add_argument("--dronecan-native-socketcan", action="store_true", default=True,
                        help="force dronecan's native SocketCAN driver via socketcan:<iface>")
    parser.add_argument("--dronecan-python-can", dest="dronecan_native_socketcan", action="store_false",
                        help="use dronecan's auto-selected PythonCAN path instead")
    parser.add_argument("--sudo-system-tools", action="store_true",
                        help="run slcand/ip through sudo while keeping this Python environment for dronecan")
    parser.add_argument("--sudo-binary", default="sudo")
    parser.add_argument("--require-rx", action="store_true",
                        help="require candump to observe at least one frame")
    parser.add_argument("--require-dronecan", action="store_true",
                        help="require DroneCAN NodeStatus during passive listen")
    parser.add_argument("--cleanup-existing", action="store_true", default=True)
    parser.add_argument("--no-cleanup-existing", dest="cleanup_existing", action="store_false")
    parser.add_argument("--cleanup-after", action="store_true", default=True)
    parser.add_argument("--no-cleanup-after", dest="cleanup_after", action="store_false")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        payload = self_test()
    else:
        payload = run_gate(args)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "socketcan_dronecan_gate.json"
    payload["json_path"] = str(json_path)
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
