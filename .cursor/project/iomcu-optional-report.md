# IOMCU Optional Configuration Report

## Current State

IOMCU is enabled in `hwdef.dat` line 141:
```
IOMCU_UART UART8
```

`rtt_hwdef.py` detects `IOMCU_UART` and auto-generates:
- `HAL_WITH_IO_MCU 1`
- `HAL_UART_IOMCU_IDX N`
- `HAL_HAVE_SERVO_VOLTAGE 1`
- `AP_FEATURE_SBUS_OUT 1`

When IOMCU hardware is absent, `AP_IOMCU::event_failed()` retries 50 times (~50s), prints DEV_PRINTF, then silently gives up. No STATUSTEXT is sent.

## Option A: Disable IOMCU via hwdef (Recommended)

**How:** Comment out `IOMCU_UART UART8` in `hwdef.dat`:

```
# IOMCU_UART UART8
```

**Effect:** `rtt_hwdef.py` sets `HAL_WITH_IO_MCU 0`, no IOMCU thread created, no retries, no delay.

**What breaks:**

| Feature | Impact | Severity |
|---------|--------|----------|
| RC input via IOMCU | Lost (RC comes through IOMCU on CUAV V5) | ⚠️ Medium — if no RC, fine |
| Safety switch | `HAL_HAVE_SAFETY_SWITCH 1` still set in hwdef (line 210), but `Util::safety_switch_state()` already has `#if HAL_WITH_IO_MCU` fallback to RCOutput local safety state | ✅ Safe — code handles this |
| LED control | Lost | ✅ Low |
| AUX PWM outputs | Lost (only FMU main outputs work) | ⚠️ Medium — depends on usage |
| SERVO_VOLTAGE | Lost | ✅ Low |

**Safety concern:** `HAL_HAVE_SAFETY_SWITCH` is hardcoded in hwdef.dat (line 210), independent of IOMCU. The code in `Util.cpp` has proper `#if HAL_WITH_IO_MCU` guards with fallback. The safety state will read from local RCOutput instead of IOMCU. **Safe.**

## Option B: Keep IOMCU enabled + Add STATUSTEXT

**Problem:** Rules prohibit modifying `AP_IOMCU.cpp`. The STATUSTEXT warning would need to go there (in `event_failed`).

**Alternative:** Add STATUSTEXT in `HAL_RTT_Class::init()` after IOMCU init, checking if IOMCU is responding. But this is fragile.

**Verdict:** Not recommended given the constraint.

## Recommendation

**Disable IOMCU via hwdef** — comment out `IOMCU_UART UART8`. This is:

1. Clean — one line change, no code modification
2. Safe — safety switch fallback exists in Util.cpp
3. Fast — eliminates 50s boot delay from retry loop
4. Reversible — uncomment to re-enable

If STATUSTEXT warning is still desired for when IOMCU is enabled but fails, that requires modifying `AP_IOMCU.cpp` (against current rules).

## Files to Modify

- `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` — comment out line 141 (`IOMCU_UART UART8`)

## Optional Cleanup

Can also remove lines 236-239 (ROMFS io_firmware) to save flash space, but not required.
