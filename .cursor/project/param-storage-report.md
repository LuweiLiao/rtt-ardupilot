# Parameter Storage Verification Report

**Date:** 2026-04-19
**Board:** CUAV V5 (RTT HAL)
**USB:** /dev/ttyACM1

## Summary

| Test | Result |
|------|--------|
| Heartbeat | ✅ OK |
| `param_request_list` | ✅ 177 parameters received |
| `param_request_read` (by name) | ❌ No response |
| `param_request_read` (by index) | ❌ No response (even index 0) |
| Parameter persistence | ⚠️ Cannot test (set requires read to verify) |

## Analysis

### Storage Backend
- Code path: FRAM → Flash → Stub (RAM)
- `HAL_WITH_RAMTRON=1` is defined
- Boot log should print one of: "RTT Storage: FRAM/Flash/STUB backend"
- **Need to check `rtt_dbg_setup_stage` via debug probe to confirm which backend is active**

### Root Cause of `param_request_read` Failure
The parameter list stream works (177 params), but individual `param_request_read` messages get no response. This is **not a storage issue** — it's a MAVLink message handling issue.

Possible causes:
1. `PARAM_REQUEST_READ` message handler not implemented or broken in the RTT HAL's GCS MAVLink implementation
2. The handler may be looking at the wrong component ID or system ID
3. The message may be consumed but the response path is broken

### Key Observation
- `param_request_list` works → parameter storage IS functional, params are loaded
- `param_request_read` fails → MAVLink command dispatch issue, not storage issue
- This means Mission Planner / QGC would work for full parameter download but individual reads (used by scripts, calibration, etc.) would fail

## Recommendations
1. Check `GCS_MAVLink::handle_param_request_read()` in the RTT port
2. Verify MAVLink routing — the USB CDC may route to a different component than expected
3. Check if `param_set` works (it may work even if read doesn't, since set uses a different code path)
4. Use `rtt_dbg_setup_stage` via SWD to confirm storage backend at runtime

## Storage Code Reference
- `libraries/AP_HAL_RTT/Storage.cpp` — FRAM/Flash/Stub backend chain
- Backend selection: line 28-48 (FRAM init → Flash fallback → Stub last resort)
