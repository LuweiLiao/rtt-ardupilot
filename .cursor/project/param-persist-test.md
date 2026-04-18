# Parameter Persistence Cross-Reboot Test

**Date:** 2026-04-19  
**USB:** /dev/ttyACM1  
**Status:** ❌ INCOMPLETE - Board crashed during test

## Results

### Step 1: Read current SYSID_THISMAV
- **Method 1 (PARAM_REQUEST_LIST):** SYSID_THISMAV not found after 25s timeout
- **Method 2 (PARAM_REQUEST_READ):** Returns wrong parameter (RTL_ALT instead of SYSID_THISMAV) — **PARAM_REQUEST_READ bug confirmed** (returns params in alphabetical order regardless of request)

### Step 2: Set SYSID_THISMAV to 42
- `param_set_send` for SYSID_THISMAV=42 executed
- Many PARAM_VALUE messages received (ack stream of ATC_* params)
- Could not verify if set actually worked (PARAM_REQUEST_READ bug prevents readback)

### Step 3: Board State After Set
- Subsequent connections show **sys=0, comp=0** (normally sys=1, comp=1)
- Board appears to be in bootloader or crashed state
- Likely cause: `param_set_send` triggered a crash (possibly FRAM write issue)

### Step 4: Reboot / Verify After Reboot
- **Could not complete** — board unresponsive (sys=0)
- OpenOCD reset failed: chip ID `0x5ba02477` not recognized by OpenOCD 0.12.0
  - `stm32h7x.cfg` expects `0x6ba02477`
  - `stm32h7x_dual_bank.cfg` expects `0x6ba02477`
  - Need physical reset or OpenOCD upgrade

## Issues Found

1. **PARAM_REQUEST_READ bug**: Returns params alphabetically, not the requested param. This is a firmware-side MAVLink handling bug.
2. **Possible crash on param_set**: Setting a parameter caused the board to enter sys=0 state. May be related to FRAM write triggering a fault.
3. **OpenOCD too old**: v0.12.0 doesn't support STM32H743 rev V (ID 0x5ba02477)

## Next Steps
1. Physically reset the board (button or power cycle)
2. Fix PARAM_REQUEST_READ bug in firmware MAVLink handler
3. Debug param_set crash — check if FRAM write is causing a hard fault
4. Upgrade OpenOCD to a version that supports this chip revision
