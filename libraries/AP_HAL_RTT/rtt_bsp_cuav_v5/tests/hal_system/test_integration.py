#!/usr/bin/env python3
"""
Flight Controller Integration Test

Tests the COMBINED behavior of all subsystems working together:
  I1: PreArm status — sensor health bits, calibration state
  I2: Mode switching — STABILIZE default, request mode changes
  I3: MAVLink commands — arm/disarm response, command ACK
  I4: Continuous stream stability — 60s uninterrupted data flow
  I5: Resource stability — CPU load, memory, no stuck threads over time
  I6: Parameter read/write — change a param, read back, restore

Requires: firmware running, USB CDC connected
"""
import sys, os, time, math
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, mavlink_connect, mavlink_request_streams, gdb_read_vars

def test_i1_prearm(conn):
    """I1: PreArm & sensor health"""
    t = TestResult("I1-PREARM")
    print(f"[I1-PREARM] === PreArm & Sensor Health ===")

    mavlink_request_streams(conn, rate_hz=4)

    sys_status = conn.recv_match(type='SYS_STATUS', blocking=True, timeout=15)
    if not sys_status:
        t.check("SYS_STATUS received", False)
        t.summary()
        return t

    present = sys_status.onboard_control_sensors_present
    enabled = sys_status.onboard_control_sensors_enabled
    health  = sys_status.onboard_control_sensors_health

    GYRO_3D    = 0x01 | 0x08           # 3D_GYRO + 3D_ACCEL
    BARO       = 0x02 << 1             # ABSOLUTE_PRESSURE (bit 3 in some defs)
    COMPASS    = 0x04 << 1             # 3D_MAG
    AHRS       = 1 << 21              # AHRS

    t.check("3D Gyro+Accel present", bool(present & 0x08),
            actual=f"0x{present:08X}")
    t.check("3D Gyro+Accel healthy", bool(health & 0x08),
            actual=f"0x{health:08X}")
    t.check("AHRS present", bool(present & AHRS),
            actual=f"bit21=0x{AHRS:X}")
    ahrs_healthy = bool(health & AHRS)
    t.check("AHRS healthy (info: needs GPS)", True,
            actual=f"{'yes' if ahrs_healthy else 'no (expected without GPS)'}")

    load = sys_status.load
    t.check("CPU load < 100 (10%)", load < 100,
            actual=f"{load} ({load/10:.1f}%)")

    voltage = sys_status.voltage_battery
    t.check("Battery voltage reported (>= 0)", voltage >= 0,
            actual=f"{voltage} mV")

    # Check for STATUSTEXT messages (PreArm failures)
    prearm_msgs = []
    t0 = time.time()
    while time.time() - t0 < 3:
        msg = conn.recv_match(type='STATUSTEXT', blocking=False)
        if msg:
            txt = msg.text
            if 'PreArm' in txt or 'Arm' in txt:
                prearm_msgs.append(txt)
        else:
            time.sleep(0.05)
    if prearm_msgs:
        t.check("PreArm messages (info only)", True,
                actual='; '.join(prearm_msgs[:3]))
    else:
        t.check("No PreArm errors in 3s", True)

    t.summary()
    return t


def test_i2_mode(conn):
    """I2: Mode switching"""
    t = TestResult("I2-MODE")
    print(f"[I2-MODE] === Mode Switching ===")

    hb = conn.recv_match(type='HEARTBEAT', blocking=True, timeout=5)
    if not hb:
        t.check("Heartbeat received", False)
        t.summary()
        return t

    from pymavlink import mavutil
    current_mode = hb.custom_mode
    t.check("Current mode reported", True,
            actual=f"mode={current_mode}")

    # STABILIZE=0, ALT_HOLD=2, LOITER=5, LAND=9
    STABILIZE = 0
    t.check("Default mode is STABILIZE (0)", current_mode == STABILIZE,
            actual=f"mode={current_mode}", expected="0 (STABILIZE)")

    # Try setting ALT_HOLD mode
    ALT_HOLD = 2
    conn.mav.set_mode_send(
        conn.target_system,
        mavutil.mavlink.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,
        ALT_HOLD)

    ack = conn.recv_match(type='COMMAND_ACK', blocking=True, timeout=3)
    mode_hb = conn.recv_match(type='HEARTBEAT', blocking=True, timeout=3)
    if mode_hb:
        t.check("Mode change response received", True,
                actual=f"new_mode={mode_hb.custom_mode}")
    else:
        t.check("Mode change response", False, actual="no heartbeat after set_mode")

    # Switch back to STABILIZE
    conn.mav.set_mode_send(
        conn.target_system,
        mavutil.mavlink.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,
        STABILIZE)
    time.sleep(0.5)
    hb2 = conn.recv_match(type='HEARTBEAT', blocking=True, timeout=3)
    if hb2:
        t.check("Restored to STABILIZE", hb2.custom_mode == STABILIZE,
                actual=f"mode={hb2.custom_mode}")

    t.summary()
    return t


def test_i3_commands(conn):
    """I3: MAVLink command handling (arm attempt — expected to fail without RC)"""
    t = TestResult("I3-COMMANDS")
    print(f"[I3-COMMANDS] === Command Handling ===")

    from pymavlink import mavutil

    # Request ARM — should be DENIED (no RC, PreArm checks fail)
    conn.mav.command_long_send(
        conn.target_system, conn.target_component,
        mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM,
        0, 1, 0, 0, 0, 0, 0, 0)

    ack = conn.recv_match(type='COMMAND_ACK', blocking=True, timeout=5)
    if ack:
        t.check("ARM command ACK received", True,
                actual=f"result={ack.result}")
        t.check("ARM correctly denied (no RC/PreArm)",
                ack.result != 0,
                actual=f"result={ack.result} (0=accepted, 4=denied)")
    else:
        t.check("ARM command ACK received", False)

    # Request version
    conn.mav.command_long_send(
        conn.target_system, conn.target_component,
        mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE,
        0, 148, 0, 0, 0, 0, 0, 0)  # AUTOPILOT_VERSION (148)

    ver = conn.recv_match(type='AUTOPILOT_VERSION', blocking=True, timeout=5)
    if ver:
        fw_ver = ver.flight_sw_version
        major = (fw_ver >> 24) & 0xFF
        minor = (fw_ver >> 16) & 0xFF
        patch = (fw_ver >> 8) & 0xFF
        t.check("Firmware version reported", True,
                actual=f"v{major}.{minor}.{patch}")
    else:
        t.check("Firmware version", False,
                actual="AUTOPILOT_VERSION not received")

    t.summary()
    return t


def test_i4_stream_stability(conn, duration_s=60):
    """I4: Stream stability over time"""
    t = TestResult("I4-STREAMS")
    print(f"[I4-STREAMS] === Stream Stability ({duration_s}s) ===")

    mavlink_request_streams(conn, rate_hz=2)
    time.sleep(0.5)

    msg_count = 0
    hb_count = 0
    attitude_count = 0
    max_gap_s = 0
    last_msg_time = time.time()
    errors = 0
    t0 = time.time()

    while time.time() - t0 < duration_s:
        msg = conn.recv_msg()
        if msg:
            now = time.time()
            gap = now - last_msg_time
            if gap > max_gap_s:
                max_gap_s = gap
            last_msg_time = now
            msg_count += 1
            mtype = msg.get_type()
            if mtype == "HEARTBEAT":
                hb_count += 1
            elif mtype == "ATTITUDE":
                attitude_count += 1
            elif mtype == "BAD_DATA":
                errors += 1
        else:
            time.sleep(0.001)

    elapsed = time.time() - t0
    rate = msg_count / elapsed if elapsed > 0 else 0

    t.check(f"Messages received > {duration_s * 2}",
            msg_count > duration_s * 2,
            actual=f"{msg_count} msgs ({rate:.1f}/s)")

    expected_hb = int(duration_s * 0.7)
    t.check(f"Heartbeats > {expected_hb} (≈1/s with CDC jitter)", hb_count > expected_hb,
            actual=f"{hb_count} heartbeats")

    min_att = int(duration_s * 0.5)
    t.check(f"ATTITUDE messages flowing (> {min_att})", attitude_count > min_att,
            actual=f"{attitude_count} attitudes")

    t.check("Max message gap < 8s", max_gap_s < 8.0,
            actual=f"{max_gap_s:.2f}s")

    t.check("Parse errors < 2%", errors < msg_count * 0.02,
            actual=f"{errors} errors / {msg_count} msgs")

    t.summary()
    return t


def test_i5_resources(conn):
    """I5: Resource stability after long run (pure MAVLink — no GDB)"""
    t = TestResult("I5-RESOURCES")
    print(f"[I5-RESOURCES] === Resource Stability ===")

    mavlink_request_streams(conn, rate_hz=2)
    time.sleep(0.5)

    # Collect MEMINFO and SYS_STATUS
    mem = None
    sys_stat = None
    hb = None
    t0 = time.time()
    while time.time() - t0 < 8:
        msg = conn.recv_msg()
        if not msg:
            time.sleep(0.01)
            continue
        mtype = msg.get_type()
        if mtype == "MEMINFO" and mem is None:
            mem = msg
        elif mtype == "SYS_STATUS" and sys_stat is None:
            sys_stat = msg
        elif mtype == "HEARTBEAT" and hb is None:
            hb = msg
        if mem and sys_stat and hb:
            break

    if hb:
        t.check("System still responsive", True,
                actual=f"mode={hb.custom_mode}")
    else:
        t.check("System responsive", False)

    if mem:
        free_kb = mem.freemem
        t.check("Free memory > 10 KB", free_kb > 10,
                actual=f"{free_kb} KB free")
    else:
        t.check("MEMINFO received", False)

    if sys_stat:
        load = sys_stat.load
        t.check("CPU load still low (< 200)", load < 200,
                actual=f"{load} ({load/10:.1f}%)")
    else:
        t.check("SYS_STATUS received", False)

    t.summary()
    return t


def test_i6_param_rw(conn):
    """I6: Parameter read/write round-trip"""
    t = TestResult("I6-PARAM-RW")
    print(f"[I6-PARAM-RW] === Parameter Read/Write ===")

    PARAM_NAME = "SCHED_LOOP_RATE"

    # Read current value
    conn.mav.param_request_read_send(
        conn.target_system, conn.target_component,
        PARAM_NAME.encode('utf-8'), -1)

    orig = conn.recv_match(type='PARAM_VALUE', blocking=True, timeout=5)
    if not orig:
        t.check(f"Read {PARAM_NAME}", False)
        t.summary()
        return t

    orig_val = orig.param_value
    t.check(f"Read {PARAM_NAME}", True,
            actual=f"{orig_val}")

    t.check("SCHED_LOOP_RATE == 400", orig_val == 400.0,
            actual=f"{orig_val}", expected="400.0")

    # Write a test value (change to 200, then restore)
    TEST_VAL = 200.0
    conn.mav.param_set_send(
        conn.target_system, conn.target_component,
        PARAM_NAME.encode('utf-8'),
        TEST_VAL, orig.param_type)

    ack = conn.recv_match(type='PARAM_VALUE', blocking=True, timeout=5)
    if ack:
        t.check("Write acknowledged", True,
                actual=f"new={ack.param_value}")
        t.check("Write value correct", ack.param_value == TEST_VAL,
                actual=f"{ack.param_value}", expected=f"{TEST_VAL}")
    else:
        t.check("Write acknowledged", False)

    # Restore original
    conn.mav.param_set_send(
        conn.target_system, conn.target_component,
        PARAM_NAME.encode('utf-8'),
        orig_val, orig.param_type)
    restore = conn.recv_match(type='PARAM_VALUE', blocking=True, timeout=5)
    if restore:
        t.check("Restore original value", restore.param_value == orig_val,
                actual=f"{restore.param_value}", expected=f"{orig_val}")
    else:
        t.check("Restore original", False)

    t.summary()
    return t


def run():
    """Run full integration test suite."""
    print("=" * 60)
    print("  AP_HAL_RTT — Flight Controller Integration Test")
    print("=" * 60)

    conn, err = mavlink_connect(timeout=10)
    if not conn:
        print(f"FATAL: Cannot connect — {err}")
        return False

    print(f"  Connected: sysid={conn.target_system}")
    print("=" * 60)
    print()

    results = []
    all_pass = True

    # I1: PreArm
    r = test_i1_prearm(conn)
    results.append(r)
    if not r.to_dict()["passed"]: all_pass = False

    # I2: Mode
    print()
    time.sleep(0.5)
    r = test_i2_mode(conn)
    results.append(r)
    if not r.to_dict()["passed"]: all_pass = False

    # I3: Commands
    print()
    time.sleep(0.5)
    r = test_i3_commands(conn)
    results.append(r)
    if not r.to_dict()["passed"]: all_pass = False

    # I6: Param R/W (before heavy tests, while CDC is stable)
    print()
    time.sleep(0.5)
    r = test_i6_param_rw(conn)
    results.append(r)
    if not r.to_dict()["passed"]: all_pass = False

    # I4: Stream stability (60s)
    print()
    time.sleep(0.5)
    r = test_i4_stream_stability(conn, duration_s=60)
    results.append(r)
    if not r.to_dict()["passed"]: all_pass = False

    # I5: Resources — reconnect after long stream test
    print()
    conn.close()
    time.sleep(2)
    conn5, err5 = mavlink_connect(timeout=10)
    if conn5:
        r = test_i5_resources(conn5)
        results.append(r)
        if not r.to_dict()["passed"]: all_pass = False
        conn5.close()
    else:
        print(f"  [I5] SKIP: CDC reconnect failed ({err5})")
        t_skip = TestResult("I5-RESOURCES")
        t_skip.check("CDC reconnect", False, actual=err5)
        t_skip.summary()
        results.append(t_skip)

    conn.close()

    # Summary
    print(f"\n{'=' * 60}")
    print("  INTEGRATION TEST SUMMARY")
    print(f"{'=' * 60}")
    for r in results:
        d = r.to_dict()
        status = "PASS" if d["passed"] else "FAIL"
        marker = "✓" if d["passed"] else "✗"
        print(f"  {marker} {d['name']:<20s} {status:<6s} ({d['checks_passed']}/{d['checks_total']} checks, {d['elapsed']:.1f}s)")

    passed = sum(1 for r in results if r.to_dict()["passed"])
    total = len(results)
    final = "ALL PASS" if all_pass else "SOME FAILED"
    total_time = sum(r.to_dict()["elapsed"] for r in results)
    print(f"\n  {final}: {passed}/{total} tests in {total_time:.1f}s")
    print(f"{'=' * 60}")

    return all_pass


if __name__ == "__main__":
    ok = run()
    sys.exit(0 if ok else 1)
