### Issue: Main loop never runs [RESOLVED 2026-04-08]
- **Root cause**: Flash storage erase (`Flash::erasepage()`) held interrupts disabled for 2-4 seconds during 256KB sector erase, starving RTT scheduler
- **Fix**: Only disable interrupts around STRT register write, then poll BSY with periodic `rt_thread_yield()`
- **Also fixed**: Linker script ROM region now correctly reserves 512KB for storage (pages 10-11)

### Issue: App "returns to bootloader" in free-run [RESOLVED 2026-04-12]
- **Root cause**: ArduPilot bootloader waits 5 seconds (bootloader(5000)) for firmware upload before jump_to_app(). All previous tests only waited 1-3 seconds before halting.
- **Evidence**: DTCM marker at 0x200000F0 unchanged after 3s (= app not started yet), overwritten after 12s (= app running). After 60s: rtt_dbg_main_loop_iterations=11279, VTOR=0x08008000.
- **Conclusion**: App runs perfectly. No HardFault, no watchdog, no crash.

### IOMCU ✅ 已实机验证通过 (2026-04-12 16:30)
- **ROMFS pipeline**: `rtt_hwdef.py write_ROMFS()` → `embed.py create_embedded_h()` → `ap_romfs_embedded.h` 成功嵌入 `io_firmware.bin`
- **hwdef.dat**: `ROMFS io_firmware.bin Tools/IO_Firmware/iofirmware_lowpolh.bin`
- **hwdef.h**: `HAL_HAVE_AP_ROMFS_EMBEDDED_H=1`, `HAL_WITH_IO_MCU=1`
- **UART8 链路**: PE0/PE1 AF8 → BSP UART8 → UARTDriver idx2 → AP_IOMCU
- **MAVLink 验证结果**:
  - SYS_STATUS: `MOTOR_OUTPUTS` present + healthy (bit 15)
  - RC_CHANNELS: 19 条消息，chancount=0（无RC接收器连接，正常）
  - 传感器: 3D_GYRO/ACCEL present, ABS_PRESSURE present+healthy, 3D_MAG healthy
  - 主循环: ~305/s 稳定
  - 气压计: 校准完成
  - EKF3: 双IMU初始化成功，AHRS EKF3 active
  - PreArm: 仅 "Motors: Check frame class and type"（正常，未配置frame）
- **构建**: ROM 93.15%, RAM 34.11%

### Current Status (2026-04-12)
- **Main loop**: ~305/s 稳定运行
- **Boot sequence**: bootloader 5s wait → jump_to_app → app init → main loop
- **Flash layout**: 0x08000000=bootloader(16KB), 0x08008000=app(~1.4MB)
- **Build/flash**: scons → openocd program bootloader + app
- **Serial**: `/dev/serial/by-id/usb-ArduPilot_CUAVv5_RTT_RTTUSB0001-if00` @ 57600 baud

### Remaining port items
- [x] Verify MAVLink over USB CDC (serial0) — ✅ 23+ 消息类型已验证
- [x] Verify barometer (MS5611 on SPI4) — ✅ 校准完成
- [x] Verify compass (IST8310 on I2C3) — ✅ 3D_MAG healthy
- [x] ROMFS support for IOMCU firmware binary — ✅ io_firmware.bin 嵌入成功
- [ ] Verify RC input with actual RC receiver (SBUS via IOMCU)
- [ ] Verify servo output with actual ESC/servo (RCOut PWM)
- [ ] GPS (requires physical antenna connection)
- [ ] SD card logging (SDMMC2 无响应，硬件问题)
- [ ] Frame configuration + motor test
- [ ] Clean up debug tracking code from source files
- [ ] Remove AP_INERTIALSENSOR_ALLOW_NO_SENSORS define (after IMU verified with real sensor data)
