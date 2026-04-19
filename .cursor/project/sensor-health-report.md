# Sensor Health Report

**Date:** 2026-04-19 08:13 CST
**USB:** /dev/ttyACM1
**Config:** IOMCU disabled, RC input on FMU (SERIAL1)

## Results

| Sensor | Status | Details |
|--------|--------|---------|
| RC Input | ⚠️ No signal | RC_CHANNELS received but all channels = 0, rssi=255 (no receiver connected or wrong protocol) |
| EKF | ⚠️ Not healthy | No EKF_STATUS_REPORT — EKF not initialized (likely due to missing sensor data) |
| SYS_STATUS | ✅ OK | sensors=0x5370fc0f enabled=0x51609c0f health=0x46509c0b, errors=0 |
| GPS | ❌ No data | No GPS_RAW_INT or GPS2_RAW received |
| Baro | ❌ No data | No SCALED_PRESSURE received |

## Analysis

- **RC:** MAVLink RC_CHANNELS message is being streamed (rssi=255 indicates no RC receiver detected). Need to verify SERIAL1 protocol (SERIAL1_PROTOCOL) matches the receiver type.
- **EKF:** Not producing EKF_STATUS_REPORT — expected since GPS and baro are missing. EKF needs at minimum IMU + baro or GPS.
- **GPS:** No GPS data. Check GPS type config and UART connections.
- **Baro:** No barometer data. May need to verify I2C/SPI baro driver is loaded.

## SYS_STATUS Sensor Bitmask

- `present=0x5370fc0f`: Sensors detected by system
- `enabled=0x51609c0f`: Sensors enabled
- `health=0x46509c0b`: Some sensors unhealthy (present & enabled bits differ from health)
- Zero error counts across all banks

## Recommendations

1. Connect an RC receiver to SERIAL1 and verify `SERIAL1_PROTOCOL` setting
2. Check GPS hardware connection and `GPS_TYPE` parameter
3. Verify barometer driver is compiled in and I2C bus is functional
4. Once sensors are available, re-check EKF health
