# AP_HAL_RTT ↔ ChibiOS 对齐状态追踪

> 最后更新: 2026-04-12
> 目标板: CUAV V5 (STM32F767)
> 分支: staging/pogo-rtt
> 注意: 此文件为专项补充档案，主状态入口为 `project/status.md`

## 总体进度: 约 97% 对齐

## 已对齐（功能可用）
- 启动链路 (boot → main → scheduler)
- USB CDC / MAVLink 通信（参数 FTP 下载、姿态数据流）
- CPU 负载（DWT idle hook 真实 ~1%，check_called_boost 对齐）
- SPI 传感器（BMI055 IMU + MS5611 Baro + SPI LLD DMA）
- I2C 传感器（IST8310 磁力计）
- 参数持久化（Flash 后端）
- 时间 API / Flash API / 直接寄存器级 Flash 操作
- 信号量语义（take(0)=非阻塞，take_blocking()=永久阻塞，与 ChibiOS 一致）
- 校准流程（AccelCal、Level Cal）
- SD 卡文件系统（SDMMC1 + ELM-FAT + DFS POSIX，非阻塞后台挂载）
- Filesystem Logging + MAVLink Logging 双后端
- AnalogIn _timer_tick() 1kHz 采样
- UART7 调试串口（msh console）
- Scheduler delay() persistent_data.scheduler_task 赋值
- MAVLink 消息频率（三层修复，默认 96.6 msgs/s，ATTITUDE 13.3Hz）
- Scheduler 完整线程模型（monitor/timer/rcout/rcin/uart/io/storage）
- RCOutput PWM（TIM1/4/12 全运行，8 通道初始化）
- RCInput 独立线程 + SBUS 串口协议路径
- RAW_IMU 加速度修复（主 IMU 索引而非硬编码 0）
- GPIO usb_connected() 基于实际 USB configured 状态
- DeviceBus "每总线一线程" 架构（对齐 ChibiOS）
- IO 线程（SD retry_mount + stack_check + reboot 前清理）
- system.cpp（millis16/micros16、panic、Fault Handler）
- Filesystem statfs 支持
- UARTDriver 改进（USB write fail count 修正、CDC max_chunks 8）
- USB CDC 重连稳定（DTR 处理完善）
- IWDG 看门狗骨架（暂禁用）
- 构建链路（干净 clone 一条命令编译）

## 部分对齐（框架在位，待实机验证）
- PWM 输出（TIM1/4/12 8 通道已初始化，需实机电调验证）
- RC 输入（SBUS 串口路径已实现，需实机验证）
- SD 卡（驱动/FS 已就位，但 SDMMC2 硬件层无卡响应，需 ChibiOS 对比）

## 未对齐（硬件依赖或低优先级）
- DShot 协议（需 DMA + 定时器捕获）
- IOMCU 独立固件（UART8 驱动链路完整，阻塞于 ROMFS — io_firmware.bin 无法加载）
- 安全开关（force_safety_on 恒 false）
- IWDG 实际启用
- CAN 总线
- Shared_DMA / bounce_buffer（由 BSP 驱动层管理）
- UARTDriver DMA TX/RX
