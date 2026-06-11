#!/usr/bin/env python3
"""Verify MAVLink HEARTBEAT - try both ACM0 and ACM1"""
from pymavlink import mavutil
import sys

for dev in ['/dev/ttyACM1', '/dev/ttyACM0']:
    print(f"Trying {dev}...", flush=True)
    try:
        master = mavutil.mavlink_connection(dev, baud=921600)
        msg = master.wait_heartbeat(timeout=10)
        if msg:
            print(f"HEARTBEAT on {dev}: type={msg.type} autopilot={msg.autopilot} status={msg.system_status}", flush=True)
            print("PASS", flush=True)
            sys.exit(0)
        else:
            print(f"  No HEARTBEAT on {dev}", flush=True)
    except Exception as e:
        print(f"  Error on {dev}: {e}", flush=True)

print("FAIL: No HEARTBEAT on any device", flush=True)
sys.exit(1)
