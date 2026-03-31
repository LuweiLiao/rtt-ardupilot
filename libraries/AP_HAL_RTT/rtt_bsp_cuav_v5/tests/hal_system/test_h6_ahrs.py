#!/usr/bin/env python3
"""
H6: AHRS / EKF Test

Validates:
  - ATTITUDE message received
  - Roll/pitch within ±30° (stationary)
  - EKF status: velocity and position flags OK
  - Sensor health bits

Method: pymavlink stream data (requests all streams at 10Hz)
"""
import sys, os, time, math
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, mavlink_connect, mavlink_request_streams

def run():
    t = TestResult("H6-AHRS")
    print(f"[H6-AHRS] === AHRS / EKF Test ===")

    conn, err = mavlink_connect(timeout=10)
    if not conn:
        t.check("MAVLink connect", False, actual=err)
        t.summary()
        return t
    t.check("MAVLink connect", True)

    mavlink_request_streams(conn, rate_hz=10)
    time.sleep(1)

    attitude = None
    ekf_status = None
    sys_status = None
    t0 = time.time()
    while time.time() - t0 < 8:
        msg = conn.recv_msg()
        if not msg:
            time.sleep(0.01)
            continue
        mtype = msg.get_type()
        if mtype == "ATTITUDE" and attitude is None:
            attitude = msg
        elif mtype == "EKF_STATUS_REPORT" and ekf_status is None:
            ekf_status = msg
        elif mtype == "SYS_STATUS" and sys_status is None:
            sys_status = msg
        if attitude and ekf_status and sys_status:
            break

    conn.close()

    if attitude:
        roll_deg = math.degrees(attitude.roll)
        pitch_deg = math.degrees(attitude.pitch)
        t.check("ATTITUDE received", True)
        t.check("Roll ±30° (stationary)", abs(roll_deg) < 30,
                actual=f"{roll_deg:.1f}°")
        t.check("Pitch ±30° (stationary)", abs(pitch_deg) < 30,
                actual=f"{pitch_deg:.1f}°")
    else:
        t.check("ATTITUDE received", False,
                actual="no ATTITUDE in 8s")

    if ekf_status:
        vel_var = ekf_status.velocity_variance
        pos_var = ekf_status.pos_horiz_variance
        t.check("EKF velocity variance < 1.0", vel_var < 1.0,
                actual=f"{vel_var:.3f}")
        t.check("EKF position variance < 1.0", pos_var < 1.0,
                actual=f"{pos_var:.3f}")
    else:
        t.check("EKF_STATUS_REPORT received", False,
                actual="not received (may need longer wait)")

    if sys_status:
        sensor_present = sys_status.onboard_control_sensors_present
        sensor_health = sys_status.onboard_control_sensors_health
        imu_bit = 0x08
        t.check("IMU sensor present", bool(sensor_present & imu_bit),
                actual=f"0x{sensor_present:08X}")
        t.check("IMU sensor healthy", bool(sensor_health & imu_bit),
                actual=f"0x{sensor_health:08X}")
    else:
        t.check("SYS_STATUS received", False)

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
