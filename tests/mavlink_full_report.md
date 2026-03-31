# MAVLink Full Hardware Report

Date: 2026-03-30
Board: `CUAV v5`
Firmware: `RT-Thread ArduCopter`
Primary link: `/dev/ttyACM1`

## Passed Coverage

- Link and heartbeat:
  - `test_h3_serial.py` PASS
  - `H7-CPU` PASS
  - `test_mavlink_stress.py` S2 reconnect `5/5 PASS`
- Parameters:
  - `test_h4_params.py` PASS after adding retry-based collection
  - `test_mavlink_stress.py` S1 full download `942/942 x5 PASS`
  - `test_integration.py` parameter read/write PASS
- MAVFTP:
  - `tests/test_mavftp.py` `6/6 PASS`
- AHRS / command / mode / resource stability:
  - `test_h6_ahrs.py` PASS
  - `test_integration.py` `6/6 PASS`
- Long-duration stream stability:
  - `test_mavlink_stress.py` S3 `10 min / 17992 msgs / 30.0 msg-s / max_gap 4.13s PASS`
- Mission protocol:
  - `tests/test_mission_protocol.py` PASS
  - Covered `MISSION_CLEAR_ALL -> MISSION_COUNT -> REQUEST/ITEM -> ACK -> REQUEST_LIST -> download -> CLEAR_ALL`
- Lua scripting smoke:
  - `tests/test_lua_hello.py` PASS after fixing EEXIST handling, script path, and wait window

## Still Failing

- Sensor stream:
  - `test_h5_sensors.py` FAIL
  - `RAW_IMU.xacc/yacc/zacc` remain `0`, while compass, barometer, AHRS and EKF outputs are valid
- Default / requested MAVLink rates:
  - `tests/test_mavlink_rates.py` FAIL
  - Default rates remain low and `REQUEST_DATA_STREAM` does not lift `ATTITUDE/RAW_IMU/SYS_STATUS` to expected values
- Explicit interval regression in current state:
  - `tests/test_set_message_interval.py` FAIL
  - `COMMAND_ACK` is accepted, but measured output rate remains very low under the current board state
- Large log download:
  - `tests/test_log_download.py` FAIL on latest large log
  - Download stopped at `35053290 / 37040128` bytes

## Classified As Test Issues Resolved This Round

- `test_h4_params.py` originally produced false negatives because it used one-shot parameter collection without retry
- `tests/test_lua_hello.py` originally failed because:
  - existing directory creation was treated as fatal
  - upload path used `/APM/scripts_rtt` instead of `/APM/scripts`
  - wait window for `hello, world` was too short

## Environment Notes

- `ttyACM1` is the reliable MAVLink CDC port for this round
- `ttyACM0` opened but did not reliably return UART7 `msh` output, so UART7 was not used as a gating signal for MAVLink regression
- A running `QGroundControl` process initially held `ttyACM1`; once it exited, automated CDC testing proceeded normally

## Recommended Next Debug Targets

- `RAW_IMU` population path versus AHRS/EKF source data
- state-dependent behavior of default stream rates, `REQUEST_DATA_STREAM`, and `SET_MESSAGE_INTERVAL`
- retry/resume handling for very large `LOG_REQUEST_DATA` transfers
