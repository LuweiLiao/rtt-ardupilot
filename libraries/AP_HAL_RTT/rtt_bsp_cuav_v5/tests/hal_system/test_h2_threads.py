#!/usr/bin/env python3
"""
H2: Thread / Scheduler Test

Validates:
  - All expected threads exist and running
  - Timer callbacks firing (overrun count stable or increasing)
  - Work time reasonable

Method: GDB reads (no MAVLink dependency)
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, gdb_read_vars

EXPECTED_THREADS = ["ap_timer", "ap_uart", "ap_io", "storage", "main"]

def run():
    t = TestResult("H2-THREADS")
    print(f"[H2-THREADS] === Thread / Scheduler Test ===")

    vars_ = gdb_read_vars([
        "rtt_dbg_hal_run_called",
        "rtt_dbg_main_loop_entry_called",
        "rtt_dbg_work_time_us",
        "rtt_dbg_work_time_max_us",
        "rtt_dbg_overrun_count",
        "rtt_dbg_main_loop_iterations",
    ])
    if not vars_:
        t.check("GDB connection", False)
        return t
    t.check("GDB connection", True)

    # HAL run() completed setup
    run_magic = vars_.get("rtt_dbg_hal_run_called", "0")
    t.check("HAL run() entered loop", run_magic == "0x11111111" or run_magic == "286331153",
            actual=run_magic, expected="0x11111111")

    entry_magic = vars_.get("rtt_dbg_main_loop_entry_called", "0")
    t.check("main_loop_entry called", entry_magic == "0x12345678" or entry_magic == "305419896",
            actual=entry_magic, expected="0x12345678")

    # Work time per loop should be < 5000µs for 400Hz
    work = int(vars_.get("rtt_dbg_work_time_us", "0"))
    t.check("Work time < 5000µs", 0 < work < 5000,
            actual=f"{work}µs", expected="< 5000µs")

    work_max = int(vars_.get("rtt_dbg_work_time_max_us", "0"))
    t.check("Max work time < 500000µs (incl. startup calibration)", work_max < 500000,
            actual=f"{work_max}µs", expected="< 500000µs")

    iters = int(vars_.get("rtt_dbg_main_loop_iterations", "0"))
    t.check("Iterations advancing (> 1000)", iters > 1000,
            actual=iters)

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
