# Current Focus

## 当前阶段
`CUAV v5` 的 RT-Thread ArduPilot 基线已稳定运行并支持干净 clone 一条命令编译，SPI LLD DMA + DTCM 堆修复已完成。当前进入"飞行级能力补齐 + ChibiOS 深度对齐 + 性能优化"阶段。

## 当前主线目标
- 对齐度约 75-80%
- 优先补齐影响实际飞行的缺失项：RCOutput（PWM/DShot）、RCInput（SBUS）、安全机制
- 验证 SD 卡日志实际写入与回读
- 验证 SPI LLD 长时间稳定性（>5分钟已验证，需>1小时）
- 在不破坏已验证基线的前提下逐步对齐

## 当前优先级
1. SD 卡文件系统验证（实际插卡后挂载、日志写入、回读）
2. RCOutput 完善（至少确保基础 PWM 可控电调）
3. RCInput 完善（SBUS 路径验证）
4. GPIO `usb_connected()` 正确实现
5. Scheduler 线程模型进一步对齐（UART 独立线程等）
6. Util 补充（toneAlarm、safety、watchdog）
7. 架构清理与多板模板

## 已完成的关键里程碑
- [x] AnalogIn `_timer_tick()` 挂入 Scheduler 1kHz 路径
- [x] 校准功能修复（BinarySemaphore/Semaphore 语义对齐）
- [x] SD 卡支持实现（SDMMC1 + ELM-FAT + DFS）
- [x] UART7 调试串口（PE8 TX / PF6 RX / 115200 / msh console）
- [x] Logging PreArm 消除（Filesystem + MAVLink 双后端）
- [x] SPI LLD DMA 驱动（消除 ISR busy-wait）
- [x] DTCM 堆修复（HEAP_BEGIN 移到 SRAM1）
- [x] 干净 clone 一条命令编译支持
- [x] newlib polyfill（asprintf/vasprintf/memmem）

## 暂不处理
- CAN 总线（当前硬件不需要）
- IOMCU（需要独立固件支持）
- 第二块板 bring-up（先完成 CUAV V5 对齐）

## 推荐起手动作
先读 `status.md` 确认基线。新环境可直接 `git clone --recursive` + `python3 -m SCons --target=cuav-v5 -j16` 编译。调试用 UART7（Windows COM33 / 115200）。
