# CUAV V5 RT-Thread 移植 — 完整修复方案

**定稿日期:** 2026-04-18 21:15
**状态:** 待执行

---

## 一、当前状态

| 模块 | 状态 | 根因 |
|------|------|------|
| 编译 | ✅ 正常 | SCons + ARM GCC 10.2.1 |
| 烧录 | ✅ 正常 | OpenOCD 0.12.0 + ST-Link |
| USB MAVLink | ✅ 正常 | 59 msgs/15 types/5s |
| Bootloader | ✅ 正常 | 已重新烧录 |
| ADC/Battery | 🔴 V=0.000V | BATT_MONITOR=NONE（Storage 为空） |
| SPI 传感器 | 🔴 全部不工作 | hwdef 缺 SPI SCK/MISO/MOSI 引脚 |
| FRAM 参数存储 | 🔴 不可用 | SPI2 引脚缺失 |
| IOMCU | ⚠️ 容错处理 | 固件上传失败不 HardFault |
| RC SBUS | ⚠️ 未验证 | 修了 UART tick |
| SD 卡 | 🔴 不可用 | hwdef 缺 SDMMC 引脚 |
| PWM 舵机 | 🔴 不可用 | hwdef 缺 PWM 定义 |
| 安全开关 | 🔴 不可用 | hwdef 缺引脚 |
| 编码工具链 | ✅ 全部就绪 | CC/OpenCode/Codex(proxy) |
| 验证环境 | ✅ ROS 2 Jazzy + MAVROS 2.14.0 | PyMavlink 也可用 |

## 二、根因

RTT hwdef 从头手写，系统性缺失大量硬件引脚配置。不是代码 bug，是硬件描述不完整。
详细差异报告: `.cursor/project/hwdef-diff-report.md`

## 三、修复方案

### 阶段 1：SPI + 电源 + 存储（CRITICAL，基础）

**目标：让 FRAM、IMU、气压计、传感器供电活起来**

#### 1.1 补 SPI 总线引脚
参考 ChibiOS fmuv5 hwdef.dat，在 RTT hwdef.dat 中添加：

```
# SPI1 - IMU (PA5 SCK, PB5 MOSI, PG9 MISO)
PA5  SPI1_SCK  SPI1  AF5
PB5  SPI1_MOSI SPI1  AF5
PG9  SPI1_MISO SPI1  AF5

# SPI2 - FRAM (PI1 SCK, PI3 MOSI, PI2 MISO)
PI1  SPI2_SCK  SPI2  AF3
PI3  SPI2_MOSI SPI2  AF3
PI2  SPI2_MISO SPI2  AF3

# SPI4 - MS5611 Baro (PE12 SCK, PE14 MOSI, PE13 MISO)
PE12 SPI4_SCK  SPI4  AF5
PE14 SPI4_MOSI SPI4  AF5
PE13 SPI4_MISO SPI4  AF5
```

注意：RTT hwdef 使用 AF 编号语法，需确认 rtt_hwdef.py 正确解析。

#### 1.2 补 5V 电源使能引脚
```
PG4  VDD_5V_PERIPH_EN   OUTPUT  HIGH
PF12 VDD_5V_HIPOWER_EN  OUTPUT  HIGH
```

#### 1.3 补电源监测 GPIO
```
PG10 VDD_5V_HIPOWER_nOC  INPUT  PULLUP
PE15 VDD_5V_PERIPH_nOC   INPUT  PULLUP
```

#### 1.4 补 DRDY 引脚
```
PG3  BMI055_G_DRDY_GYR   INPUT
PA10 BMI055_DRDY_ACC      INPUT
PF2  ICM20689_DRDY        INPUT
```

#### 1.5 补缺失 define
```
define HAL_DEFAULT_INS_FAST_SAMPLE 1
define HAL_STORAGE_SIZE 32768
```

**验证：** 编译 → 烧录 → MAVLink 检查 STORAGE_INFO + RAW_IMU + SCALED_PRESSURE

---

### 阶段 2：外设功能对齐（IMPORTANT，可与阶段 1 并行）

**目标：SD 卡、UART 流控、安全开关、LED、PWM、CAN、蜂鸣器、I2C**

#### 2.1 SD 卡
```
PD6  SDMMC2_CK   SDMMC2  AF10
PD7  SDMMC2_CMD  SDMMC2  AF10
PB14 SDMMC2_D0   SDMMC2  AF9
PB15 SDMMC2_D1   SDMMC2  AF9
PG11 SDMMC2_D2   SDMMC2  AF10
PB4  SDMMC2_D3   SDMMC2  AF9
define FATFS_HAL_DEVICE SDCD2
```

#### 2.2 UART 流控（6 组）
```
# USART2 TELEM1
PD3  USART2_RTS  USART2  AF7
PD4  USART2_CTS  USART2  AF7

# USART3 TELEM2
PD12 USART3_RTS  USART3  AF7
PD11 USART3_CTS  USART3  AF7

# USART1 GPS1
PB13 USART1_RTS  USART1  AF7

# UART4 GPS2 — 需确认 PE0/PE1 不与 UART8 冲突

# USART6 TELEM3
PG12 USART6_RTS  USART6  AF8
PG13 USART6_CTS  USART6  AF8

# UART7 DEBUG
PF8  UART7_RTS   UART7   AF8
PE10 UART7_CTS   UART7   AF8
```

⚠️ 引脚冲突需逐个确认（特别是 PB14 SDMMC2_D1 vs USART1）

#### 2.3 安全开关
```
PD10 LED_SAFETY   OUTPUT
PF5  SAFETY_IN    INPUT  PULLDOWN
define HAL_HAVE_SAFETY_SWITCH 1
```

#### 2.4 状态 LED
```
PE3  LED_RED    OUTPUT  GPIO(90)  LOW
PE4  LED_GREEN  OUTPUT  GPIO(91)  LOW
PE5  LED_BLUE   OUTPUT  GPIO(92)  LOW
define AP_NOTIFY_GPIO_LED_RGB_RED_PIN   90
define AP_NOTIFY_GPIO_LED_RGB_GREEN_PIN 91
define AP_NOTIFY_GPIO_LED_RGB_BLUE_PIN  92
```

#### 2.5 PWM 舵机输出（12 路）
```
PI0  TIM5_CH4  TIM5  PWM(1)  GPIO(50)  BIDIR
PH12 TIM5_CH3  TIM5  PWM(2)  GPIO(51)
PH11 TIM5_CH2  TIM5  PWM(3)  GPIO(52)  BIDIR
PH10 TIM5_CH1  TIM5  PWM(4)  GPIO(53)
PD13 TIM4_CH2  TIM4  PWM(5)  GPIO(54)
PD14 TIM4_CH3  TIM4  PWM(6)  GPIO(55)
PE11 TIM1_CH2  TIM1  PWM(7)  GPIO(56)
PE9  TIM1_CH1  TIM1  PWM(8)  GPIO(57)
PI6  TIM8_CH2  TIM8  PWM(9)  GPIO(58)
PI7  TIM8_CH3  TIM8  PWM(10) GPIO(59)
PI5  TIM8_CH1  TIM8  PWM(11) GPIO(60)
PE6  TIM15_CH2 TIM15 PWM(12) GPIO(61)
PH6  TIM12_CH1 TIM12 PWM(13) GPIO(62)  NODMA
PH9  TIM12_CH2 TIM12 PWM(14) GPIO(63)  NODMA
```

#### 2.6 CAN 总线
```
PD0  CAN1_RX  CAN1
PD1  CAN1_TX  CAN1
PB12 CAN2_RX  CAN2
PB13 CAN2_TX  CAN2
PE2  GPIO_CAN1_SILENT  OUTPUT  PUSHPULL  SPEED_LOW  LOW  GPIO(70)
PI8  GPIO_CAN2_SILENT  OUTPUT  PUSHPULL  SPEED_LOW  LOW  GPIO(71)
```

#### 2.7 蜂鸣器
```
PF9  TIM14_CH1  TIM14  GPIO(77)  ALARM
```

#### 2.8 I2C 总线
```
PB9  I2C1_SDA  I2C1
PB8  I2C1_SCL  I2C1
PF0  I2C2_SDA  I2C2
PF1  I2C2_SCL  I2C2
PF15 I2C4_SDA  I2C4
PF14 I2C4_SCL  I2C4
```

#### 2.9 其他 define
```
define HAL_WITH_IO_MCU_DSHOT 1
define HAL_OS_FATFS_IO 1
```

---

### 阶段 3：验证 + 参数配置（阶段 1/2 完成后）

**验证方式：PyMavlink 快速验证 + MAVROS 2 (ROS 2 Jazzy) 深度验证**

#### 3.1 MAVROS 启动
```bash
source /opt/ros/jazzy/setup.bash
ros2 launch mavros apm.launch fcu_url:=serial:///dev/ttyACM1:115200
```

#### 3.2 验证清单

| 模块 | MAVROS Topic | PyMavlink Msg | 判断标准 |
|------|-------------|---------------|---------|
| 飞控在线 | /mavros/state | HEARTBEAT | connected=True |
| 电压/电流 | /mavros/battery | SYS_STATUS | voltage > 0 |
| IMU | /mavros/imu/data_raw | RAW_IMU | 数据非零 |
| 气压计 | /mavros/imu/atm_pressure | SCALED_PRESSURE | press_abs > 0 |
| 传感器健康 | /mavros/sensor_status | SENSOR_STATUS | health=OK |
| 遥控 | /mavros/rc/in | RC_CHANNELS | chancount > 0 |
| 舵机 | /mavros/setpoint_raw/actuator_controls | SERVO_OUTPUT_RAW | 有 PWM 值 |
| GPS | /mavros/global_position/raw/fix | GPS_RAW_INT | fix_type > 0 |
| 姿态 | /mavros/local_position/odom | ATTITUDE | 有四元数 |
| 存储 | N/A | STORAGE_INFO | storage_type != 0 |

#### 3.3 参数配置
通过 MAVLink SET_MESSAGE 或 Mission Planner：
- BATT_MONITOR = 4
- 其他必要参数

#### 3.4 输出
- 终端彩色结果 + JSON 日志
- 存档到 `.cursor/project/verification-results.md`

---

### 阶段 4：已知冲突/风险

| 风险 | 描述 | 处理 |
|------|------|------|
| USART6 TX PG14 | ChibiOS 注释掉（IOMCU SBUS 冲突），RTT 启用 | 验证 SBUS 是否受影响 |
| PB14 复用 | SDMMC2_D1 vs USART1_RTS | 确认实际接线 |
| PE0/PE1 复用 | UART4 vs UART8 RX/TX | 确认实际接线 |
| AF 编号 | RTT 用 AF 编号，ChibiOS 不用 | 确认 rtt_hwdef.py 支持 |
| RTT BSP 引脚 | 部分 SPI 引脚可能已被 RTT BSP 占用 | 检查 RTT board.c |

---

## 四、执行策略

- **阶段 1 + 阶段 2 并行**，两个 CC 任务同时跑
- **阶段 3** 等 1/2 完成后，用 MAVROS 全面验证
- **每个 commit 附带 .cursor 文档更新**
- **编译下载用 flash.sh 一键脚本**

## 五、工具链参考

- Build: `/home/llw/venv-ardupilot/bin/scons --target=cuav_v5 -j4`
- Flash: `.cursor/project/build-flash-pipeline.md` 中的 flash.sh
- Debug: `/opt/gcc-arm-none-eabi-10-2020-q4-major/bin/arm-none-eabi-gdb`
- MAVLink: `/home/llw/venv-ardupilot/bin/python3` + pymavlink
- MAVROS: `source /opt/ros/jazzy/setup.bash && ros2 launch mavros apm.launch`
- 详细报告: `memory/toolchain-audit.md`
