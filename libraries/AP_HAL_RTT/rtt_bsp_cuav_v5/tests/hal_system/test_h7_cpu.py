#!/usr/bin/env python3
"""
H7: CPU Load Test

Validates:
  - SYS_STATUS.load < 200 (20%)
  - DWT-based CPU idle percentage > 80%

Method: pymavlink FIRST (non-disruptive), then GDB (halts MCU)
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, gdb_read_vars, mavlink_connect, mavlink_request_streams

def run():
    t = TestResult("H7-CPU")
    print(f"[H7-CPU] === CPU Load Test ===")

    # MAVLink first (non-disruptive)
    conn, err = mavlink_connect(timeout=10)
    if conn:
        t.check("MAVLink connect", True)
        mavlink_request_streams(conn, rate_hz=2)
        sys_status = conn.recv_match(type='SYS_STATUS', blocking=True, timeout=5)
        if sys_status:
            load = sys_status.load
            t.check("SYS_STATUS.load < 200 (20%)", load < 200,
                    actual=f"{load} ({load/10:.1f}%)")
        else:
            t.check("SYS_STATUS received", False)
        conn.close()
    else:
        t.check("MAVLink connect", False, actual=err)

    # GDB second (may disrupt CDC)
    time.sleep(1)
    vars_ = gdb_read_vars(["rtt_cpu_idle_pct", "rtt_cpu_idle_cycles"])
    if vars_:
        idle_pct = int(vars_.get("rtt_cpu_idle_pct", "0"))
        t.check("DWT CPU idle > 80%", idle_pct > 80,
                actual=f"{idle_pct}%", expected="> 80%")
    else:
        t.check("GDB readable", False, actual="GDB connection failed")

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
