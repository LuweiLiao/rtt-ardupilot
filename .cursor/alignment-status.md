# AP_HAL_RTT ↔ ChibiOS 对齐状态追踪

> 最后更新: 2026-03-24
> 目标板: CUAV V5 (STM32F767)
> 分支: staging/pogo-rtt
> 注意: 此文件为专项补充档案，主状态入口为 `project/status.md`

## 总体进度: 约 70-75% 对齐

## 已对齐（功能可用）
- 启动链路 (boot → main → scheduler)
- USB CDC / MAVLink 通信（参数 FTP 下载、姿态数据流）
- CPU 负载（check_called_boost 对齐，6.5%）
- SPI 传感器（BMI055 IMU + MS5611 Baro）
- I2C 传感器（IST8310 磁力计）
- 参数持久化（Flash 后端）
- 时间 API / Flash API
- 信号量语义（take(0)=非阻塞，take_blocking()=永久阻塞，与 ChibiOS 一致）
- 校准流程（AccelCal、Level Cal）
- SD 卡文件系统（SDMMC1 + ELM-FAT + DFS POSIX）
- Filesystem Logging + MAVLink Logging 双后端
- AnalogIn _timer_tick() 1kHz 采样
- UART7 调试串口（msh console）
- Scheduler delay() persistent_data.scheduler_task 赋值

## 部分对齐
- PWM 输出（基础 PWM，无 DShot / 无 IOMCU）
- RC 输入（SBUS 串口路径，无脉冲捕获）
- GPIO（基础功能可用，usb_connected() 恒 true）
- I2C（驱动可用，无 clear_bus 恢复）

## 未对齐
- DShot 协议
- IOMCU 独立固件
- 安全开关（force_safety_on 恒 false）
- 看门狗 / watchdog_reset
- 通知系统（toneAlarm / LED）
- CAN 总线
- Scheduler rcout/rcin/monitor 独立线程
- Util 完整覆盖（flash_bootloader, random 等）
