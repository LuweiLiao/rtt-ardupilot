# MAVROS 2 Re-verification Report

**Date:** 2026-04-19 04:56 CST
**USB:** /dev/ttyACM1
**Baud rate:** 921600
**ROS:** Jazzy
**Command:** `ros2 launch mavros apm.launch fcu_url:=serial:///dev/ttyACM1:921600`

## Result: ❌ FAILED — Param timeout + heartbeat lost

### Timeline

| Time (s) | Event |
|----------|-------|
| ~0.5 | Plugins initialized (param, imu, etc.) |
| ~1.0 | **HEARTBEAT received** — FCU: ArduPilot ✅ |
| ~1.0 | WARN: No GPS fix |
| ~2.0 | WARN: Unexpected command 520 |
| ~6.0 | WARN: RTT too high for timesync (128.75 ms) |
| ~11 | **WARN: Param request list timeout**, retries 2→1→0 |
| ~15 | CMD ack timeout (command 410) |
| ~16 | **Lost connection** — HEARTBEAT timed out |
| ~18-20 | Mission WP timeouts, then ERROR: WP timed out |

### Key Issues

1. **Param fetch fails** — 3 retries all timeout (same as 115200)
2. **Heartbeat unstable** — connects initially, drops after ~15s
3. **High RTT** — 128.75 ms (921600 should be much faster)
4. **No topics published** — after MAVROS exits, only `/parameter_events` and `/rosout` remain

### Analysis

Baud rate increase (115200→921600) did **not** fix the param timeout issue. This suggests the problem is **not link bandwidth** but likely:

- **ArduPilot side:** Param fetch handler may not be responding (check if `SERIALn_PROTOCOL` matches, or if the port is overloaded with other streams)
- **Stream rate conflict:** At 921600 the FCU may be streaming high-rate data (IMU, GPS) that saturates the link, leaving no room for param responses
- **Firmware issue:** The custom build may have a bug in param handling over this serial port

### Recommendations

1. Check `SERIAL0_PROTOCOL` / `SERIAL1_PROTOCOL` — ensure it's set to 1 (MAVLink 1) or 2 (MAVLink 2)
2. Reduce stream rates: `SERIALn_BAUD 921600` + `STREAMRATES` to minimum
3. Test with USB directly from Mission Planner first to confirm params are fetchable
4. Try `MAV_0_CONFIG` matching the actual port number
5. Check if `SYSID_MYGCS` and `SYSID` match

### Comparison with Previous Test (115200)

| Metric | 115200 | 921600 |
|--------|--------|--------|
| Heartbeat | Connected | Connected (then lost) |
| Param fetch | Timeout | Timeout |
| Connection stability | Dropped ~15s | Dropped ~16s |
| RTT | — | 128.75 ms |

**Conclusion:** Baud rate alone does not resolve the issue. Root cause is on the ArduPilot/firmware side.
