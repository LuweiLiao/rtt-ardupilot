#!/usr/bin/env python3
"""
AP_HAL_RTT — HAL System Test Runner

Runs all HAL-level tests in dependency order.
Each test is independent and can also be run standalone.

Usage:
    python3 run_all.py              # Run all tests
    python3 run_all.py h1 h3 h7     # Run specific tests
    python3 run_all.py --stop       # Stop on first failure

Dependency order (bottom-up):
    H1 Timing    →  (no deps)
    H2 Threads   →  (no deps)
    H3 Serial    →  (no deps)
    H7 CPU       →  H1
    H4 Params    →  H3
    H5 Sensors   →  H3
    H6 AHRS      →  H5
"""
import sys
import os
import time
import importlib
import json

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TESTS_DIR)

TEST_ORDER = [
    # Group 1: GDB-based tests (halt/resume the MCU)
    ("h1", "test_h1_timing",  "Clock & Timing"),
    ("h2", "test_h2_threads", "Threads & Scheduler"),
    # ── CDC recovery pause inserted by runner ──
    # Group 2: MAVLink-based tests (USB CDC must be stable)
    ("h3", "test_h3_serial",  "Serial / USB CDC"),
    ("h5", "test_h5_sensors", "Sensor Data"),
    ("h6", "test_h6_ahrs",    "AHRS / EKF"),
    # Group 3: Mixed (GDB + MAVLink) — MAVLink first, GDB last
    ("h7", "test_h7_cpu",     "CPU Load"),
    # Group 4: Heavy traffic tests (last to avoid interference)
    ("h4", "test_h4_params",  "Parameter Download"),
]
GDB_TESTS = {"h1", "h2"}  # tests that use GDB halt/resume


def main():
    stop_on_fail = "--stop" in sys.argv
    requested = [a for a in sys.argv[1:] if not a.startswith("-")]

    tests_to_run = TEST_ORDER
    if requested:
        tests_to_run = [t for t in TEST_ORDER if t[0] in requested]
        if not tests_to_run:
            print(f"Unknown test IDs: {requested}")
            print(f"Available: {', '.join(t[0] for t in TEST_ORDER)}")
            sys.exit(1)

    print("=" * 60)
    print("  AP_HAL_RTT — HAL System Test Suite")
    print("=" * 60)
    print(f"  Tests to run: {len(tests_to_run)}")
    print(f"  Stop on fail: {stop_on_fail}")
    print("=" * 60)
    print()

    results = []
    all_pass = True
    t_start = time.time()
    last_was_gdb = False

    for tag, module_name, desc in tests_to_run:
        # Insert CDC recovery pause when switching from GDB to MAVLink tests
        is_gdb = tag in GDB_TESTS
        if last_was_gdb and not is_gdb:
            print(f"\n  [RUNNER] Waiting 3s for USB CDC to recover after GDB halt...")
            time.sleep(3)
        last_was_gdb = is_gdb

        print(f"\n{'─' * 50}")
        try:
            mod = importlib.import_module(module_name)
            result = mod.run()
            results.append(result.to_dict())
            if not result.to_dict()["passed"]:
                all_pass = False
                if stop_on_fail:
                    print(f"\n  *** Stopping: {tag} FAILED ***")
                    break
        except Exception as e:
            print(f"  [{tag.upper()}] EXCEPTION: {e}")
            results.append({"name": tag, "passed": False, "error": str(e)})
            all_pass = False
            if stop_on_fail:
                break

        time.sleep(1)

    total_time = time.time() - t_start

    # Summary
    print(f"\n{'=' * 60}")
    print("  SUMMARY")
    print(f"{'=' * 60}")
    for r in results:
        status = "PASS" if r.get("passed") else "FAIL"
        name = r.get("name", "?")
        checks = f"{r.get('checks_passed', '?')}/{r.get('checks_total', '?')}"
        elapsed = r.get("elapsed", 0)
        marker = "✓" if r.get("passed") else "✗"
        print(f"  {marker} {name:<20s} {status:<6s} ({checks} checks, {elapsed:.1f}s)")

    passed_count = sum(1 for r in results if r.get("passed"))
    total_count = len(results)
    final = "ALL PASS" if all_pass else "SOME FAILED"
    print(f"\n  {final}: {passed_count}/{total_count} tests passed in {total_time:.1f}s")
    print(f"{'=' * 60}")

    # Write JSON report
    report_path = os.path.join(TESTS_DIR, "test_report.json")
    with open(report_path, "w") as f:
        json.dump({
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "all_pass": all_pass,
            "total_time": round(total_time, 2),
            "results": results,
        }, f, indent=2)
    print(f"\n  Report: {report_path}")

    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
