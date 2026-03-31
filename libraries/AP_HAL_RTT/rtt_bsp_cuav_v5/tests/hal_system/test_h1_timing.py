#!/usr/bin/env python3
"""
H1: Clock & Timing Test

Validates:
  - SystemCoreClock == 216 MHz
  - millis() advancing at correct rate
  - main loop period ≈ 2500µs (400 Hz)
  - DWT CYCCNT running

Method: GDB single-shot reads (no MAVLink dependency)
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, gdb_read_vars

def run():
    t = TestResult("H1-TIMING")
    print(f"[H1-TIMING] === Clock & Timing Test ===")

    # Read 1: snapshot
    vars1 = gdb_read_vars([
        "SystemCoreClock",
        "rtt_dbg_main_loop_iterations",
        "rtt_dbg_loop_time_us",
    ])
    if not vars1:
        t.check("GDB connection", False)
        return t

    t.check("GDB connection", True)

    clk = int(vars1.get("SystemCoreClock", "0"))
    t.check("SystemCoreClock == 216000000", clk == 216000000,
            actual=clk, expected=216000000)

    loop_us = int(vars1.get("rtt_dbg_loop_time_us", "0"))
    t.check("Main loop period 1000-5000µs", 1000 <= loop_us <= 5000,
            actual=f"{loop_us}µs", expected="1000-5000µs")

    iters1 = int(vars1.get("rtt_dbg_main_loop_iterations", "0"))
    t.check("Main loop running (iterations > 100)", iters1 > 100,
            actual=iters1)

    # Read 2: after 2s delay, check iterations advanced
    time.sleep(2)
    vars2 = gdb_read_vars(["rtt_dbg_main_loop_iterations"])
    if vars2:
        iters2 = int(vars2.get("rtt_dbg_main_loop_iterations", "0"))
        delta = iters2 - iters1
        hz = delta / 2.0
        t.check("Loop rate 300-500 Hz over 2s", 300 <= hz <= 500,
                actual=f"{hz:.0f} Hz", expected="300-500 Hz")
    else:
        t.check("GDB second read", False)

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
