# Parameter Persistence Cross-Reboot Verification

**Date:** 2026-04-19
**USB:** /dev/ttyACM1
**Firmware:** ArduCopter V4.7.0-dev, CUAVv5-RTT

## Result: ❌ FAIL

## Observations

1. **SYSID_THISMAV not found in param list** — despite 798 PARAM_VALUE messages received, SYSID_THISMAV was not among them
2. **Firmware continuously rebooting** — "Initialising ArduPilot" appeared ~8 times during a single param list request, suggesting the firmware is crash-looping
3. **param_set / param_request_read got no PARAM_VALUE response** — the firmware doesn't respond to individual param requests
4. **STATUSTEXT shows:** "CUAVv5-RTT 36363539 34385103 0039002B" and "Frame: UNSUPPORTED"
5. **Loop rate stable at ~243/s** when running, with timeavail=186us

## Root Cause (suspected)

The firmware is crash-looping, likely due to FRAM initialization or storage issues. The param subsystem may not be fully initializing before the reboot cycle. The 798 params received are from whichever partial init completes before the next crash.

## Next Steps

- Check if FRAM init is causing the boot loop (add debug prints or check via SWV/UART debug)
- Verify FRAM chip is responding correctly at boot (SPI bus health)
- Check if the Storage backend is timing out during AP_Param::load_all()
- Consider: is there a watchdog timeout during FRAM read that triggers reboot?
