#!/usr/bin/env python3
"""Capture a serial port to a file for RTT board diagnostics."""

from __future__ import annotations

import argparse
import os
import sys
import time

import serial


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", required=True)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--wait-port", type=float, default=10.0)
    return parser.parse_args()


def wait_port(path: str, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.1)
    return False


def main() -> int:
    args = parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    with open(args.output, "wb") as out:
        if not wait_port(args.port, args.wait_port):
            out.write(b"PORT_TIMEOUT\n")
            return 1

        end = time.monotonic() + args.duration
        try:
            with serial.Serial(args.port, args.baud, timeout=0.2) as dev:
                while time.monotonic() < end:
                    data = dev.read(4096)
                    if data:
                        out.write(data)
                        out.flush()
        except Exception as exc:  # noqa: BLE001 - diagnostic capture records raw transport state
            out.write(("SERIAL_ERROR %r\n" % (exc,)).encode("utf-8", "replace"))
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
