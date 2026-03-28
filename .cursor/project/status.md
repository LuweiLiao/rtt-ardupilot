# AP_HAL_RTT 当前状态

> 基线：`CUAV v5` / `STM32F767` / `ArduCopter V4.7.0-dev on RT-Thread 5.3.0`
> 最后更新：2026-03-28（SPI LLD + DTCM 堆修复 + 编译链路简化后）

## 当前稳定成立的事实

- `bootloader -> RTT app -> scheduler -> main -> hal.run()` 主链路已跑通
- `CUAV v5` 上的 USB CDC / MAVLink 已在 Windows 和 WSL2 侧验证到可用
- SPI 传感器链已带起，`BMI055`（IMU）与 `MS5611`（Baro）形成有效数据路径
- **SPI LLD**（`drv_spi_lld.c/h`）：低层 DMA 驱动替代 STM32 HAL 在 ISR 内的 busy-wait；在线程上下文 poll `BSY`，消除 ISR 内阻塞，CPU 负载进一步改善
- I2C3 软件驱动已初始化，`IST8310`（磁力计）已识别
- 已观测到 23 种 MAVLink 消息类型（HEARTBEAT、ATTITUDE、RAW_IMU、SYS_STATUS 等）
- MAVLink 参数下载：**943 全部完成**（FTP 协议，快速）
- 主循环频率：**~400Hz 稳定运行**
- CPU 负载：**平均 72%**（SPI DMA / LLD 启用后，数据流开启时 min 17% / max 100%）
- **DTCM 与 DMA**：STM32F767 的 DTCM（0x20000000–0x2001FFFF）不可被 DMA 访问；`HEAP_BEGIN` 从 `&__bss_end`（DTCM 内）改为 `0x20020000`（SRAM1 起始），消除运行约 30s 必现 HardFault
- 参数持久化：Flash 后端验证闭环（on-chip Flash page 10-11, 0x08180000）
- D-Cache 已启用、编译优化 `-O2 -Os`、无阻塞性 `rt_kprintf`
- **信号量语义已对齐 ChibiOS**：`take(0)` / `wait(0)` = 非阻塞；`take_blocking()` / `wait_blocking()` = 永久阻塞
- **校准功能已验证**：加速度计校准、Level 校准均可正常完成，地面站不再冻结
- **SD 卡文件系统支持已实现**：SDMMC1 驱动 + ELM-FAT + DFS 挂载 `/sd`，ArduPilot 目录已创建
- **Filesystem Logging 已启用**：`HAL_LOGGING_FILESYSTEM_ENABLED=1` + `AP_FILESYSTEM_POSIX_ENABLED=1` + `statfs` 支持
- **MAVLink Logging 已禁用**（`HAL_LOGGING_MAVLINK_ENABLED=0`），避免无客户端时 PreArm 失败
- **UART7 调试串口已启用**：PE8=TX / PF6=RX / 115200，RT-Thread msh 控制台已切到 UART7
- AnalogIn `_timer_tick()` 已挂入 Scheduler 1kHz 路径
- **USB CDC 重连稳定**：2s 间隔 5/5 成功（含数据流），纯心跳 13/15 成功
- **CherryUSB DTR 处理已完善**：DTR set/clear 回调中正确 reset TX 状态 + `usbd_ep_recover_stuck`
- **Logging PreArm 已解除**：只剩 "PreArm: RC not found"（预期行为）
- **构建链路**：支持 `git clone --recursive` 后一条命令全量编译；`.gitmodules` 中 rt-thread 已指向 pogo fork；`rtt_bsp_deploy.py` 自动下载 packages；`SConscript` 自动创建 `ap_config.h`
- **newlib polyfill**：`rtt_libc_compat.c` 提供 `asprintf` / `vasprintf` / `memmem`；`hwdef.h` 中含对应 `extern "C"` 声明

## 线程模型（实测 12 线程）

| 线程 | 优先级 | 栈大小 | 用途 |
|------|--------|--------|------|
| main | 8 | 32KB | ArduPilot 主循环 |
| ap_timer | 4 | 8KB | 定时器回调 |
| ap_io | 16 | 8KB | IO 线程 |
| storage | 18 | 2KB | 存储后端 |
| log_io | 15 | 2KB | 日志 IO |
| dcb0-3 | 10 | 8KB×4 | SPI/I2C 延迟回调（SPI LLD 调用链需更大栈） |
| tshell | 20 | 4KB | msh 控制台（UART7） |
| mmcsd_detect | 22 | 1KB | SD 卡热插拔检测 |
| tidle0 | 31 | 256B | 空闲线程 |

## 内存使用

- RAM：约 113KB / 512KB（21.58%）
- ROM：约 1254KB / 2016KB（60.73%）

## 设备列表（实测）

- `uart7` — 控制台（ref=2）
- `uart3` — 原控制台（ref=3）
- `usb-acm0` — USB CDC MAVLink（ref=3）
- `sd0` / `sd` — SD 卡 Block Device
- `spi1` / `spi2` / `spi4` — SPI 总线
- `spi11-15` / `spi21` / `spi41` — SPI 子设备
- `i2c3` — 软件 I2C 总线
- `pin` — GPIO

## 与 ChibiOS 对齐度

整体约 **75-80%**（以 fmuv5/CUAV V5 上 ChibiOS HAL 能力为尺度）

### 已对齐部分
- 启动链路（boot → main → scheduler）
- USB CDC / MAVLink 通信（含断线重连）
- CPU 负载与主循环节拍（check_called_boost）
- SPI 传感器数据（IMU/Baro）
- I2C 传感器（IST8310 磁力计）
- 参数持久化（Flash）
- 时间 API（micros/millis via DWT+tick）
- Flash 读写 API
- 信号量语义（take/wait 非阻塞 vs 阻塞）
- 校准流程（AccelCal、Level Cal）
- SD 卡文件系统（SDMMC1 + ELM-FAT + POSIX statfs）
- Filesystem Logging（已解除 PreArm: Logging failed）
- AnalogIn _timer_tick() 1kHz 采样
- UART7 调试串口 + msh shell
- GPIO `usb_connected()` 检测真实 USB configured 状态
- USB CDC DTR 处理（CherryUSB ep_recover_stuck）

### 未对齐部分
- RCOutput 仅基础 PWM（无 DShot/IOMCU/安全开关）
- RCInput 仅串口协议路径（无脉冲捕获）
- Util 覆盖面不足（缺 toneAlarm/safety/watchdog 等）
- Scheduler 缺 monitor/rcout/rcin 独立线程
- I2C 无 clear_bus 恢复
- 看门狗/安全机制空桩
- CAN / IOMCU 未实现
- **SPI DMA 已启用**：SPI1 使用 LL DMA 驱动（`drv_spi_lld`），消除 ISR busy-wait；HAL 路径保留给未启用 LLD 的总线

## 当前已知限制

- CPU 负载偏高（SCHED_LOOP_RATE=400Hz 下 avg 72%，启动时峰值 100%）；降到 300Hz 可进一步缓解
- WSL2 `usbipd` 下 USB CDC 在 1.5s 间隔高流量重连时偶尔失败（usbipd 吞吐瓶颈）；真实物理 USB 或 Windows COM 更稳定
- `mmcsd_detect` 线程栈使用率 90%，接近溢出
- `EKF3` 仍存在内存压力，允许回退到 `DCM active`
- CH343 USB-TTL 在 WSL2 内核无驱动（VID:PID 1A86:55D3 不匹配 ch341.ko），需从 Windows 端读串口
- `RCOutput`、`RCInput`、部分 `GPIO/I2C` 仍未形成实体飞行级能力
