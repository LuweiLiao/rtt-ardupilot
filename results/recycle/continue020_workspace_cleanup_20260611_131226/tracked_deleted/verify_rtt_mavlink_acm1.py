#!/usr/bin/env python3
"""
RTT 飞控 MAVLink 通信验证 — /dev/ttyACM1（实际 MAVLink 端口）
同时也尝试 ACM0
"""
from pymavlink import mavutil
import time
import sys

def try_connection(port_name, timeout_hb=5):
    """尝试连接并验证"""
    print(f"\n{'='*60}")
    print(f"尝试连接: {port_name} @ 115200")
    print(f"{'='*60}")
    sys.stdout.flush()

    try:
        master = mavutil.mavlink_connection(port_name, baud=115200, source_system=255)
    except Exception as e:
        print(f"❌ 连接失败: {e}")
        return None

    print(f"[*] 等待 HEARTBEAT（最多 {timeout_hb} 秒）...")
    sys.stdout.flush()
    msg = master.wait_heartbeat(timeout=timeout_hb)

    if not msg:
        print(f"❌ 未收到 HEARTBEAT")
        master.close()
        return None

    autopilot_map = {0: 'Generic', 3: 'ArduPilot', 4: 'PX4', 12: 'AP_Periph'}
    type_map = {0: 'Generic', 1: 'Fixed-wing', 2: 'Multirotor', 3: 'Helicopter',
                4: 'Ground Rover', 5: 'Boat', 6: 'Submarine', 7: 'Antenna',
                8: 'Gimbal', 9: 'ADSB', 10: 'GCS'}
    state_map = {0: 'Uninit', 1: 'Booting', 2: 'Calibrating', 3: 'Standby',
                 4: 'Active', 5: 'Critical', 6: 'Emergency', 7: 'PowerOff', 8: 'FlightTermination'}

    hb = {
        'system': msg.get_srcSystem(),
        'component': msg.get_srcComponent(),
        'type': type_map.get(msg.type, f'Unknown({msg.type})'),
        'autopilot': autopilot_map.get(msg.autopilot, f'Unknown({msg.autopilot})'),
        'state': state_map.get(msg.status, f'Unknown({msg.status})'),
        'mode': msg.custom_mode,
    }
    print(f"✅ HEARTBEAT 接收成功")
    print(f"   System: {hb['system']}, Component: {hb['component']}")
    print(f"   Type: {hb['type']}")
    print(f"   Autopilot: {hb['autopilot']}")
    print(f"   State: {hb['state']}")
    print(f"   Mode: {hb['mode']}")

    return master

def collect_sensor_data(master, label, max_time=25, max_msgs=60):
    """请求数据流并收集传感器数据"""
    captured = {k: None for k in ['RAW_IMU','SCALED_PRESSURE','SCALED_PRESSURE2',
                                    'ATTITUDE','SYS_STATUS','GPS_RAW_INT']}
    captured['HEARTBEAT_count'] = 0

    print(f"\n[*] {label}: 请求 MAV_DATA_STREAM_ALL @ 10 Hz...")
    master.mav.request_data_stream_send(
        master.target_system, master.target_component,
        mavutil.mavlink.MAV_DATA_STREAM_ALL, 10, 1
    )
    time.sleep(1)

    print(f"[*] 开始采集传感器数据（最多 {max_time} 秒 / {max_msgs} 条）...")
    sys.stdout.flush()

    time_start = time.time()
    msg_count = 0
    seen_types = set()

    while time.time() - time_start < max_time and msg_count < max_msgs:
        msg = master.recv_match(blocking=True, timeout=3)
        if msg is None:
            continue
        msg_count += 1
        t = msg.get_type()
        seen_types.add(t)

        if t == 'RAW_IMU' and captured['RAW_IMU'] is None:
            captured['RAW_IMU'] = {
                'xacc': msg.xacc, 'yacc': msg.yacc, 'zacc': msg.zacc,
                'xgyro': msg.xgyro, 'ygyro': msg.ygyro, 'zgyro': msg.zgyro,
                'xmag': msg.xmag, 'ymag': msg.ymag, 'zmag': msg.zmag,
            }
            print(f"  📡 RAW_IMU: accel=({msg.xacc},{msg.yacc},{msg.zacc}) "
                  f"gyro=({msg.xgyro},{msg.ygyro},{msg.zgyro})")

        elif t == 'SCALED_PRESSURE' and captured['SCALED_PRESSURE'] is None:
            captured['SCALED_PRESSURE'] = {
                'press_abs': msg.press_abs, 'press_diff': msg.press_diff,
                'temperature': msg.temperature,
            }
            print(f"  🌡️  SCALED_PRESSURE: press_abs={msg.press_abs:.2f} hPa "
                  f"temp={msg.temperature/100:.1f}°C")

        elif t == 'SCALED_PRESSURE2' and captured['SCALED_PRESSURE2'] is None:
            captured['SCALED_PRESSURE2'] = {
                'press_abs': msg.press_abs, 'temperature': msg.temperature,
            }
            print(f"  🌡️  SCALED_PRESSURE2: press_abs={msg.press_abs:.2f} hPa "
                  f"temp={msg.temperature/100:.1f}°C")

        elif t == 'ATTITUDE' and captured['ATTITUDE'] is None:
            captured['ATTITUDE'] = {
                'roll': msg.roll*180/3.14159, 'pitch': msg.pitch*180/3.14159,
                'yaw': msg.yaw*180/3.14159,
            }
            print(f"  🧭 ATTITUDE: roll={msg.roll*180/3.14159:.1f}° "
                  f"pitch={msg.pitch*180/3.14159:.1f}° yaw={msg.yaw*180/3.14159:.1f}°")

        elif t == 'SYS_STATUS' and captured['SYS_STATUS'] is None:
            captured['SYS_STATUS'] = {
                'voltage': msg.voltage_battery/1000.0,
                'current': msg.current_battery/100.0 if msg.current_battery>=0 else 0,
                'remaining': msg.battery_remaining,
            }
            print(f"  🔋 SYS_STATUS: {msg.voltage_battery/1000:.2f}V "
                  f"{msg.current_battery/100:.2f}A {msg.battery_remaining}%")

        elif t == 'GPS_RAW_INT' and captured['GPS_RAW_INT'] is None:
            fix_map = {0:'No GPS',1:'No Fix',2:'2D Fix',3:'3D Fix',4:'DGPS',5:'RTK Float',6:'RTK Fixed'}
            captured['GPS_RAW_INT'] = {
                'lat': msg.lat/1e7, 'lon': msg.lon/1e7, 'alt': msg.alt/1000.0,
                'fix': fix_map.get(msg.fix_type, f'Unknown({msg.fix_type})'),
                'sat': msg.satellites_visible,
            }
            print(f"  🛰️  GPS: {msg.lat/1e7:.6f},{msg.lon/1e7:.6f} "
                  f"alt={msg.alt/1000:.1f}m fix={captured['GPS_RAW_INT']['fix']}")

        elif t == 'HEARTBEAT':
            captured['HEARTBEAT_count'] += 1

        if all(v is not None for k,v in captured.items() if k != 'HEARTBEAT_count'):
            print(f"\n[✓] 所有目标数据已捕获（{msg_count} 条消息）")
            break

    elapsed = time.time() - time_start
    print(f"\n⏱️  用时: {elapsed:.1f}s | 总消息: {msg_count} | 类型: {sorted(seen_types)}")

    return captured


# ====== MAIN ======
# First try ACM0 (as requested)
master0 = try_connection('/dev/ttyACM0', timeout_hb=8)
if master0:
    data0 = collect_sensor_data(master0, "ACM0")
    master0.close()
else:
    data0 = None
    print("⚠️  ACM0: 无 MAVLink 数据")

# Then try ACM1 (where raw data was observed)
master1 = try_connection('/dev/ttyACM1', timeout_hb=5)
if master1:
    data1 = collect_sensor_data(master1, "ACM1")
    master1.close()
else:
    data1 = None

# ====== 最终报告 ======
print("\n\n" + "=" * 60)
print("📋 最终验证报告")
print("=" * 60)

for port, master, data in [('ACM0', master0, data0), ('ACM1', master1, data1)]:
    if data is None:
        print(f"\n❌ {port}: 无 MAVLink 通信")
        continue

    print(f"\n✅ {port}: MAVLink 通信验证通过")
    collected = sum(1 for k,v in data.items() if v is not None and k != 'HEARTBEAT_count')
    total = sum(1 for k in data.keys() if k != 'HEARTBEAT_count')
    print(f"   数据采集率: {collected}/{total}")

    if data.get('RAW_IMU'):
        imu = data['RAW_IMU']
        print(f"   RAW_IMU: accel=({imu['xacc']:.2f},{imu['yacc']:.2f},{imu['zacc']:.2f}) "
              f"gyro=({imu['xgyro']:.2f},{imu['ygyro']:.2f},{imu['zgyro']:.2f})")

    if data.get('SCALED_PRESSURE'):
        p = data['SCALED_PRESSURE']
        print(f"   Baro: {p['press_abs']:.1f} hPa @ {p['temperature']/100:.1f}°C")

    if data.get('ATTITUDE'):
        a = data['ATTITUDE']
        print(f"   Attitude: roll={a['roll']:.1f}° pitch={a['pitch']:.1f}° yaw={a['yaw']:.1f}°")

    if data.get('SYS_STATUS'):
        s = data['SYS_STATUS']
        print(f"   Battery: {s['voltage']:.2f}V {s['current']:.2f}A {s['remaining']}%")

    if data.get('GPS_RAW_INT'):
        g = data['GPS_RAW_INT']
        print(f"   GPS: {g['lat']:.4f},{g['lon']:.4f} alt={g['alt']:.1f}m {g['fix']} sat={g['sat']}")

print("\n" + "=" * 60)
if data1 and all(v is not None for k,v in data1.items() if k != 'HEARTBEAT_count'):
    print("✅ RTT 飞控 MAVLink 通信完全验证通过！")
elif data1:
    print("✅ MAVLink 心跳已确认，部分传感器数据可用")
else:
    print("❌ 飞控无 MAVLink 响应，需进一步排查")
print("=" * 60)
