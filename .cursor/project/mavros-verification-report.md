# MAVROS 2 Verification Report — RTT CUAV V5

**Date:** 2026-04-19 02:08 CST  
**Setup:** ROS 2 Jazzy + MAVROS 2.14.0, serial:///dev/ttyACM1:115200  
**Firmware:** RTT CUAV V5 (latest build, aligned hwdef, 937 msgs, 25 types confirmed working)

## Summary

MAVROS **successfully connected** to the RTT CUAV V5 firmware. The FCU identified as **ArduPilot** with **AHRS: DCM active**. However, the connection lasted only ~10 seconds before the firmware crashed (serial EOF), causing MAVROS to abort.

## Connection & Initialization

| Item | Status |
|------|--------|
| MAVLink heartbeat | ✅ Received, ArduPilot detected |
| AHRS | ✅ DCM active |
| Barometer | ✅ Calibration complete |
| IMU | ✅ Raw IMU message used |
| RC_CHANNELS | ✅ Message detected |
| GPS | ⚠️ No GPS fix (expected, no antenna) |
| AUTOPILOT_VERSION | ❌ Not supported — FCU doesn't respond to this command |
| Parameter fetch | ❌ Timeout — FCU doesn't respond to PARAM_REQUEST_LIST |

## Data Observed (before crash)

### `/mavros/imu/data` — ✅ Working
Orientation quaternion data received successfully:
```
orientation: {x: 0.0008, y: 0.0075, z: -0.7074, w: -0.7068}
```

### `/mavros/imu/data_raw` — ⚠️ No data
MAVROS warned: *"IMU: linear acceleration on RAW_IMU known on APM only. IMU: ~imu/data_raw stores unscaled raw acceleration report."*  
This is a MAVROS/ArduPilot compatibility note, not a firmware bug.

### `/mavros/state` — ✅ Received (once)
```
connected: false, armed: false, mode: '', system_status: 0
```
State was received once but shows `connected: false` — likely because MAVROS transitioned to disconnected state.

### `/mavros/battery` — ❌ No data
### `/mavros/rc/in` — ❌ No data (expected, no RC)
### `/mavros/global_position/raw/fix` — ❌ No data (expected, no GPS)
### `/mavros/vfr_hud` — ❌ No data
### `/mavros/home_position/home` — ❌ No data
### `/mavros/diagnostic` — ❌ No data

## ROS Topics Registered

54 topics registered under `/mavros/` before crash, including:
- GPS, global_position, home_position ✅
- IMU, attitude ✅
- battery, esc_status, esc_telemetry ✅
- gimbal_control, rangefinder ✅
- waypoint, trajectory, terrain ✅
- adsb, cam_imu_sync, camera ✅

## ROS Services

Only **parameter services** were available (describe/get/set parameters). **No command/action services** were registered, likely because MAVROS couldn't complete initialization (version check failed, connection lost).

## Critical Issues

### 1. 🔴 Firmware Crash (Serial EOF)
~10 seconds after MAVROS connected, the serial port returned EOF and the firmware crashed. This is the **most critical issue**. Likely causes:
- Stack overflow or memory corruption triggered by MAVLink command handling
- `COMMAND_LONG` (cmd 520 = REQUEST_AUTOPILOT_CAPABILITIES) causing a crash
- PARAM_REQUEST_LIST timeout could indicate the firmware's MAVLink command handler is crashing

### 2. 🟡 AUTOPILOT_VERSION Not Supported
The firmware doesn't respond to `AUTOPILOT_VERSION` requests. This is expected for a port that hasn't implemented this message. MAVROS falls back to default capabilities, which limits some functionality.

### 3. 🟡 PARAM_REQUEST_LIST Timeout
MAVROS couldn't fetch parameters. This could be because:
- The parameter subsystem isn't fully working over MAVLink yet
- The firmware crashed before responding

### 4. 🟡 FCU Time Wrong
MAVROS reported wrong FCU time, suggesting `SYSTEM_TIME` message may not be properly implemented.

## Comparison: RTT CUAV V5 vs ChibiOS fmuv5

| Feature | ChibiOS fmuv5 | RTT CUAV V5 |
|---------|---------------|-------------|
| Heartbeat | ✅ | ✅ |
| IMU data | ✅ | ✅ |
| AHRS/Attitude | ✅ | ✅ |
| Barometer | ✅ | ✅ |
| AUTOPILOT_VERSION | ✅ | ❌ Not implemented |
| PARAM_REQUEST_LIST | ✅ | ❌ Timeout/crash |
| GPS (with antenna) | ✅ | Not tested |
| RC input | ✅ | Not tested |
| Connection stability | ✅ Stable | ❌ Crashes after ~10s |
| Battery status | ✅ | Not tested |
| Home position | ✅ | ❌ Not sent |
| VFR HUD | ✅ | ❌ Not sent |
| System time | ✅ | ❌ Wrong |

## Recommendations

1. **Fix the crash first** — The serial EOF after ~10s is a showstopper. Investigate:
   - Add debug prints in `GCS_MAVLINK::handle_message()` for COMMAND_LONG
   - Check stack usage when processing MAVLink messages
   - Verify `handle_command_request_autopilot_capabilities()` doesn't crash
   
2. **Implement AUTOPILOT_VERSION** (msg #337) — This is required by MAVROS for full initialization

3. **Implement PARAM_REQUEST_LIST** — Essential for MAVROS parameter management

4. **Fix SYSTEM_TIME** — Send proper `time_unix_usec` in `SYSTEM_TIME` message

5. **After crash is fixed**, re-run verification with GPS antenna and RC to test those data paths
