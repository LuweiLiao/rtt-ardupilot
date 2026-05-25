#!/usr/bin/env python3
"""Quick MAVLink test on ACM ports"""
from pymavlink import mavutil
import sys

for port in ["/dev/ttyACM1", "/dev/ttyACM0"]:
    print(f"Trying {port}...")
    try:
        m = mavutil.mavlink_connection(port, baud=115200)
        msg = m.wait_heartbeat(timeout=5)
        if msg:
            print(f"HEARTBEAT on {port}!")
            print(f"  type={msg.type} state={msg.status}")
            sys.exit(0)
    except Exception as e:
        print(f"  Error: {e}")

print("No heartbeat on any port")
sys.exit(1)
