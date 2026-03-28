# AP_HAL_RTT 实现状态

> 基线日期：2026-03-28  
> 目标板：CUAV V5 (STM32F767, 2MB Flash, 512KB RAM)  
> 验证固件：ArduCopter V4.7.0-dev on RT-Thread

## 已验证工作的模块

| 模块 | 文件 | 说明 |
|------|------|------|
| SPI | `SPIDevice.cpp`, `SPIDeviceManager.cpp` | SPI1(IMU) + SPI4(Baro)，SPI1 使用 LL DMA 驱动 (drv_spi_lld)。DMA buffer 分配在 SRAM1 |
| UART/USB CDC | `UARTDriver.cpp` | CherryUSB CDC ACM + UART7 msh 控制台，23 种 MAVLink 消息类型 |
| Scheduler | `Scheduler.cpp` | timer/io/storage/log_io 多线程，dcb 线程 8KB 栈 |
| Semaphores | `Semaphores.cpp` | `rt_mutex_t`（递归互斥锁）+ `rt_sem_t`（二值信号量） |
| DeviceBus | `DeviceBus.cpp` | `register_periodic_callback` 通过 RT-Thread 线程实现 |
| Util | `Util.cpp` | `get_micros64()` 用 DWT CYCCNT；`available_memory()` 用 `rt_memory_info()` |
| system | `system.cpp` | `panic`、`millis`、`micros64` |
| Storage | `Storage.cpp` | Flash 后端验证闭环（on-chip Flash page 10-11） |
| AnalogIn | `AnalogIn.cpp` | `_timer_tick()` 挂入 Scheduler 1kHz 路径 |
| I2C | `I2CDevice.cpp` | I2C3 软件驱动，IST8310 磁力计已识别 |
| GPIO | `GPIO.cpp` | `usb_connected()` 检测真实 USB configured 状态 |
| Flash | `Flash.cpp` | STM32 Flash 驱动，参数持久化 |

## 传感器状态

| 传感器 | SPI 总线 | CS 引脚 | 状态 |
|--------|----------|---------|------|
| ICM20689 (IMU) | SPI1 | PF2 | 工作 |
| ICM20602 (IMU) | SPI1 | PF3 | 未测试（板上可能无此芯片） |
| ICM42688 (IMU) | SPI1 | PF11 | WHO_AM_I=0xFF（板上可能无此芯片） |
| BMI055 (IMU) | SPI1 | PF4/PG10 | 工作 |
| IST8310 (磁力计) | I2C3 | — | 工作 |
| MS5611 (Baro) | SPI4 | PF10 | 工作，PROM CRC 校验通过 |

## MAVLink 验证结果

实机链路与前文一致：**飞控 USB 在 Windows（COM）**；若代码与 pymavlink 在 **WSL2**，用 **`scripts/README_MAVLINK_WSL2.md`** 中的 **`mavlink_serial_tcp_bridge.py` + `wsl2_mavlink_smoke_test.py`** 做 TCP 桥接即可闭环，不依赖 WSL 内 `/dev/ttyACM*`。

通过 Windows pymavlink (COM36) 测试：

- HEARTBEAT: type=2 (QUADROTOR), status=3 (STANDBY)
- 23 种消息类型: ATTITUDE, RAW_IMU, SCALED_PRESSURE, SYS_STATUS, VFR_HUD, VIBRATION 等
- 参数读取: 943/943 个参数 (FTP 全量下载)
- STATUSTEXT: "ArduPilot Ready", "AHRS: DCM active", "PreArm: Motors: Check frame class and type"

## 桩实现（未完成）

下表沿用原汇总格式：I2C、GPIO、Storage、AnalogIn、Flash 等已落地；**RCInput**、**RCOutput** 仍为桩。

| 模块 | 文件 | 说明 |
|------|------|------|
| I2C | `I2CDevice.cpp` | I2C3 软件驱动已实现，IST8310 已识别 |
| GPIO | `GPIO.cpp` | `usb_connected()` 已实现 |
| RCInput | `RCInput.cpp` | 框架存在但无 PPM/SBUS 输入 |
| RCOutput | `RCOutput.h` | 无 PWM 输出 |
| Storage | `Storage.cpp` | Flash 后端已验证 |
| AnalogIn | `AnalogIn.cpp` | `_timer_tick()` 已挂入 1kHz |
| Flash | `Flash.cpp` | STM32 Flash 驱动已实现 |

## 已知问题

1. **地面站参数读取** — 已通过 FTP 协议优化，943 参数全量下载正常。
2. **DTCM 与 DMA** — DTCM 仅 CPU 可访问、DMA 不可达；已在 SRAM1 分配 DMA buffer / 相关栈等（见 `docs/AP_HAL_RTT_ARCHITECTURE.md` 内存布局）。属已修复类约束，仍须在新增 DMA 路径时遵守。
3. **SPI LLD 覆盖范围** — 自研 LL DMA 目前仅覆盖 **SPI1**；其余 SPI 总线仍走 HAL DMA 路径。
4. **参数/存储回归风险** — `HAL_WITH_RAMTRON=1` 会强制走 FRAM；BSP 未就绪时应在 `hwdef.dat` 保持 `define HAL_WITH_RAMTRON 0`，且勿在 `scons_ardupilot_sources.py` 的 F7 分支重新加入该宏。
5. **EKF 精度与内存** — `NavEKF3` 启动前检查 `available_memory() >= sizeof(NavEKF3_core)*N + 4096`。CUAV v5 在 `hwdef.dat` 中默认 `HAL_WITH_EKF_DOUBLE 0`（单精度 EKF，与多数 F4 板一致）以降低 `NavEKF3_core` 体积、避免 "EKF3 not enough memory"。若堆裕量充足可改为 `1`。
6. **USB CDC via usbipd** — WSL 下双向通信可能不稳定；Windows 直连 MissionPlanner 正常。
7. **ap_config.h** — SConscript 在编译时确保 `#include "hwdef.h"`，属预期流程。

## 构建系统

- **干净 clone 首次编译**：`git clone --recursive -b staging/pogo-rtt <repo-url> && cd pogo-apm && python3 -m SCons --target=cuav-v5 -j16`（子模块与 BSP **packages** 会在构建过程中按需自动下载；**ap_config.h** 由 SCons 流程自动生成，无需手抄。）
- Waf (`./waf configure --board rtt_cuav_v5 && ./waf copter`) 用于生成 hwdef.h 和 ArduPilot 源文件列表
- SCons（根目录 `SConstruct`）：`python3 -m SCons --v=ArduCopter --target=cuav-v5`（或 `cuav_v5`）执行 RT-Thread BSP + ArduPilot 联合编译；首次构建会拉取 **packages**，并生成 **ap_config.h**
- OpenOCD + ST-Link V2 用于烧录和调试
