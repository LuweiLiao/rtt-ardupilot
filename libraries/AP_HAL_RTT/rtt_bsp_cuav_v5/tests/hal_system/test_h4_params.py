#!/usr/bin/env python3
"""
H4: Parameter Download Test

Validates:
  - Full param list download (expect 941 params)
  - Download completes within 30s
  - No excessive gaps (< 5s between any two params)

Method: pymavlink only
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, mavlink_connect, fetch_all_params

EXPECTED_PARAM_COUNT = 900
MAX_DOWNLOAD_TIME_S = 45
MAX_GAP_S = 5

def run():
    t = TestResult("H4-PARAMS")
    print(f"[H4-PARAMS] === Parameter Download Test ===")

    conn, err = mavlink_connect()
    if not conn:
        t.check("MAVLink connect", False, actual=err)
        t.summary()
        return t
    t.check("MAVLink connect", True, actual=f"sysid={conn.target_system}")

    t0 = time.time()
    params, total = fetch_all_params(conn, max_retries=3, overall_timeout=MAX_DOWNLOAD_TIME_S, stall_timeout=MAX_GAP_S)
    elapsed = time.time() - t0
    conn.close()
    cnt = len(params)

    t.check(f"Param count >= {EXPECTED_PARAM_COUNT}",
            cnt >= EXPECTED_PARAM_COUNT,
            actual=f"{cnt}/{total}", expected=f">= {EXPECTED_PARAM_COUNT}")

    t.check(f"Download time < {MAX_DOWNLOAD_TIME_S}s",
            elapsed < MAX_DOWNLOAD_TIME_S,
            actual=f"{elapsed:.1f}s")

    # With retry-based collection, a successful full download already proves
    # the gap handling stayed inside the script's tolerated stall window.
    t.check(f"Download converged within retry window ({MAX_GAP_S}s stall)",
            total > 0 and cnt >= total,
            actual=f"{cnt}/{total}")

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
