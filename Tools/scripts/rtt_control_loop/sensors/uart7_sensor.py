#!/usr/bin/env python3
"""
UART7 (RT console / CH340) feedback sensor — parses RTT_CTL telemetry lines.
"""
from __future__ import annotations

import re
import time
from dataclasses import dataclass, asdict
from typing import Any, Dict, Optional

try:
    import serial
except ImportError:
    serial = None  # type: ignore

RTT_CTL_RE = re.compile(
    r"RTT_CTL\s+"
    r"t=(?P<t>\d+)\s+"
    r"hal=0x(?P<hal>[0-9a-fA-F]+)\s+"
    r"ent=0x(?P<ent>[0-9a-fA-F]+)\s+"
    r"iter=(?P<iter>\d+)\s+"
    r"usb=(?P<usb>\d+)\s+"
    r"stup=(?P<stup>\d+)\s+"
    r"usbrst=(?P<usbrst>\d+)\s+"
    r"enmd=(?P<enmd>\d+)\s+"
    r"cfg=(?P<cfg>[01])\s+"
    r"conn=(?P<conn>[01])\s+"
    r"loop_us=(?P<loop_us>\d+)\s+"
    r"loop_hz=(?P<loop_hz>\d+)\s+"
    r"ov=(?P<ov>\d+)\s+"
    r"stg=(?P<stg>\d+)\s+"
    r"hf=0x(?P<hf>[0-9a-fA-F]+)"
)


@dataclass
class UART7Sample:
    ok: bool
    port: str
    line: str
    fields: Dict[str, Any]
    error: str = ""
    bytes_received: int = 0
    boot_banner: bool = False

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


def _parse_line(line: str) -> Dict[str, Any]:
    m = RTT_CTL_RE.search(line)
    if not m:
        return {}
    g = m.groupdict()
    return {
        "t_ms": int(g["t"]),
        "hal": int(g["hal"], 16),
        "ent": int(g["ent"], 16),
        "iter": int(g["iter"]),
        "usb": int(g["usb"]),
        "stup": int(g["stup"]),
        "usbrst": int(g["usbrst"]),
        "enmd": int(g["enmd"]),
        "cfg": int(g["cfg"]),
        "conn": int(g["conn"]),
        "loop_us": int(g["loop_us"]),
        "loop_hz": int(g["loop_hz"]),
        "ov": int(g["ov"]),
        "stg": int(g["stg"]),
        "hf": int(g["hf"], 16),
    }


def sample(
    port: str = "/dev/ttyACM0",
    baud: int = 115200,
    timeout_s: float = 8.0,
) -> UART7Sample:
    if serial is None:
        return UART7Sample(False, port, "", {}, "pyserial not installed (pip install pyserial)")

    deadline = time.time() + timeout_s
    total_bytes = 0
    boot_banner = False
    last_err = ""
    ser: Optional["serial.Serial"] = None

    def _open() -> bool:
        nonlocal ser, last_err
        try:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
            ser = serial.Serial(port, baud, timeout=0.3)
            return True
        except serial.SerialException as e:
            last_err = str(e)
            ser = None
            return False

    if not _open():
        return UART7Sample(False, port, "", {}, last_err)

    try:
        buf = ""
        last_line = ""
        last_fields: Dict[str, Any] = {}
        while time.time() < deadline:
            try:
                chunk = ser.read(512)  # type: ignore[union-attr]
            except serial.SerialException as e:
                last_err = str(e)
                time.sleep(0.2)
                _open()
                continue
            if not chunk:
                continue
            total_bytes += len(chunk)
            buf += chunk.decode("utf-8", errors="ignore")
            if "[BOARD-INIT]" in buf:
                boot_banner = True
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if line.startswith("RTT_CTL"):
                    fields = _parse_line(line)
                    if fields:
                        last_line = line
                        last_fields = fields
        if last_fields:
            return UART7Sample(
                True, port, last_line, last_fields, "",
                bytes_received=total_bytes, boot_banner=boot_banner,
            )
        return UART7Sample(
            False, port, "", {},
            f"no RTT_CTL within {timeout_s}s (bytes={total_bytes}, boot_banner={boot_banner})",
            bytes_received=total_bytes, boot_banner=boot_banner,
        )
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass


if __name__ == "__main__":
    import argparse
    import json

    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--timeout", type=float, default=10.0)
    args = ap.parse_args()
    print(json.dumps(sample(args.port, timeout_s=args.timeout).to_dict(), indent=2))
