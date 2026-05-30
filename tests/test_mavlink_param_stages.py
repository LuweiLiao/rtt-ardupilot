#!/usr/bin/env python3
"""
Staged MAVLink USB CDC tests: isolate where link drops during param traffic.
Usage: python3 tests/test_mavlink_param_stages.py [--port /dev/ttyACM0] [--baud 57600]
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from collections import defaultdict

from pymavlink import mavutil


def dmesg_tail(n: int = 40) -> str:
    for cmd in (
        ["journalctl", "-k", "-n", str(n), "--no-pager"],
        ["sudo", "dmesg", "-T"],
        ["dmesg", "-T"],
    ):
        try:
            out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True, timeout=8)
            lines = out.strip().splitlines()
            return "\n".join(lines[-n:])
        except Exception:
            continue
    return "(kernel log unavailable)"


def usb_keywords_in_dmesg(since_lines: list[str]) -> list[str]:
    keys = (
        "disconnect", "reset", "new address", "ttyACM", "cdc_acm",
        "USB disconnect", "timeout", "stall", "error", "ArduPilot", "1209:5741",
    )
    hits = []
    for line in since_lines:
        low = line.lower()
        if any(k.lower() in low for k in keys):
            hits.append(line)
    return hits


def connect(port: str, baud: int, timeout: float) -> mavutil.mavfile:
    conn = mavutil.mavlink_connection(port, baud=baud)
    hb = conn.wait_heartbeat(timeout=timeout)
    if hb is None:
        raise RuntimeError("no heartbeat")
    conn.target_system = hb.get_srcSystem()
    conn.target_component = hb.get_srcComponent()
    return conn


def drain(conn: mavutil.mavfile, ms: int = 300) -> None:
    end = time.time() + ms / 1000.0
    while time.time() < end:
        conn.recv_match(blocking=False)


def stage_heartbeat(conn: mavutil.mavfile, seconds: int) -> dict:
    counts = defaultdict(int)
    t0 = time.time()
    last_hb = t0
    while time.time() - t0 < seconds:
        msg = conn.recv_match(blocking=True, timeout=2)
        if msg is None:
            continue
        counts[msg.get_type()] += 1
        if msg.get_type() == "HEARTBEAT":
            last_hb = time.time()
        if time.time() - last_hb > 5:
            return {
                "ok": False,
                "reason": "no HEARTBEAT for 5s",
                "counts": dict(counts),
                "elapsed": time.time() - t0,
            }
    return {"ok": True, "counts": dict(counts), "elapsed": time.time() - t0}


def stage_param_read_one(conn: mavutil.mavfile, name: str, timeout: float) -> dict:
    drain(conn, 200)
    t0 = time.time()
    conn.mav.param_request_read_send(
        conn.target_system,
        conn.target_component,
        name.encode("ascii") + b"\x00",
        -1,
    )
    got = None
    while time.time() - t0 < timeout:
        msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
        if msg is None:
            continue
        pid = msg.param_id
        if isinstance(pid, bytes):
            pid = pid.decode("ascii", "replace")
        pid = pid.strip("\x00")
        if pid == name:
            got = msg
            break
    if got is None:
        return {"ok": False, "reason": f"no PARAM_VALUE for {name}", "elapsed": time.time() - t0}
    return {
        "ok": True,
        "param_id": name,
        "value": got.param_value,
        "elapsed": time.time() - t0,
    }


def stage_param_list(conn: mavutil.mavfile, timeout: float) -> dict:
    drain(conn, 300)
    t0 = time.time()
    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    params: dict[str, float] = {}
    last_idx = -1
    stall_since = None
    while time.time() - t0 < timeout:
        msg = conn.recv_match(blocking=True, timeout=2)
        if msg is None:
            if params and stall_since is None:
                stall_since = time.time()
            if stall_since and time.time() - stall_since > 8:
                break
            continue
        stall_since = None
        if msg.get_type() == "HEARTBEAT":
            continue
        if msg.get_type() != "PARAM_VALUE":
            continue
        pid = msg.param_id
        if isinstance(pid, bytes):
            pid = pid.decode("ascii", "replace")
        pid = pid.strip("\x00")
        params[pid] = msg.param_value
        last_idx = msg.param_index
        if msg.param_count > 0 and msg.param_index >= msg.param_count - 1:
            break
    hb_ok = False
    try:
        hb = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=3)
        hb_ok = hb is not None
    except Exception:
        hb_ok = False
    return {
        "ok": len(params) >= 900 and hb_ok,
        "param_count": len(params),
        "last_index": last_idx,
        "post_hb": hb_ok,
        "elapsed": time.time() - t0,
        "reason": None if len(params) >= 900 else f"only {len(params)} params",
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--port",
        default="/dev/serial/by-id/usb-ArduPilot_CUAV_V5_2B0039000351383439353636-if00",
    )
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--hb-seconds", type=int, default=60)
    ap.add_argument("--param-timeout", type=float, default=60)
    args = ap.parse_args()

    print(f"port={args.port} baud={args.baud}")
    print("--- dmesg before ---")
    print(dmesg_tail(15))

    results = []
    conn = None
    try:
        print("\n=== S0: connect + heartbeat ===")
        conn = connect(args.port, args.baud, 15)
        print(f"  target sys={conn.target_system} comp={conn.target_component}")

        print(f"\n=== S1: heartbeat only {args.hb_seconds}s ===")
        r1 = stage_heartbeat(conn, args.hb_seconds)
        print(f"  ok={r1['ok']} elapsed={r1['elapsed']:.1f}s hb={r1['counts'].get('HEARTBEAT',0)}")
        if not r1["ok"]:
            print(f"  FAIL: {r1.get('reason')}")
            results.append(("S1_heartbeat", False))
        else:
            results.append(("S1_heartbeat", True))

        print("\n=== S2: PARAM_REQUEST_READ (SYSID_THISMAV) ===")
        r2 = stage_param_read_one(conn, "SYSID_THISMAV", 15)
        print(f"  ok={r2['ok']} elapsed={r2.get('elapsed',0):.2f}s {r2.get('reason','')}")
        if r2["ok"]:
            print(f"  value={r2['value']}")
        results.append(("S2_param_read_one", r2["ok"]))

        print("\n=== S3: PARAM_REQUEST_LIST (full download) ===")
        r3 = stage_param_list(conn, args.param_timeout)
        print(
            f"  ok={r3['ok']} count={r3['param_count']} elapsed={r3['elapsed']:.1f}s "
            f"post_hb={r3['post_hb']} {r3.get('reason') or ''}"
        )
        results.append(("S3_param_list", r3["ok"]))

    except Exception as e:
        print(f"\nEXCEPTION (likely disconnect): {e}")
        results.append(("exception", False))
    finally:
        if conn:
            try:
                conn.close()
            except Exception:
                pass

    print("\n--- dmesg after ---")
    tail = dmesg_tail(30)
    print(tail)
    hits = usb_keywords_in_dmesg(tail.splitlines())
    if hits:
        print("\n--- USB-related dmesg lines ---")
        for h in hits:
            print(h)

    print("\n=== SUMMARY ===")
    all_ok = True
    for name, ok in results:
        print(f"  {name}: {'PASS' if ok else 'FAIL'}")
        if not ok:
            all_ok = False
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
