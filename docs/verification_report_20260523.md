# RTT 实体验证报告 v2 — 2026-05-23

## 概要
重新编译并烧录最新 RTT 固件（commit `67c9c10a8f`）到 CUAV V5，验证整体系统启动和 MAVLink 数据流。

## 固件信息
| 项目 | 值 |
|------|-----|
| 编译时间 | 2026-05-23 02:58 |
| Git commit | 67c9c10a8f [AUDIT] Storage: add audit report document |
| 目标 | cuav_v5 (STM32F767) |
| 机型 | ArduCopter |
| ROM 占用 | 1307128 bytes / 1504 KB (84.87%) |
| RAM 占用 | 285408 bytes / 512 KB (54.44%) |
| APP_DESCRIPTOR | ✅ crc1=0x089b8489, crc2=0x49156ecb, size=1312588, hash=0xc9c10a8f |
| Binary Integrity | ✅ Reset_Handler literal pool 验证通过 |

## 烧录结果
| 步骤 | 状态 | 说明 |
|------|------|------|
| 清理旧 OpenOCD | ✅ | 杀死 2 个残留进程 |
| ST-Link 检测 | ✅ | 0483:3748 在线 |
| 编译 | ✅ | scons 编译成功 |
| OpenOCD 启动 | ✅ | 4444 端口监听 |
| 扇区擦除 (1-11) | ✅ | 17.38s |
| Flash 写入 | ✅ | 1312588 bytes, 13.76s (93 KB/s) |
| 向量表验证 | ✅ | 0x08008000: 0x20005554 0x080ef44d |

## MCU 运行状态
| 检查项 | 结果 |
|--------|------|
| OpenOCD halt | ✅ PC=0x0810183e（应用区） |
| HardFault | ✅ 无（CFSR=0, HFSR=0） |
| USB CDC 枚举 | ✅ /dev/ttyACM1（03:01 新鲜时间戳） |

## MAVLink 验证

### HEARTBEAT
- **收到**: ✅
- type=2 (MAV_TYPE_QUADROTOR)
- autopilot=3 (MAV_AUTOPILOT_ARDUPILOT)
- base_mode=0x59

### 观测到的消息类型（18种）
HEARTBEAT, AHRS, ATTITUDE, GLOBAL_POSITION_INT, GPS_RAW_INT, MEMINFO, MISSION_CURRENT, RAW_IMU, RC_CHANNELS, SCALED_PRESSURE, SERVO_OUTPUT_RAW, STATUSTEXT, SYSTEM_TIME, TERRAIN_REPORT, VFR_HUD, VIBRATION, UNKNOWN_196093, BAD_DATA

### 传感器状态
| 传感器 | 状态 | 数据 |
|--------|------|------|
| 气压计 (MS5611) | ✅ 功能正常 | 1000.4hPa（合理大气压） |
| 气压计温度 | ❌ 异常 | 3372.0°C（传感器读值错误） |
| IMU1 (ICM20689) | ❌ 全零 | acc=(0,0,0) gyro=(0,0,0) |
| IMU2 (ICM20602) | ❌ 全零 | 未读到数据 |
| IMU3 (BMI055) | ❌ 全零 | 未读到数据 |

### 关键启动事件流
1. `Calibrating barometer` → `Barometer 1 calibration complete` ✅
2. 系统保持运行，主循环活跃，无 HardFault
3. 多条 MAVLink 消息持续输出

## 结论

**L0 级别验证通过** — 固件烧录正确、bootloader 跳转成功、RT-Thread 调度器运行、MAVLink 心跳稳定。系统在启动过程中正常运行了主循环，产生 18 种消息类型。

**IMU 全零问题（已知遗留问题）**：RAW_IMU x=y=z=0 且 gyro=0，说明 IMU 传感器（SPI1 总线上的 ICM20689/ICM20602/BMI055）未正确初始化或读取失败。这与前一验证会话（t_aaba4399/t_e0d08a37）中记录的问题一致，属于传感器驱动层的修复工作，不影响本次烧录验证的结论。

## 对比历史
| 验证会话 | 日期 | L0 状态 | 备注 |
|---------|------|---------|------|
| t_aaba4399 (P0-2) | 2026-05-22 | ✅ | 首次 L0 通过，DWC2 USB 复位残留 |
| t_e0d08a37 | 2026-05-22 | ✅ | SPI1 并发锁修复、MS5611 挂死 |
| t_8179f777 (本次) | 2026-05-23 | ✅ | 重新编译+烧录验证，IMU=0 未修复 |
