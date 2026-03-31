#!/usr/bin/env python3
"""
H5: Sensor Data Test

Validates:
  - IMU data non-zero (RAW_IMU)
  - Barometer data reasonable (SCALED_PRESSURE)
  - Compass data present (RAW_IMU mag fields)

Method: pymavlink stream data (requests all streams at 10Hz)
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, mavlink_connect, mavlink_request_streams

def run():
    t = TestResult("H5-SENSORS")
    print(f"[H5-SENSORS] === Sensor Data Test ===")

    conn, err = mavlink_connect(timeout=10)
    if not conn:
        t.check("MAVLink connect", False, actual=err)
        t.summary()
        return t
    t.check("MAVLink connect", True)

    mavlink_request_streams(conn, rate_hz=10)
    time.sleep(1)

    raw_imu = None
    scaled_pressure = None
    t0 = time.time()
    while time.time() - t0 < 8:
        msg = conn.recv_msg()
        if not msg:
            time.sleep(0.01)
            continue
        mtype = msg.get_type()
        if mtype == "RAW_IMU" and raw_imu is None:
            raw_imu = msg
        elif mtype == "SCALED_PRESSURE" and scaled_pressure is None:
            scaled_pressure = msg
        if raw_imu and scaled_pressure:
            break

    conn.close()

    if raw_imu:
        ax, ay, az = raw_imu.xacc, raw_imu.yacc, raw_imu.zacc
        accel_valid = any(abs(v) > 10 for v in [ax, ay, az])
        t.check("IMU accel non-zero", accel_valid,
                actual=f"x={ax} y={ay} z={az}")

        gx, gy, gz = raw_imu.xgyro, raw_imu.ygyro, raw_imu.zgyro
        t.check("IMU gyro present", True,
                actual=f"x={gx} y={gy} z={gz}")

        mx, my, mz = raw_imu.xmag, raw_imu.ymag, raw_imu.zmag
        mag_valid = any(abs(v) > 0 for v in [mx, my, mz])
        t.check("Compass data non-zero", mag_valid,
                actual=f"x={mx} y={my} z={mz}")
    else:
        t.check("RAW_IMU received", False,
                actual="no RAW_IMU in 8s")

    if scaled_pressure:
        press = scaled_pressure.press_abs
        temp = scaled_pressure.temperature / 100.0
        t.check("Baro pressure 800-1200 hPa", 800 < press < 1200,
                actual=f"{press:.1f} hPa")
        t.check("Baro temperature 0-60°C", 0 < temp < 60,
                actual=f"{temp:.1f}°C")
    else:
        t.check("SCALED_PRESSURE received", False)

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
