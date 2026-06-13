#!/usr/bin/env python3
"""AP_HAL_RTT no-stream heartbeat stability gate."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from typing import Any

from pymavlink import mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"


def close_conn(conn: Any | None) -> None:
    if conn is None:
        return
    try:
        conn.close()
    except Exception:
        pass


def wait_port(port: str, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(port):
            return True
        time.sleep(0.1)
    return False


def run_gate(args: argparse.Namespace) -> tuple[bool, dict[str, Any]]:
    start = time.monotonic()
    result: dict[str, Any] = {
        "port": args.port,
        "attempts": 0,
        "port_wait_s": None,
        "first_heartbeat_s": None,
        "duration_after_first_s": 0.0,
        "heartbeat_count": 0,
        "statuses": [],
        "last_status": None,
        "stable": False,
        "errors": [],
    }

    first = None
    conn = None
    boot_deadline = time.monotonic() + args.boot_timeout

    while time.monotonic() < boot_deadline and first is None:
        result["attempts"] += 1
        if not wait_port(args.port, args.port_wait):
            result["errors"].append("port_absent")
            continue

        if result["port_wait_s"] is None:
            result["port_wait_s"] = round(time.monotonic() - start, 3)

        try:
            conn = mavutil.mavlink_connection(
                args.port,
                baud=args.baud,
                autoreconnect=False,
            )
            attempt_deadline = time.monotonic() + args.first_heartbeat_timeout
            while time.monotonic() < attempt_deadline:
                if not os.path.exists(args.port):
                    raise RuntimeError("port_disappeared_before_heartbeat")

                msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
                now = time.monotonic()
                if msg is None:
                    continue

                first = now
                status = int(msg.system_status)
                result["first_heartbeat_s"] = round(now - start, 3)
                result["heartbeat_count"] += 1
                result["statuses"].append(status)
                result["last_status"] = status
                print(
                    "heartbeat %u elapsed=%.3fs status=%u"
                    % (result["heartbeat_count"], now - start, status),
                    flush=True,
                )
                break
        except Exception as exc:  # noqa: BLE001 - gate records transport failures verbatim
            result["errors"].append(repr(exc))
            close_conn(conn)
            conn = None
            time.sleep(args.retry_delay)

    if first is None or conn is None:
        result["errors"].append("no_heartbeat_after_retry_window")
        return False, result

    try:
        observe_until = first + args.observe
        while time.monotonic() < observe_until:
            if not os.path.exists(args.port):
                raise RuntimeError("CDC port disappeared during heartbeat observation")

            msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
            now = time.monotonic()
            if msg is None:
                continue

            status = int(msg.system_status)
            result["heartbeat_count"] += 1
            result["statuses"].append(status)
            result["last_status"] = status
            print(
                "heartbeat %u elapsed=%.3fs status=%u"
                % (result["heartbeat_count"], now - start, status),
                flush=True,
            )

        result["duration_after_first_s"] = round(time.monotonic() - first, 3)
        result["stable"] = (
            result["heartbeat_count"] >= args.min_heartbeats
            and result["duration_after_first_s"] >= args.min_duration
            and result["last_status"] in args.accept_status
        )
        return bool(result["stable"]), result
    except Exception as exc:  # noqa: BLE001 - gate records transport failures verbatim
        result["errors"].append(repr(exc))
        return False, result
    finally:
        close_conn(conn)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-timeout", type=float, default=60.0)
    parser.add_argument("--port-wait", type=float, default=5.0)
    parser.add_argument("--first-heartbeat-timeout", type=float, default=8.0)
    parser.add_argument("--retry-delay", type=float, default=1.0)
    parser.add_argument("--observe", type=float, default=50.0)
    parser.add_argument("--min-duration", type=float, default=45.0)
    parser.add_argument("--min-heartbeats", type=int, default=35)
    parser.add_argument("--accept-status", type=int, action="append", default=[3, 4, 5])
    parser.add_argument("--json", dest="json_path", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stable, result = run_gate(args)
    os.makedirs(os.path.dirname(os.path.abspath(args.json_path)), exist_ok=True)
    with open(args.json_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, sort_keys=True)
    print(json.dumps(result, sort_keys=True), flush=True)
    return 0 if stable else 1


if __name__ == "__main__":
    sys.exit(main())
