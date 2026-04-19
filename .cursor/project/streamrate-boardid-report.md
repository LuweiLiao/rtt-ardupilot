# MAVLink Stream Rate & BOARD_ID Verification Report

**Date:** 2026-04-19 08:12 CST
**USB:** /dev/ttyACM1 @ 115200 baud
**Firmware:** pogo-apm (ArduPilot)

## Test 1: BOARD_ID / System Info

| Field | Value |
|---|---|
| System ID | 1 |
| Component ID | 1 (autopilot) |
| Autopilot type | 2 (fixed-wing) |
| Flight mode | STABILIZE |

## Test 2: Message Rates (default streams)

Rates measured over 5 seconds after sending `MAV_CMD_SET_MESSAGE_INTERVAL` for ATTITUDE to 1Hz:

| Message | Count | Rate (/s) |
|---|---|---|
| AHRS | 16 | 3.2 |
| AHRS2 | 65 | 13.0 |
| ATTITUDE | 8 | 1.6 |
| EKF_STATUS_REPORT | 16 | 3.2 |
| GLOBAL_POSITION_INT | 16 | 3.2 |
| GPS_RAW_INT | 16 | 3.2 |
| HEARTBEAT | 15 | 3.0 |
| MEMINFO | 16 | 3.2 |
| MISSION_CURRENT | 16 | 3.2 |
| NAV_CONTROLLER_OUTPUT | 16 | 3.2 |
| POWER_STATUS | 16 | 3.2 |
| RAW_IMU | 33 | 6.6 |
| RC_CHANNELS | 16 | 3.2 |
| SCALED_IMU2 | 33 | 6.6 |
| SCALED_IMU3 | 33 | 6.6 |
| SCALED_PRESSURE | 33 | 6.6 |
| SERVO_OUTPUT_RAW | 16 | 3.2 |
| SYS_STATUS | 24 | 4.8 |
| SYSTEM_TIME | 16 | 3.2 |
| TERRAIN_REPORT | 16 | 3.2 |
| VFR_HUD | 33 | 6.6 |
| VIBRATION | 16 | 3.2 |

**SET_MESSAGE_INTERVAL result:** ATTITUDE rate ≈ 1.6/s (target 1.0/s). The command was accepted (COMMAND_ACK received) but rate is higher than requested — likely due to default stream also sending ATTITUDE at ~3.2/s. The `SET_MESSAGE_INTERVAL` may not fully override `STREAM_RATE` on this firmware build, or the measurement window caught residual stream messages.

## Test 3: Peripheral AUTODETECT (STATUSTEXT)

Messages captured during 15s post-heartbeat:

```
Initialising ArduPilot
Loop: 25/s timeavail=0
Initialising ArduPilot
EKF3 IMU1 forced reset
EKF3 IMU1 initialised
Loop: 22/s timeavail=144
Initialising ArduPilot
EKF3 IMU1 tilt alignment complete
Loop: 19/s timeavail=145
Initialising ArduPilot
```

**Observations:**
- Multiple "Initialising ArduPilot" messages suggest reboot loops or restart sequences
- EKF3 IMU1 successfully initialised and completed tilt alignment
- Loop rates: 25 → 22 → 19 /s (decreasing trend, may indicate CPU load)
- No GPS/range finder/baro peripheral detection messages observed
- No "PreArm: ..." or sensor failure messages
