# AP_HAL_RTT 实现状态

> 基线日期：2026-03-21  
> 目标板：CUAV V5 (STM32F767, 2MB Flash, 512KB RAM)  
> 验证固件：ArduCopter V4.7.0-dev on RT-Thread

## 已验证工作的模块

| 模块 | 文件 | 说明 |
|------|------|------|
| SPI | `SPIDevice.cpp`, `SPIDeviceManager.cpp` | SPI1(IMU) + SPI4(Baro)，0% 失败率。使用 `rt_mutex` 递归锁解决重入问题 |
| UART/USB CDC | `UARTDriver.cpp` | CherryUSB CDC ACM，21 种 MAVLink 消息类型 |
| Scheduler | `Scheduler.cpp` | timer/io/storage 三线程，`thread_create` 支持 |
| Semaphores | `Semaphores.cpp` | `rt_mutex_t`（递归互斥锁）+ `rt_sem_t`（二值信号量） |
| DeviceBus | `DeviceBus.cpp` | `register_periodic_callback` 通过 RT-Thread 线程实现 |
| Util | `Util.cpp` | `get_micros64()` 用 DWT CYCCNT；`available_memory()` 用 `rt_memory_info()` |
| system | `system.cpp` | `panic`、`millis`、`micros64` |

## 传感器状态

| 传感器 | SPI 总线 | CS 引脚 | 状态 |
|--------|----------|---------|------|
| ICM20689 (IMU) | SPI1 | PF2 | 工作 |
| ICM20602 (IMU) | SPI1 | PF3 | 未测试（板上可能无此芯片） |
| ICM42688 (IMU) | SPI1 | PF11 | WHO_AM_I=0xFF（板上可能无此芯片） |
| BMI055 (IMU) | SPI1 | PF4/PG10 | 未测试 |
| MS5611 (Baro) | SPI4 | PF10 | 工作，PROM CRC 校验通过 |

## MAVLink 验证结果

实机链路与前文一致：**飞控 USB 在 Windows（COM）**；若代码与 pymavlink 在 **WSL2**，用 **`scripts/README_MAVLINK_WSL2.md`** 中的 **`mavlink_serial_tcp_bridge.py` + `wsl2_mavlink_smoke_test.py`** 做 TCP 桥接即可闭环，不依赖 WSL 内 `/dev/ttyACM*`。

通过 Windows pymavlink (COM36) 测试：

- HEARTBEAT: type=2 (QUADROTOR), status=3 (STANDBY)
- 21 种消息类型: ATTITUDE, RAW_IMU, SCALED_PRESSURE, SYS_STATUS, VFR_HUD, VIBRATION 等
- 参数读取: 574/943 个参数
- STATUSTEXT: "ArduPilot Ready", "AHRS: DCM active", "PreArm: Motors: Check frame class and type"

## 桩实现（未完成）

| 模块 | 文件 | 说明 |
|------|------|------|
| I2C | `I2CDevice.h` | 空实现，无硬件验证 |
| GPIO | `GPIO.h` | 空实现 |
| RCInput | `RCInput.cpp` | 框架存在但无 PPM/SBUS 输入 |
| RCOutput | `RCOutput.h` | 无 PWM 输出 |
| Storage | `Storage.h` | 仅 RAM，无 Flash/FRAM 持久化 |
| AnalogIn | `AnalogIn.h` | 无 ADC |
| Flash | `Flash.h` | 桩 |

## 已知问题

1. **地面站拉参数很慢 / 像读不出来** — **GCS_Param::queued_param_send()** 用 `bw_in_bytes_per_second()`；AP 默认 **5760 B/s** 会严重限流。**RTT**：`hwdef.dat` 里 **SERIAL_ORDER 首项为 OTG** 时，`rtt_hwdef.py` 生成 **`HAL_RTT_SERIAL0_OTG 1`**，`UARTDriver` 对 **serial0** 强制 **`200*1024`**（与 ChibiOS `is_usb` 一致）；设备名 **`usb-acm0`** 的字符串判断仍保留作双保险。另 **`have_flow_control()`** 在 `MAVLINK_COMM_0` 上依赖 **`GPIO::usb_connected()`**（USB CDC 主链路应返回 **true**）；保持 **`RT_TICK_PER_SECOND=1000`**。详见 `scripts/README_DEBUG.md`。
2. **参数/存储回归风险** — `HAL_WITH_RAMTRON=1` 会强制走 FRAM；BSP 未就绪时应在 `hwdef.dat` 保持 `define HAL_WITH_RAMTRON 0`，且勿在 `scons_ardupilot_sources.py` 的 F7 分支重新加入该宏。
3. **EKF 精度与内存** — `NavEKF3` 启动前检查 `available_memory() >= sizeof(NavEKF3_core)*N + 4096`。CUAV v5 在 `hwdef.dat` 中默认 `HAL_WITH_EKF_DOUBLE 0`（单精度 EKF，与多数 F4 板一致）以降低 `NavEKF3_core` 体积、避免 "EKF3 not enough memory"。若堆裕量充足可改为 `1`。
4. **USB CDC via usbipd** — WSL 下双向通信可能不稳定；Windows 直连 MissionPlanner 正常。
5. **ap_config.h** — SConscript 在编译时确保 `#include "hwdef.h"`，属预期流程。

## 构建系统

- Waf (`./waf configure --board rtt_cuav_v5 && ./waf copter`) 用于生成 hwdef.h 和 ArduPilot 源文件列表
- SCons (`python3 -m SCons --v=ArduCopter --target=cuav_v5`) 执行 RT-Thread BSP + ArduPilot 联合编译
- OpenOCD + ST-Link V2 用于烧录和调试
