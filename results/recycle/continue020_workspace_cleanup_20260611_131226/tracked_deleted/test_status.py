#!/usr/bin/env python3
import sys
from pymavlink import mavutil
import time

PORT = '/dev/serial/by-id/usb-ArduPilot_CUAVv5_RTT_RTTUSB0001-if00'
m = mavutil.mavlink_connection(PORT, baud=57600)

hb = m.wait_heartbeat(timeout=8)
if not hb:
    print("No heartbeat!")
    sys.exit(1)

print(f"HB: type={hb.type} ap={hb.autopilot} base_mode=0x{hb.base_mode:02X} status={hb.system_status}")

start = time.time()
msgs_seen = set()
while time.time() - start < 8:
    msg = m.recv_match(blocking=True, timeout=2)
    if not msg:
        continue
    name = msg.get_type()
    
    if name == 'SYS_STATUS':
        v = msg.voltage_battery / 1000.0
        i = msg.current_battery / 100.0
        load = msg.load / 10.0
        present = msg.onboard_control_sensors_present
        health = msg.onboard_control_sensors_health
        print(f"SYS: load={load:.1f}% V={v:.3f}V I={i:.2f}A")
        print(f"  present=0x{present:08X} health=0x{health:08X}")
    elif name == 'STATUSTEXT':
        print(f"TEXT[{msg.severity}]: {msg.text}")
    elif name == 'RC_CHANNELS':
        chs = [getattr(msg, f'chan{i}_raw', 0) for i in range(1, min(msg.chancount+1, 9))]
        print(f"RC: count={msg.chancount} ch1-8={chs}")
    elif name == 'SERVO_OUTPUT_RAW':
        sv = [getattr(msg, f'servo{i}_raw', 0) for i in range(1, 5)]
        print(f"SERVO: 1-4={sv}")

elapsed = time.time() - start
print(f"\nDone in {elapsed:.1f}s")
