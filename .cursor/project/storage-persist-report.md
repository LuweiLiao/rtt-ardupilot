# Parameter Storage Persistence Test Report

**Date:** 2026-04-19  
**Board:** CUAV V5 (AP_HAL_RTT)  
**USB:** /dev/ttyACM1

## Summary

**Parameters do NOT persist across reboot.** The storage backend is falling through to **Stub** (no-op), meaning no parameters are loaded or saved.

## Findings

### 1. MAVLink Parameter Read — FAILED
- `SYSID_THISMAV`: no response
- `AHRS_EKF_TYPE`: no response
- `LOG_DISARMED`: no response
- `SERIAL0_PROTOCOL`: no response
- Heartbeat is received (sys=1, comp=0), but **zero PARAM_VALUE messages** are returned.
- This confirms no parameters are loaded into memory at boot.

### 2. FRAM Configuration — Present
```
hwdef.dat:187  # CUAV V5 has FM25V02A FRAM on SPI2
hwdef.dat:188  define HAL_WITH_RAMTRON 1
hwdef.dat:250  # ---- SPI2 - FRAM ----
```
FRAM is enabled in the board definition. The FM25V02A should be on SPI2.

### 3. Storage Initialization Code Flow (Storage.cpp)
```
Line 24: rtt_dbg_setup_stage = 500  # _storage_open entered
Line 29: rtt_dbg_setup_stage = 501  # trying FRAM
Line 30: _fram.init() && _fram.read(...)  → fails
Line 38: rtt_dbg_setup_stage = 502  # trying Flash
Line 46: rtt_dbg_setup_stage = 503  # using Stub  ← ends up here
```
The code correctly tries FRAM → Flash → Stub. The fact that no params are available means both FRAM and Flash init failed.

### 4. STATUSTEXT Messages — No Storage Errors
```
EKF3 IMU0 tilt alignment complete
EKF3 IMU1 tilt alignment complete
Loop: 113/s timeavail=0
Initialising ArduPilot
Loop: 146/s timeavail=186
EKF3 IMU0 forced reset
EKF3 IMU0 initialised
Initialising ArduPilot
Loop: 175/s timeavail=62
EKF3 IMU0 tilt alignment complete
PreArm: Motors: Check frame class and type
Initialising ArduPilot
```
No explicit storage error messages in STATUSTEXT. The failure is silent.

## Root Cause Hypothesis

Most likely: **SPI2 FRAM driver not working** — either:
1. SPI2 bus not initialized/configured properly before Storage::init()
2. FRAM chip not responding (CS pin, wiring, or chip absent)
3. `_fram.init()` returns false silently without printing a diagnostic

## Recommended Debugging Steps

1. **Check `rtt_dbg_setup_stage`** via SWD/JTAG debugger — if it stops at 501, FRAM init failed
2. **Add debug prints** in `_fram.init()` and `_fram.read()` to see which step fails
3. **Probe SPI2 with oscilloscope/logic analyzer** during boot to verify CS, CLK, MOSI/MISO activity
4. **Check SPI2 pin configuration** in hwdef.dat matches CUAV V5 schematic
5. **Verify FRAM chip is actually present** on the board variant being used
