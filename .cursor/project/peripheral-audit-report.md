# Peripheral Configuration Audit: RTT vs ChibiOS (CUAV V5 / fmuv5)

**Date:** 2026-04-19  
**USB:** /dev/ttyACM1  
**Firmware:** AP_HAL_RTT cuav_v5

## 1. SPI1 MOSI Pin — ⚠️ Mismatch

| Signal | ChibiOS (fmuv5) | RTT (cuav_v5) |
|--------|-----------------|---------------|
| SPI1_SCK | PG11 | PG11 ✅ |
| SPI1_MISO | PA6 | PG9 ⚠️ |
| SPI1_MOSI | **PD7** | **PB5** ⚠️ |

**Issue:** Both MISO and MOSI pins differ between ChibiOS and RTT configs.  
- ChibiOS: MISO=PA6, MOSI=PD7  
- RTT: MISO=PG9, MOSI=PB5 (AF5)

**Impact:** If the CUAV V5 hardware uses the ChibiOS pinout, SPI1 devices (icm20689, icm20602, bmi055_g, bmi055_a) will fail to communicate. If the RTT config was derived from a different board variant, this needs verification against the CUAV V5 schematic.

**SPI1 device list** (identical in both configs):
- icm20689 (DEVID1), icm20602 (DEVID2), bmi055_g (DEVID3), bmi055_a (DEVID4)

## 2. Spektrum RC — ✅ Configured

Both configs match:
- `PE4 SPEKTRUM_PWR OUTPUT HIGH GPIO(73)`
- `HAL_SPEKTRUM_PWR_ENABLED 1`
- `HAL_GPIO_SPEKTRUM_PWR 73`

**MAVLink confirms:** `RC_CHANNELS` messages are being received (19 in 10s sample). ✅

## 3. GPS — ✅ Configured (but no GPS fix yet)

Both configs define GPS on:
- **GPS1:** USART1 (PB6 TX, PB7 RX)
- **GPS2:** UART4 (PD1 TX, PD0 RX)

**MAVLink confirms:** `GPS_RAW_INT` messages present (19 in 10s), but likely no satellite fix (no satellite count visible from this sample).

## 4. Compass/Mag — ✅ Configured

Both configs use IST8310:
- ChibiOS: `HAL_PROBE_EXTERNAL_I2C_COMPASSES`, probes IST8310 on both internal and external I2C at 0x0E, rotation `ROLL_180_YAW_90`
- RTT: `HAL_MAG_PROBE_LIST PROBE_MAG_I2C(IST8310, 0, 0x0E, false, ROTATION_ROLL_180_YAW_90)` — internal only (I2C3 bus 0), no external probe by default

**Difference:** RTT only probes internal compass; ChibiOS probes both internal and external. `HAL_PROBE_EXTERNAL_I2C_COMPASSES` is commented out in RTT config. Minor — external compass users would need to enable this.

## 5. Baro — ✅ Configured

Both configs match:
- MS5611 on SPI4 (PF10 CS), DEVID1, 20MHz
- RTT additionally has `HAL_BARO_ALLOW_INIT_NO_BARO 1` (allows boot without baro)

**MAVLink confirms:** `SCALED_PRESSURE` messages present (38 in 10s). ✅

## 6. Current MAVLink Status (10s sample)

24 message types, 691 total messages — indicates a healthy, running system.

| Category | Messages | Status |
|----------|----------|--------|
| IMU | RAW_IMU, SCALED_IMU2, SCALED_IMU3 | ✅ All 3 IMUs active |
| Attitude | AHRS, AHRS2, ATTITUDE | ✅ |
| Baro | SCALED_PRESSURE | ✅ |
| GPS | GPS_RAW_INT | ✅ (present, fix status TBD) |
| RC | RC_CHANNELS | ✅ |
| Servo | SERVO_OUTPUT_RAW | ✅ |
| EKF | EKF_STATUS_REPORT, GLOBAL_POSITION_INT | ✅ |
| System | HEARTBEAT, SYS_STATUS, MEMINFO, STATUSTEXT | ✅ |
| Navigation | MISSION_CURRENT, NAV_CONTROLLER_OUTPUT, VFR_HUD | ✅ |

## Summary

| Item | Status | Notes |
|------|--------|-------|
| SPI1 pins | ⚠️ **MISMATCH** | MOSI: PD7 vs PB5, MISO: PA6 vs PG9 — verify against schematic |
| Spektrum RC | ✅ | Working, RC_CHANNELS received |
| GPS | ✅ | Configured on USART1/UART4, MAVLink present |
| Compass | ✅ | IST8310 internal configured, external probe commented out in RTT |
| Baro | ✅ | MS5611 on SPI4, SCALED_PRESSURE received |
| Overall MAVLink | ✅ | 24 msg types, system healthy |

## Recommended Actions

1. **Priority 1:** Verify SPI1 MOSI/MISO pins against CUAV V5 schematic. The pin mismatch could be correct (different board variant) or a bug.
2. **Priority 2:** If external compass support is needed, uncomment `HAL_PROBE_EXTERNAL_I2C_COMPASSES` in RTT hwdef.
