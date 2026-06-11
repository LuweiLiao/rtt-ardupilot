#!/usr/bin/env python3
"""
RTT 飞控 MAVLink 通信完整验证
连接 /dev/ttyACM0 @ 115200 baud, MAVLink 2.0
"""
from pymavlink import mavutil
import time
import sys

# ===== 1. 连接串口 =====
print("=" * 60)
print("RTT 飞控 MAVLink 通信验证")
print("=" * 60)
print(f"[*] 连接 /dev/ttyACM0 @ 115200 baud...")
sys.stdout.flush()

master = mavutil.mavlink_connection('/dev/ttyACM0', baud=115200, source_system=255)

# ===== 2. 等待 HEARTBEAT =====
print("[*] 等待 HEARTBEAT（最多 5 秒）...")
sys.stdout.flush()
msg = master.wait_heartbeat(timeout=5)

results = {}

if msg:
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
    results['HEARTBEAT'] = hb
    print(f"✅ HEARTBEAT 接收成功")
    print(f"   System: {hb['system']}, Component: {hb['component']}")
    print(f"   Type: {hb['type']}")
    print(f"   Autopilot: {hb['autopilot']}")
    print(f"   State: {hb['state']}")
    print(f"   Mode: {hb['mode']}")
    sys.stdout.flush()
else:
    print("❌ 错误: 未收到 HEARTBEAT")
    sys.exit(1)

# ===== 3. 请求数据流 =====
print("\n[*] 请求数据流 MAV_DATA_STREAM_ALL @ 10 Hz...")
master.mav.request_data_stream_send(
    master.target_system, master.target_component,
    mavutil.mavlink.MAV_DATA_STREAM_ALL, 10, 1
)
time.sleep(1)
print("[*] 数据流请求已发送，开始捕获传感器数据...")
sys.stdout.flush()

# ===== 4. 捕获传感器数据 =====
captured = {
    'RAW_IMU': None,
    'SCALED_PRESSURE': None,
    'SCALED_PRESSURE2': None,
    'ATTITUDE': None,
    'SYS_STATUS': None,
    'GPS_RAW_INT': None,
    'HEARTBEAT_count': 0,
}
seen_types = set()

print("\n" + "-" * 60)
print("传感器数据捕获（最多 30 秒 / 60 条消息）")
print("-" * 60)
sys.stdout.flush()

time_start = time.time()
msg_count = 0
max_msgs = 60
timeout = 30

while time.time() - time_start < timeout and msg_count < max_msgs:
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
        print(f"  📡 RAW_IMU: accel=({msg.xacc}, {msg.yacc}, {msg.zacc}) "
              f"gyro=({msg.xgyro}, {msg.ygyro}, {msg.zgyro}) "
              f"mag=({msg.xmag}, {msg.ymag}, {msg.zmag})")

    elif t == 'SCALED_PRESSURE' and captured['SCALED_PRESSURE'] is None:
        captured['SCALED_PRESSURE'] = {
            'press_abs': msg.press_abs,
            'press_diff': msg.press_diff,
            'temperature': msg.temperature,
        }
        print(f"  🌡️  SCALED_PRESSURE: press_abs={msg.press_abs:.2f} hPa "
              f"temp={msg.temperature:.2f} cdeg ({msg.temperature/100:.1f}°C)")

    elif t == 'SCALED_PRESSURE2' and captured['SCALED_PRESSURE2'] is None:
        captured['SCALED_PRESSURE2'] = {
            'press_abs': msg.press_abs,
            'temperature': msg.temperature,
        }
        print(f"  🌡️  SCALED_PRESSURE2: press_abs={msg.press_abs:.2f} hPa "
              f"temp={msg.temperature:.2f} cdeg")

    elif t == 'ATTITUDE' and captured['ATTITUDE'] is None:
        captured['ATTITUDE'] = {
            'roll_deg': msg.roll * 180 / 3.14159,
            'pitch_deg': msg.pitch * 180 / 3.14159,
            'yaw_deg': msg.yaw * 180 / 3.14159,
            'rollspeed': msg.rollspeed,
            'pitchspeed': msg.pitchspeed,
            'yawspeed': msg.yawspeed,
        }
        print(f"  🧭 ATTITUDE: roll={msg.roll*180/3.14159:.1f}° "
              f"pitch={msg.pitch*180/3.14159:.1f}° yaw={msg.yaw*180/3.14159:.1f}° "
              f"(angular rates: {msg.rollspeed:.2f}, {msg.pitchspeed:.2f}, {msg.yawspeed:.2f})")

    elif t == 'SYS_STATUS' and captured['SYS_STATUS'] is None:
        captured['SYS_STATUS'] = {
            'voltage_battery': msg.voltage_battery / 1000.0,
            'current_battery': msg.current_battery / 100.0 if msg.current_battery >= 0 else 0,
            'battery_remaining': msg.battery_remaining,
        }
        print(f"  🔋 SYS_STATUS: voltage={msg.voltage_battery/1000:.2f}V "
              f"current={msg.current_battery/100:.2f}A "
              f"remaining={msg.battery_remaining}%")

    elif t == 'GPS_RAW_INT' and captured['GPS_RAW_INT'] is None:
        fix_map = {0: 'No GPS', 1: 'No Fix', 2: '2D Fix', 3: '3D Fix', 4: 'DGPS', 5: 'RTK Float', 6: 'RTK Fixed'}
        captured['GPS_RAW_INT'] = {
            'lat': msg.lat / 1e7,
            'lon': msg.lon / 1e7,
            'alt': msg.alt / 1000.0,
            'fix_type': fix_map.get(msg.fix_type, f'Unknown({msg.fix_type})'),
            'satellites_visible': msg.satellites_visible,
        }
        print(f"  🛰️  GPS_RAW_INT: lat={msg.lat/1e7:.6f} lon={msg.lon/1e7:.6f} "
              f"alt={msg.alt/1000:.1f}m fix={captured['GPS_RAW_INT']['fix_type']} "
              f"sat={msg.satellites_visible}")

    elif t == 'HEARTBEAT':
        captured['HEARTBEAT_count'] += 1

    # Check if we have all target data
    if all(v is not None for k, v in captured.items() if k != 'HEARTBEAT_count'):
        print("\n[✓] 所有目标传感器数据均已捕获，提前完成")
        break

elapsed = time.time() - time_start

# ===== 5. 汇总报告 =====
print("\n" + "=" * 60)
print("📋 完整传感器数据验证报告")
print("=" * 60)

# 统计
collected = sum(1 for k, v in captured.items() if v is not None and k != 'HEARTBEAT_count')
total = sum(1 for k, v in captured.items() if k != 'HEARTBEAT_count')

actual_count = len(captured) - 1  # exclude HEARTBEAT_count
all_collected = sum(1 for k in list(captured.keys())[:-1] if captured[k] is not None)

print(f"\n⏱️  捕获用时: {elapsed:.1f} 秒")
print(f"📨 总消息数: {msg_count}")
print(f"📊 消息类型数: {len(seen_types)}")
print(f"   => 类型列表: {sorted(seen_types)}")
print(f"📡 HEARTBEAT 消息数: {captured['HEARTBEAT_count']}")
print(f"✅ 目标数据采集率: {all_collected}/{total}")

missing = [k for k in list(captured.keys())[:-1] if captured[k] is None]
if missing:
    print(f"⚠️  未捕获数据: {', '.join(missing)}")
else:
    print(f"✅ 所有目标传感器数据已全部捕获！")

# 详细报告
if captured['HEARTBEAT']:
    hb = captured['HEARTBEAT']
    print(f"\n1️⃣  HEARTBEAT (心跳)")
    print(f"   System ID: {hb['system']}")
    print(f"   Component ID: {hb['component']}")
    print(f"   Type: {hb['type']}")
    print(f"   Autopilot: {hb['autopilot']}")
    print(f"   State: {hb['state']}")

if captured['RAW_IMU']:
    imu = captured['RAW_IMU']
    print(f"\n2️⃣  RAW_IMU (原始 IMU)")
    print(f"   Accelerometer (m/s²): x={imu['xacc']}, y={imu['yacc']}, z={imu['zacc']}")
    print(f"   Gyroscope (rad/s):     x={imu['xgyro']}, y={imu['ygyro']}, z={imu['zgyro']}")
    print(f"   Magnetometer (gauss):  x={imu['xmag']}, y={imu['ymag']}, z={imu['zmag']}")

if captured['SCALED_PRESSURE']:
    p = captured['SCALED_PRESSURE']
    print(f"\n3️⃣  SCALED_PRESSURE (气压计)")

    print(f"   绝对气压: {p['press_abs']:.2f} hPa")
    print(f"   差压: {p['press_diff']:.2f} hPa")
    print(f"   温度: {p['temperature']/100:.1f} °C")

if captured['SCALED_PRESSURE2']:
    p2 = captured['SCALED_PRESSURE2']
    print(f"   SCALED_PRESSURE2 (气压计2)")
    print(f"   绝对气压: {p2['press_abs']:.2f} hPa")
    print(f"   温度: {p2['temperature']/100:.1f} °C")

if captured['ATTITUDE']:
    a = captured['ATTITUDE']
    print(f"\n4️⃣  ATTITUDE (姿态)")

    print(f"   Roll:  {a['roll_deg']:.1f}°")
    print(f"   Pitch: {a['pitch_deg']:.1f}°")
    print(f"   Yaw:   {a['yaw_deg']:.1f}°")
    print(f"   Angular rates: roll={a['rollspeed']:.3f}, pitch={a['pitchspeed']:.3f}, yaw={a['yawspeed']:.3f} rad/s")

if captured['SYS_STATUS']:
    s = captured['SYS_STATUS']
    print(f"\n5️⃣  SYS_STATUS (系统状态)")
    print(f"   电池电压: {s['voltage_battery']:.2f} V")
    print(f"   电池电流: {s['current_battery']:.2f} A")
    print(f"   剩余电量: {s['battery_remaining']}%")

if captured['GPS_RAW_INT']:
    g = captured['GPS_RAW_INT']
    print(f"\n6️⃣  GPS_RAW_INT (GPS)")
    print(f"   位置: {g['lat']:.6f}, {g['lon']:.6f}")
    print(f"   海拔: {g['alt']:.1f} m")
    print(f"   定位: {g['fix_type']}")
    print(f"   卫星数: {g['satellites_visible']}")

print("\n" + "=" * 60)
if not missing:
    print("✅ RTT 飞控 MAVLink 通信完整验证通过！")
else:
    print(f"⚠️  部分数据未捕获: {', '.join(missing)}")
print("=" * 60)
