#!/usr/bin/env python3
"""
H3: Serial / USB CDC Test

Validates:
  - USB CDC device enumerated
  - MAVLink heartbeat received
  - Message rate > 5 msg/s
  - Multiple message types flowing

Method: pymavlink only (avoids port contention with pyserial)
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from hal_test_lib import TestResult, find_cdc_port, mavlink_connect, mavlink_request_streams

def run():
    t = TestResult("H3-SERIAL")
    print(f"[H3-SERIAL] === Serial / USB CDC Test ===")

    port = find_cdc_port()
    t.check("CDC device enumerated", port is not None,
            actual=port or "not found")
    if not port:
        t.summary()
        return t

    conn, err = mavlink_connect(timeout=10)
    if not conn:
        t.check("MAVLink connect", False, actual=err)
        t.summary()
        return t
    t.check("Heartbeat received", True,
            actual=f"sysid={conn.target_system}")

    mavlink_request_streams(conn, rate_hz=10)
    time.sleep(1)

    msg_count = 0
    msg_types = set()
    t0 = time.time()
    while time.time() - t0 < 3:
        msg = conn.recv_msg()
        if msg:
            msg_count += 1
            mtype = msg.get_type()
            if mtype != "BAD_DATA":
                msg_types.add(mtype)
        else:
            time.sleep(0.001)

    rate = msg_count / 3.0
    conn.close()

    t.check("Message rate > 5 msg/s", rate > 5,
            actual=f"{rate:.1f} msg/s")
    t.check("Multiple message types (> 3)", len(msg_types) > 3,
            actual=f"{len(msg_types)} types: {', '.join(sorted(msg_types)[:8])}")

    t.summary()
    return t

if __name__ == "__main__":
    r = run()
    sys.exit(0 if r.summary() else 1)
