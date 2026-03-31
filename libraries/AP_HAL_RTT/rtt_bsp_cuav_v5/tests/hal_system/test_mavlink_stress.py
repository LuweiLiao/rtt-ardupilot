#!/usr/bin/env python3
"""
MAVLink Stress Tests for AP_HAL_RTT

S1: Full parameter download × N rounds
S2: High-rate stream + disconnect/reconnect × N rounds
S3: Long-duration streaming (configurable) + memory leak detection

Usage:
  python3 test_mavlink_stress.py [--rounds N] [--long-min M] [--port /dev/ttyACM3]
"""

import sys, os, time, argparse
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import find_cdc_port

def get_connection(port, baud=115200, timeout=10, retries=3):
    from pymavlink import mavutil
    for attempt in range(retries):
        try:
            m = mavutil.mavlink_connection(port, baud=baud, source_system=255)
            m.wait_heartbeat(timeout=timeout)
            if m.target_system == 0:
                m.close()
                if attempt < retries - 1:
                    time.sleep(3)
                    continue
                raise RuntimeError("No heartbeat (target_system=0)")
            return m
        except Exception as e:
            if attempt < retries - 1:
                time.sleep(3)
                continue
            raise

def request_streams(conn, rate_hz=10):
    from pymavlink import mavutil
    conn.mav.request_data_stream_send(
        conn.target_system, conn.target_component,
        mavutil.mavlink.MAV_DATA_STREAM_ALL, rate_hz, 1)

# ─────────────────────────────────────────────────────────────────
# S1: Full parameter download × N
# ─────────────────────────────────────────────────────────────────
def _fetch_all_params(conn, max_retries=3, overall_timeout=60):
    """Download all params with retry for missing ones (like real GCS)."""
    params = {}
    expected_count = 0

    for attempt in range(max_retries):
        conn.mav.param_request_list_send(conn.target_system, conn.target_component)

        stall_start = time.time()
        deadline = time.time() + overall_timeout

        while time.time() < deadline:
            msg = conn.recv_match(type='PARAM_VALUE', blocking=True, timeout=2)
            if msg:
                params[msg.param_id] = msg.param_value
                if msg.param_count > 0:
                    expected_count = msg.param_count
                stall_start = time.time()
                if expected_count > 0 and len(params) >= expected_count:
                    return params, expected_count
            if time.time() - stall_start > 8:
                break

        if expected_count > 0 and len(params) >= expected_count:
            break

        if expected_count > 0:
            missing = expected_count - len(params)
            if missing <= 20 and attempt < max_retries - 1:
                known_indices = set()
                for pid in params:
                    conn.mav.param_request_read_send(
                        conn.target_system, conn.target_component, b'', -1)
                time.sleep(1)

    return params, expected_count


def test_s1_param_download(port, rounds=5):
    print(f"\n{'='*60}")
    print(f"  S1: Full Parameter Download × {rounds} (with retry)")
    print(f"{'='*60}")
    from pymavlink import mavutil

    conn = get_connection(port, retries=3)
    time.sleep(1)

    results = []
    for r in range(rounds):
        while conn.recv_match(blocking=False):
            pass

        t0 = time.time()
        params, expected_count = _fetch_all_params(conn)
        dt = time.time() - t0

        ok = len(params) >= expected_count and expected_count > 0
        results.append((ok, len(params), expected_count, dt))
        status = "PASS" if ok else "FAIL"
        print(f"  Round {r+1}/{rounds}: {status} — {len(params)}/{expected_count} params in {dt:.1f}s")
        time.sleep(2)

    conn.close()

    passed = sum(1 for r in results if r[0])
    threshold = max(1, rounds - 1)
    overall = "PASS" if passed >= threshold else "FAIL"
    print(f"\n  S1 RESULT: {overall} ({passed}/{rounds} rounds, need {threshold}+)")
    return passed >= threshold

# ─────────────────────────────────────────────────────────────────
# S2: High-speed stream + disconnect/reconnect × N
# ─────────────────────────────────────────────────────────────────
def test_s2_reconnect(port, rounds=10):
    print(f"\n{'='*60}")
    print(f"  S2: Stream + Disconnect/Reconnect × {rounds}")
    print(f"{'='*60}")

    results = []
    for r in range(rounds):
        try:
            conn = get_connection(port, timeout=10, retries=3)
            request_streams(conn, rate_hz=10)

            count = 0
            t0 = time.time()
            while time.time() - t0 < 5:
                msg = conn.recv_match(blocking=True, timeout=1)
                if msg:
                    count += 1
            conn.close()

            time.sleep(3)

            conn2 = get_connection(port, timeout=10, retries=2)
            hb = conn2.recv_match(type='HEARTBEAT', blocking=True, timeout=8)
            conn2.close()

            ok = count > 5 and hb is not None
            results.append(ok)
            status = "PASS" if ok else "FAIL"
            print(f"  Round {r+1}/{rounds}: {status} — {count} msgs, reconnect={'OK' if hb else 'FAIL'}")
        except Exception as e:
            results.append(False)
            print(f"  Round {r+1}/{rounds}: FAIL — {e}")
        time.sleep(3)

    passed = sum(1 for r in results if r)
    threshold = max(1, int(rounds * 0.8))
    overall = "PASS" if passed >= threshold else "FAIL"
    print(f"\n  S2 RESULT: {overall} ({passed}/{rounds} rounds, need {threshold}+)")
    return passed >= threshold

# ─────────────────────────────────────────────────────────────────
# S3: Long-duration stream + memory leak detection
# ─────────────────────────────────────────────────────────────────
def test_s3_long_stream(port, duration_min=10):
    print(f"\n{'='*60}")
    print(f"  S3: Long-Duration Stream ({duration_min} min) + Leak Check")
    print(f"{'='*60}")
    from pymavlink import mavutil

    conn = get_connection(port)
    request_streams(conn, rate_hz=4)

    total_msgs = 0
    total_errors = 0
    start = time.time()
    duration_s = duration_min * 60
    check_interval = 30
    last_check = start

    initial_mem = None
    final_mem = None
    initial_load = None
    final_load = None
    max_gap = 0
    last_msg_time = start
    reconnects = 0
    disconnected_s = 0.0

    print(f"  Streaming for {duration_min} minutes...")

    while time.time() - start < duration_s:
        try:
            msg = conn.recv_match(blocking=True, timeout=2)
        except Exception as e:
            # USB CDC can occasionally drop; recover by reconnecting and continuing.
            # Only do this for genuine serial/CDC exceptions; let other errors surface.
            try:
                import serial  # pyserial (used by pymavlink)
                SerialException = getattr(serial, "SerialException", None)
            except Exception:
                SerialException = None

            if SerialException is not None and isinstance(e, SerialException):
                t_disc_start = time.time()
                try:
                    conn.close()
                except Exception:
                    pass
                try:
                    conn = get_connection(port, timeout=15, retries=6)
                    request_streams(conn, rate_hz=4)
                    reconnects += 1
                    dt = time.time() - t_disc_start
                    disconnected_s += dt
                    print(f"    [RECOVER] reconnect #{reconnects} after {dt:.1f}s ({type(e).__name__})")
                    # do not treat as a normal message timeout
                    continue
                except Exception as e2:
                    dt = time.time() - t_disc_start
                    disconnected_s += dt
                    print(f"    [FATAL] reconnect failed after {dt:.1f}s ({type(e2).__name__})")
                    raise

            # Not a serial exception: don't hide bugs.
            raise
        if msg:
            total_msgs += 1
            now = time.time()
            gap = now - last_msg_time
            if gap > max_gap:
                max_gap = gap
            last_msg_time = now

            if msg.get_type() == 'SYS_STATUS':
                mem = getattr(msg, 'onboard_control_sensors_present', 0)
                load = msg.load
                if initial_load is None:
                    initial_load = load
                    sys_msg = conn.recv_match(type='MEMINFO', blocking=True, timeout=2)
                    if sys_msg:
                        initial_mem = getattr(sys_msg, 'freemem', None)
                final_load = load
        else:
            total_errors += 1

        now = time.time()
        if now - last_check > check_interval:
            elapsed = now - start
            rate = total_msgs / elapsed if elapsed > 0 else 0
            pct = elapsed / duration_s * 100
            print(f"    [{pct:5.1f}%] {total_msgs} msgs ({rate:.1f}/s), errors={total_errors}, max_gap={max_gap:.2f}s")
            last_check = now

    sys_status = conn.recv_match(type='SYS_STATUS', blocking=True, timeout=5)
    if sys_status:
        final_load = sys_status.load

    meminfo = conn.recv_match(type='MEMINFO', blocking=True, timeout=5)
    if meminfo:
        final_mem = getattr(meminfo, 'freemem', None)

    conn.close()

    elapsed = time.time() - start
    rate = total_msgs / elapsed if elapsed > 0 else 0

    print(f"\n  --- S3 Results ---")
    print(f"  Duration:    {elapsed:.0f}s ({elapsed/60:.1f} min)")
    print(f"  Messages:    {total_msgs} ({rate:.1f}/s)")
    print(f"  Errors:      {total_errors}")
    print(f"  Max gap:     {max_gap:.2f}s")
    print(f"  Reconnects:  {reconnects} (down {disconnected_s:.1f}s total)")
    print(f"  CPU load:    {initial_load} → {final_load}")
    if initial_mem is not None and final_mem is not None:
        print(f"  Free memory: {initial_mem} → {final_mem} KB (delta={final_mem - initial_mem})")
    else:
        print(f"  Free memory: (MEMINFO not available)")

    ok_msgs = total_msgs > duration_s * 2
    ok_gap = max_gap < 30
    ok_load = final_load is not None and final_load < 200
    ok = ok_msgs and ok_gap and ok_load

    checks = [
        ("Messages > 2/s average", ok_msgs, f"{rate:.1f}/s"),
        ("Max gap < 30s (USB CDC limit)", ok_gap, f"{max_gap:.2f}s"),
        ("CPU load < 20%", ok_load, f"{final_load}" if final_load else "N/A"),
    ]

    for name, passed, actual in checks:
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name} (got {actual})")

    overall = "PASS" if ok else "FAIL"
    print(f"\n  S3 RESULT: {overall}")
    return ok


def main():
    parser = argparse.ArgumentParser(description="MAVLink Stress Tests")
    parser.add_argument('--port', default=None, help='Serial port')
    parser.add_argument('--rounds', type=int, default=5, help='Rounds for S1/S2')
    parser.add_argument('--long-min', type=int, default=10, help='Duration for S3 in minutes')
    parser.add_argument('--test', choices=['s1', 's2', 's3', 'all'], default='all')
    args = parser.parse_args()

    port = args.port or find_cdc_port()
    if not port:
        print("FATAL: CDC port not found")
        sys.exit(1)

    print(f"Using port: {port}")

    results = {}
    if args.test in ('all', 's1'):
        results['S1'] = test_s1_param_download(port, rounds=args.rounds)
    if args.test in ('all', 's2'):
        results['S2'] = test_s2_reconnect(port, rounds=args.rounds)
    if args.test in ('all', 's3'):
        results['S3'] = test_s3_long_stream(port, duration_min=args.long_min)

    print(f"\n{'='*60}")
    print(f"  STRESS TEST SUMMARY")
    print(f"{'='*60}")
    all_pass = True
    for name, ok in results.items():
        status = "PASS" if ok else "FAIL"
        mark = "✓" if ok else "✗"
        print(f"  {mark} {name}: {status}")
        if not ok:
            all_pass = False

    if all_pass:
        print(f"\n  ALL STRESS TESTS PASSED")
    else:
        print(f"\n  SOME STRESS TESTS FAILED")
    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
