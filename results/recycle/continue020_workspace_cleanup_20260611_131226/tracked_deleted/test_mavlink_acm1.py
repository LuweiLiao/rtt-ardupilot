#!/usr/bin/env python3
"""Test MAVLink on both ACM0 and ACM1"""
from pymavlink import mavutil
import sys

for port in ['/dev/ttyACM1', '/dev/ttyACM0']:
    print(f'[*] Trying {port}...')
    try:
        m = mavutil.mavlink_connection(port, baud=115200, source_system=255)
        msg = m.wait_heartbeat(timeout=5)
        if msg:
            print(f'  HEARTBEAT on {port}!')
            print(f'  System: {msg.get_srcSystem()}, Type: {msg.type}, State: {msg.status}')
            sys.exit(0)
        else:
            print(f'  No heartbeat on {port}')
    except Exception as e:
        print(f'  Error: {e}')

print('No heartbeat on any port')
sys.exit(1)
