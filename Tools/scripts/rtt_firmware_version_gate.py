#!/usr/bin/env python3
"""Verify that the live RTT board is running the expected git hash.

Hardware acceptance evidence is only useful if it is tied to the firmware that
was actually flashed.  This gate reads AUTOPILOT_VERSION over MAVLink and
compares flight_custom_version against either an explicit expected hash or the
current repository HEAD.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port

DEFAULT_PORT = MAVLINK_PORT


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def git_short_hash(source_root: Path, length: int = 8) -> str:
    proc = subprocess.run(
        ["git", "-C", str(source_root), "rev-parse", f"--short={length}", "HEAD"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"git_rev_parse_failed:{proc.stderr.strip()}")
    value = proc.stdout.strip().lower()
    if not value:
        raise RuntimeError("git_rev_parse_empty")
    return value[:length]


def decode_custom_version(values: Any) -> str:
    if values is None:
        return ""
    chars: list[str] = []
    for item in values:
        try:
            value = int(item)
        except (TypeError, ValueError):
            continue
        if value == 0:
            continue
        if 32 <= value <= 126:
            chars.append(chr(value))
    return "".join(chars).lower()


def load_mavutil() -> Any:
    try:
        from pymavlink import mavutil  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pymavlink_not_installed") from exc
    return mavutil


def request_autopilot_version(conn: Any, mavutil: Any, timeout_s: float) -> dict[str, Any] | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
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
        request_deadline = min(deadline, time.monotonic() + 2.0)
        while time.monotonic() < request_deadline:
            msg = conn.recv_match(blocking=True, timeout=0.5)
            if msg is None:
                continue
            if msg.get_type() == "AUTOPILOT_VERSION":
                return msg.to_dict()
    return None


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    source_root = Path(args.source_root).resolve()
    expected_hash = (args.expected_hash or git_short_hash(source_root)).lower()[:8]
    port = resolve_mavlink_port(args.port)
    mavutil = load_mavutil()
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "source_root": str(source_root),
        "expected_hash": expected_hash,
        "expected_source": "argument" if args.expected_hash else "git_head",
    }

    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=args.source_system,
    )
    try:
        hb = conn.wait_heartbeat(timeout=args.heartbeat_timeout)
        if hb is None or conn.target_system == 0:
            payload["verdict"] = "RED"
            payload["reason"] = "no_heartbeat"
            return payload
        payload["heartbeat"] = hb.to_dict()
        payload["target_system"] = int(conn.target_system)
        payload["target_component"] = int(conn.target_component)

        version = request_autopilot_version(conn, mavutil, args.version_timeout)
        if version is None:
            payload["verdict"] = "RED"
            payload["reason"] = "no_autopilot_version"
            return payload
        payload["autopilot_version"] = version
        actual_hash = decode_custom_version(version.get("flight_custom_version"))[:8]
        payload["actual_hash"] = actual_hash
        if actual_hash != expected_hash:
            payload["verdict"] = "RED"
            payload["reason"] = "firmware_hash_mismatch"
        else:
            payload["verdict"] = "GREEN"
            payload["reason"] = "firmware_hash_match"
        return payload
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--source-root", default=str(repo_root()))
    parser.add_argument("--expected-hash", default=None,
                        help="expected 8-character firmware git hash; defaults to git HEAD")
    parser.add_argument("--source-system", type=int, default=247)
    parser.add_argument("--heartbeat-timeout", type=float, default=15.0)
    parser.add_argument("--version-timeout", type=float, default=10.0)
    args = parser.parse_args()

    try:
        payload = run_gate(args)
    except Exception as exc:  # noqa: BLE001 - gate reports structured failure
        payload = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": str(exc),
            "port": args.port,
            "expected_hash": (args.expected_hash or "")[:8].lower(),
        }

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "firmware_version_gate.json"
    payload["json_path"] = str(json_path)
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
