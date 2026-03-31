# Milestone Plan

## 当前里程碑视图

| ID | 里程碑 | 状态 | 完成判据 |
|---|---|---|---|
| `M0` | 构建与烧录链建立 | **已完成** | 可生成固件、可烧录、可连 OpenOCD/GDB |
| `M1` | boot → scheduler → main 主链跑通 | **已完成** | bootloader → app → scheduler → hal.run() 稳定成立 |
| `M2` | USB CDC / MAVLink 基础通信打通 | **已完成** | Windows 侧出现 ArduPilot 串口并可见 HEARTBEAT |
| `M3` | 传感器链打通 | **已完成** | BMI055（IMU）+ MS5611（Baro）+ IST8310（Compass）数据有效 |
| `M4` | 参数与状态消息形成可用基线 | **已完成** | 943 参数 FTP 全量下载，关键消息类型齐备 |
| `M5` | CUAV v5 稳定开发基线 | **已完成** | 主循环 400Hz / CPU 6.5% / 参数持久化 / Flow Control / SPI LLD DMA（消除 ISR busy-wait）/ DTCM 堆修复（HEAP_BEGIN → SRAM1） |
| `M6` | ChibiOS 功能对齐 Phase 1 | **进行中 (~90%)** | 补齐 AnalogIn、Logging、校准、SD卡、调试串口、基础飞行能力 |
| `M7` | ChibiOS 功能对齐 Phase 2 | 待开始 | RC I/O、Util 完善、安全机制 |
| `M8` | 数据驱动化与第二块板模板 | 待开始 | hwdef.dat 生成路径完整，出现第二块板模板 |

## 当前所处阶段

- 当前主线位于 **M6**，对齐度约 ~90%
- M5 在 2026-03-24 随 check_called_boost 修复、Flow Control 优化达成

## M6 进度详情

- [x] AnalogIn `_timer_tick()` 挂入 Scheduler 1kHz 路径
- [x] 信号量语义对齐（take(0)/wait(0) = 非阻塞）
- [x] 校准功能修复（AccelCal、Level Cal 正常）
- [x] SD 卡文件系统（SDMMC1 + ELM-FAT + DFS + POSIX Backend）
- [x] Filesystem + MAVLink 双 Logging 后端
- [x] UART7 调试串口 + msh console
- [x] SPI LLD DMA 驱动（SPI1 LL 路径，消除 ISR busy-wait）
- [x] DTCM 堆修复（HEAP_BEGIN → SRAM1 0x20020000）
- [x] 干净 clone 编译链路（.gitmodules + auto packages + ap_config.h）
- [x] newlib polyfill（rtt_libc_compat.c）
- [x] DeviceBus dcb 线程栈 4KB → 8KB
- [x] GPIO `usb_connected()` 正确实现
- [x] MAVLink 消息频率修复（call_delay_cb + delay callback 白名单 + USB CDC TX buffer）
- [x] IWDG 独立看门狗骨架（暂 `#if 0` 禁用，需 GDB 调试）
- [ ] SD 卡实际插卡验证（挂载 + 日志写入 + 回读）
- [ ] RCOutput 完善（至少确保基础 PWM 可控电调）
- [ ] RCInput 完善（SBUS 路径验证）
- [ ] Scheduler 线程模型进一步对齐

## 升级到 M7 前需要确认

- SD 卡日志可写入可回读
- 至少一种 RC 输入方式可工作
- PWM 输出可控电调
- PreArm 检查项不再阻断基本操作

## M7 关注点

- [ ] Util 补充（toneAlarm、safety_switch_state、watchdog）
- [ ] 看门狗/安全机制实现
- [ ] I2C clear_bus 恢复
- [ ] CAN 总线评估
- [ ] 架构清理（HAL/BSP/hwdef 分层）
