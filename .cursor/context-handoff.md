# Context Handoff

## User Goal
- Port and debug `RTT + CUAV V5`.
- Need end-to-end compile, flash, debug, and `MAVROS` full-flow validation.
- Current focus is continued root-cause analysis of sensor bring-up failure, especially barometer.

## Work Completed
- Read project logs under `.cursor/` and used them as bring-up context.
- Fixed USB build regression:
  - Added OTG FS pins to `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat`.
  - Updated `libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py` so USB MSP also initializes/deinitializes non-DM/DP OTG pins.
- Fixed hwdef BSP deploy regression:
  - updated `Tools/scripts/rtt_bsp_deploy.py`
  - `cuav_v5` now copies the checked-in board `stm32f7xx_hal_msp.c`
  - generated `rt_pin_config.c` only replaces MSP when the target has no board-specific MSP file
- Rebuilt successfully with:
  - `python3 -m SCons --target=cuav-v5 -j16`
- Flashed successfully with:
  - `openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_deploy/cuav_v5/rtthread.bin verify 0x08008000 reset exit"`
- Verified MAVLink heartbeat on `/dev/ttyACM1`.
- Ran `MAVROS` full-flow smoke test:
  - connection OK
  - parameter pull OK
  - mission pull OK
  - set_mode OK
  - persistent FCU error: `Baro: unable to initialise driver`

## Sensor/Baro Debug Timeline
- Confirmed `HAL_BARO_PROBE_LIST` includes:
  - `AP_Baro_MS5611::probe(*this, hal.spi->get_device("ms5611"))`
- GDB showed:
  - `AP::baro()._num_drivers = 0`
  - `AP::baro()._num_sensors = 0`
- Confirmed runtime device registry is present:
  - `rt_device_find("spi41") != nullptr`
- Breakpoint at `AP_Baro_MS56XX::_init()` hit normally.
- `AP_Baro_MS56XX::_read_prom_word()` returned `0` for first words.
- Initially found `_dev->transfer(..., recv_len=2)` returned `false`.
- Patched `libraries/AP_HAL_RTT/SPIDevice.cpp`:
  - changed `send+recv` path from chained TX-only/RX-only messages to one full-duplex message with dummy bytes
  - rationale: avoid RT-Thread STM32 SPI `RX-only` polling path during register reads
- Rebuilt/flashed again.

## Current Root-Cause Findings
- After the SPI transaction patch, `PROM` reads still fail.
- `_dev->transfer(..., recv_len=2)` still returns `false`.
- GDB on the failing path shows:
  - failure occurs in `HAL_SPI_TransmitReceive(..., Size=3)`
  - returned status is `HAL_TIMEOUT`
- Critical finding:
  - immediately after `RTT::SPIDevice::set_speed()` returns `true` for `ms5611`, `SPI4->CR1/CR2/SR` are still all `0`
  - so configure path reports success, but SPI peripheral is not actually configured
- Even stronger evidence:
  - final linked symbol `HAL_SPI_MspInit` in `build/rtt_deploy/cuav_v5/rt-thread.elf` is the weak symbol from:
    - `packages/stm32f7_hal_driver-latest/Src/stm32f7xx_hal_spi.o`
  - not the board-specific implementation from:
    - `board/CubeMX_Config/Src/stm32f7xx_hal_msp.c`
- Symbol evidence:
  - `arm-none-eabi-nm -C build/rtt_deploy/cuav_v5/rt-thread.elf | rg "HAL_SPI_MspInit|HAL_UART_MspInit|HAL_PCD_MspInit"`
  - result:
    - `HAL_PCD_MspInit` is strong (`T`)
    - `HAL_SPI_MspInit` is weak (`W`)
    - `HAL_UART_MspInit` is weak (`W`)
- Object evidence:
  - `arm-none-eabi-nm -C build/rtt_deploy/cuav_v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.o | rg "HAL_.*Msp"`
  - result only exports:
    - `HAL_MspInit`
    - `HAL_PCD_MspInit`
    - `HAL_PCD_MspDeInit`
  - does **not** export:
    - `HAL_SPI_MspInit`
    - `HAL_UART_MspInit`
- This mismatch explains why SPI peripheral init never really takes effect during HAL bring-up.

## Resolved Root Cause
- The hwdef deployment path was overwriting `build/rtt_deploy/cuav_v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.c`
  with generated `_hwdef_gen/rt_pin_config.c`.
- That generated file only contained:
  - `HAL_MspInit`
  - `HAL_PCD_MspInit`
  - `HAL_PCD_MspDeInit`
- It did **not** contain:
  - `HAL_SPI_MspInit`
  - `HAL_UART_MspInit`
  - `HAL_SD_MspInit`
- Result: final ELF linked HAL weak defaults for SPI/UART MSP, so SPI4 never got real GPIO/peripheral MSP setup.
- After fixing deploy logic and rebuilding:
  - `arm-none-eabi-nm -C build/rtt_deploy/cuav_v5/rt-thread.elf | rg "HAL_PCD_MspInit|HAL_SPI_MspInit|HAL_UART_MspInit|HAL_SD_MspInit"`
  - now shows all four as strong (`T`)
  - `build/rtt_deploy/cuav_v5/rtthread.map` resolves them from:
    - `board/CubeMX_Config/Src/stm32f7xx_hal_msp.o`

## Post-Fix Validation
- Rebuilt successfully again:
  - ROM `1412580 B / 2016 KB (68.43%)`
  - RAM `142528 B / 512 KB (27.19%)`
- Reflashed successfully again:
  - `Programming Finished`
  - `Verified OK`
- GDB runtime validation after boot:
  - `rtt_dbg_main_loop_entry_called = 0x12345678`
  - `AP::baro()._num_drivers = 1`
  - `AP::baro()._num_sensors = 1`
- `pymavlink` validation on `/dev/ttyACM1`:
  - heartbeat OK
  - received normal STATUSTEXT such as:
    - `ArduPilot Ready`
    - `EKF3 IMU0 initialised`
    - `EKF3 IMU1 initialised`
  - no longer saw:
    - `Baro: unable to initialise driver`
- `MAVROS` validation:
  - `/mavros/state`: `connected: true`, `mode: STABILIZE`
  - `/mavros/param/pull`: `success=True, param_received=942`
  - `/mavros/imu/data_raw`: has valid gyro/accel data
  - `/mavros/vfr_hud`: has data
  - `/mavros/global_position/raw/fix`: topic present, still `status: -1` (no GPS fix)
  - `MAVROS` logs no longer show baro init failure

## Current Status
- Original barometer bring-up failure is fixed.
- Traditional quad (`Quad X`) configuration has now been applied and verified:
  - `FRAME_CLASS = 1`
  - `FRAME_TYPE = 1`
  - after reboot, MAVROS reports:
    - `FCU: Frame: QUAD/X`
- `FRAME_CLASS`/`FRAME_TYPE` is no longer the active blocker.
- Current remaining PreArm/Arm blockers observed after the frame fix:
  - `PreArm: RC not found`
  - `PreArm: Gyros not healthy`
  - `PreArm: Compass not healthy`
  - `PreArm: Main loop slow (100Hz < 400Hz)`
- MAVROS control-path validation after frame fix:
  - `/mavros/state`: `connected: true`, `mode: STABILIZE`, `armed: false`
  - `/mavros/set_mode`: succeeded with `mode_sent=True`
  - `/mavros/cmd/arming`: failed with `success=False, result=4`
- Additional observations after frame fix:
  - MAVROS sometimes terminates with serial EOF / `Resource deadlock avoided` after FCU reboot or disconnect/reconnect
  - one later `STATUSTEXT` also showed:
    - `AP_Logger: stuck thread ()`

## Most Likely Next Step
- Investigate the new real runtime blockers after `Quad X` is configured:
  1. Why the main loop is only around `100Hz` instead of the expected `400Hz`
  2. Why gyro and compass health are not stable in ArduPilot startup / PreArm checks
  3. Whether `RC not found` should be solved by real SBUS input or temporarily bypassed for bench testing
  4. Investigate `AP_Logger: stuck thread ()`
- After these are addressed, rerun:
  - arming test
  - RC input validation
  - mission/param writeback tests
  - GPS integration once hardware is present

## Modified Files So Far
- `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat`
- `libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py`
- `libraries/AP_HAL_RTT/SPIDevice.cpp`
- `Tools/scripts/rtt_bsp_deploy.py`

## Notes For Continuation
- `/dev/ttyACM1` is the working MAVLink endpoint.
- `rtthread.bin` flash address is `0x08008000`.
- `Baro` is no longer the active blocker.
- Current investigation has advanced to system configuration / PreArm readiness.

## Latest Runtime Findings
- The earlier `AP_AHRS::reset()` hang at stage `1181` was traced to short STM32F7 SPI transfers falling back to HAL polling paths.
- Two mitigations have now been applied:
  - `libraries/AP_HAL_RTT/SPIDevice.cpp`
    - TX-only SPI writes use a throwaway RX buffer so they stay on the full-duplex path.
  - `modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/drivers/drv_spi.c`
    - `DMA_TRANS_MIN_LEN` for STM32F7 was reduced from `16` to `1` so short transfers use DMA/LLD instead of HAL polling.
- Observed behavior after those changes:
  - the old hard stop in `HAL_SPI_Transmit()` / `HAL_SPI_TransmitReceive()` is no longer the only visible failure mode
  - boot can sit transiently at stage `104`, then later still reach stage `1181`
  - when halted later, IMU worker threads are active (for example in `AP_InertialSensor_BMI055::read_fifo_gyro()`), so the new front line is likely inside `dcm.reset()` waiting on INS frontend progress rather than a pure SPI deadlock
- Additional instrumentation has now been added to `libraries/AP_AHRS/AP_AHRS_DCM.cpp`:
  - `11811` before first `_ins.get_accel()`
  - `11812` before `_ins.wait_for_sample()`
  - `11813` after `_ins.wait_for_sample()`
  - `11814` after `_ins.update()`
  - `11815` after refreshed `_ins.get_accel()`
- Immediate next step:
  - flash the new DCM-instrumented build
  - read `rtt_dbg_setup_stage`
  - determine whether the hang is specifically in `wait_for_sample()` or after it

## Modified Files So Far
- `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat`
- `libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py`
- `libraries/AP_HAL_RTT/SPIDevice.cpp`
- `libraries/AP_AHRS/AP_AHRS_DCM.cpp`
- `Tools/scripts/rtt_bsp_deploy.py`
- `modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/drivers/drv_spi.c`
