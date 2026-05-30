# AP_HAL_RTT STM32 寄存器级重写总体规划

> 目标：参考 `AP_HAL_ChibiOS` 的 hwdef + LLD 语义，将 RTT 侧 STM32 驱动彻底去 HAL 化，
> 使 ArduPilot 在 RT-Thread 上达到与 ChibiOS 等价的 bring-up / 通信 / 传感器 / 运行完整性。
> 板级基线：`CUAV v5` / STM32F767。

## 0. 当前事实（2026-05-26 Hermes 会话）

| 层级 | 状态 |
|------|------|
| P0a FPU/烧录地址 | CPACR 修复、app @ 0x08008000 已验证 |
| 历史基线（4月） | boot→main→hal.run→~400Hz、USB CDC MAVLink 曾稳定 |
| **最新阻塞** | `rtt_dbg_hal_run_called=0xDEADBEEF`，`main()`/`HAL_RTT::run()` 未进入；SPI1 未 init |
| 已有 LL 层 | `board/drivers_ll/`：clock/gpio/usart/spi/flash |
| 仍混 HAL | RTT BSP `drv_*.c`、部分 AP_HAL_RTT 路径、CubeMX/HAL 包 |

## 1. 架构目标（对齐 ChibiOS，不复制硬编码）

```
hwdef.dat / rtt_hwdef.py
        ↓
hwdef.h (引脚/时钟/外设宏)
        ↓
┌───────────────────────────────────────┐
│  LLD 层 (寄存器/LL，无 HAL 状态机)      │  ← board/drivers_ll/*.c
│  hal_*_lld_rtt.c 或 drv_*_ll.c         │
└───────────────────────────────────────┘
        ↓
┌───────────────────────────────────────┐
│  RT-Thread 设备框架 (可选薄封装)         │  ← rt_device_register
└───────────────────────────────────────┘
        ↓
┌───────────────────────────────────────┐
│  AP_HAL_RTT (语义对齐 AP_HAL_ChibiOS)  │  ← GPIO/UART/SPI/Scheduler...
└───────────────────────────────────────┘
        ↓
ArduCopter main → hal.run() → init_ardupilot()
```

**原则**
- ChibiOS 对齐的是**职责边界与初始化顺序**，不是复制 `hal_pal_lld.c` 全文
- HAL 库（`HAL_SPI_*`/`HAL_UART_*`/`rt_pin_*` 包装 HAL）逐步替换为 LLD 直写
- 每改一个外设：单模块编译 → 烧录 → OpenOCD + CDC MAVLink 双重验证
- 当前 P0：**先修启动链让 `HAL_RTT::run()` 进入**，再并行外设 LLD 化

## 2. 分层与职责

| 层 | ChibiOS 参考 | RTT 现状 | 目标 |
|----|-------------|---------|------|
| Boot | `common_startup` + `boardInit` | `startup_rtt_override.S` + `entry()` | 保证 `main()` 被调用 |
| Clock | `hal_lld_init` / RCC LL | `stm32f7_clock_ll.c` | 已 LL，复核 PLL/USBCLK |
| GPIO/PAL | `hal_pal_lld.c` | `GPIO.cpp` + `drv_gpio_ll` + `rt_pin` | 统一 CMSIS MODER/AF/BSRR |
| SPI | `hal_spi_lld.c` + `Shared_DMA` | `drv_spi_ll` + `SPIDevice.cpp` | DMA+CS 时序对齐 ChibiOS |
| I2C | `hal_i2c_lld.c` | 软件 I2C + `I2CDevice.cpp` | 评估 bitbang vs HW I2C3 |
| UART | `hal_serial_lld.c` | `drv_usart_ll` + `UARTDriver.cpp` | 去 HAL UART，CDC 事件驱动 |
| USB | `hal_usb_*` / ChibiOS USB | CherryUSB + `hal_usb_lld_rtt.c` | DTR/枚举/TX 环对齐 |
| Timer/PWM | `hal_pwm_lld.c` | `drv_pwm`/`drv_tim` | RCOutput 50/100Hz |
| ADC | `hal_adc_lld.c` | `hal_adc_lld_rtt.c` | 1kHz tick 采样 |
| Flash | `hal_flash_lld.c` | `drv_flash_ll` + `Flash.cpp` | 参数区 page 10-11 |
| SD | `hal_sdc_lld.c` | SDMMC + DFS | 非阻塞 mount |
| DMA | `stm32_dma.c` | 部分 LL SPI | 统一 stream 分配表 |
| Scheduler | ChibiOS threads | RT-Thread threads | 语义已 97% 对齐 |

## 3. 阶段里程碑

### M0 — 启动链（当前 P0，阻塞一切）
- [ ] 确认 `rtthread_startup → main_thread → main()` 完整路径
- [ ] `rtt_dbg_hal_run_called == 0xAAAAAAAA`
- [ ] `init_ardupilot` setup_stage 推进
- 验证：OpenOCD 线程列表 + 25s 后 debug 变量

### M1 — GPIO/PAL 寄存器化（P0b）
- [ ] `GPIO.cpp` 去 `rt_pin_*`，改用 `hal_gpio_lld` / CMSIS
- [ ] SPI1 引脚 MODER/AF 与 ChibiOS hwdef 一致（PG11/PA6/PD7）
- 验证：OpenOCD 读 MODER/AFR；IMU WHOAMI

### M2 — SPI + 传感器（P0c）
- [ ] SPI1 LLD DMA 或稳定轮询；CS 共线全部拉高
- [ ] BMI055/MS5611/IST8310 数据有效
- 验证：RAW_IMU、SYS_STATUS、GCS 心跳

### M3 — UART/USB 通信
- [ ] UARTDriver 全路径 LL；USB CDC DTR/重连
- 验证：pymavlink 15s 心跳 + 30s 稳定流

### M4 — 运行完整性
- [ ] PWM/RCInput/Storage/AnalogIn/IOMCU
- 验证：L0→L2 测试矩阵 + ArduCopter 30min

### M5 — HAL 清除与多板模板
- [ ] 移除未使用的 `stm32f7xx_hal_*` 链接
- [ ] hwdef 数据驱动，无板级硬编码

## 4. 20 路并行分析任务（composer-2.5-fast）

| ID | 子系统 | RTT 入口 | ChibiOS 参考 | 交付物 |
|----|--------|---------|-------------|--------|
| A01 | 启动链/main | `startup_rtt_override.S`, `main.c`, `AP_HAL_Main.h` | ChibiOS `crt0` + `main` | 调用链图 + 根因假设 |
| A02 | Clock/RCC | `stm32f7_clock_ll.c` | `hal_lld.c` | 寄存器对照表 |
| A03 | GPIO/PAL | `GPIO.cpp`, `drv_gpio_ll.c` | `hal_pal_lld.c`, hwdef | 重写 API 草案 |
| A04 | SPI LLD | `drv_spi_ll.c`, `SPIDevice.cpp` | `hal_spi_lld.c` | DMA/CS 差异清单 |
| A05 | I2C | `I2CDevice.cpp`, soft I2C | `hal_i2c_lld.c` | IST8310 路径 |
| A06 | UART | `UARTDriver.cpp`, `drv_usart_ll.c` | `hal_serial_lld.c` | 端口映射表 |
| A07 | USB CDC | `hal_usb_lld_rtt.c`, CherryUSB | ChibiOS USB 栈 | DTR/枚举时序 |
| A08 | Timer/PWM | `RCOutput`, drv_pwm | `hal_pwm_lld.c` | TIM1/4/12 映射 |
| A09 | ADC | `AnalogIn`, `hal_adc_lld_rtt.c` | `hal_adc_lld.c` | 1kHz 采样链 |
| A10 | Flash | `Flash.cpp`, `drv_flash_ll.c` | `hal_flash_lld.c` | 参数区布局 |
| A11 | SD/MMC | `sdcard.cpp` | `hal_sdc_lld.c` | SDMMC1 vs 2 |
| A12 | DMA | SPI/UART DMA | `stm32_dma.c` | stream 冲突表 |
| A13 | Scheduler | `Scheduler.cpp`, `HAL_RTT_Class.cpp` | ChibiOS threads | 线程/优先级对照 |
| A14 | IOMCU | `AP_IOMCU`, UART8 | ChibiOS IOMCU | ROMFS+UART8 |
| A15 | IWDG | `system.cpp`, startup | ChibiOS WDG | 512ms→10s 策略 |
| A16 | MPU/Cache | `system.cpp`, link.lds | ChibiOS MPU | DTCM/DMA 规则 |
| A17 | RCInput | `RCInput.cpp` | ChibiOS RC | SBUS/IOMCU |
| A18 | hwdef 生成 | `rtt_hwdef.py`, hwdef.dat | ChibiOS hwdef | 宏/引脚 diff |
| A19 | HAL 依赖审计 | 全仓库 grep HAL | — | 移除优先级列表 |
| A20 | 验证矩阵 | `tests/`, CLAUDE.md | driver-validation | 每阶段命令 |

## 5. 修复顺序（强制）

1. **M0 启动链** — 不修外设前先让 `hal.run()` 进入
2. **M1 GPIO** — SPI/UART 引脚 AF 依赖 GPIO
3. **M2 SPI** — IMU/Baro 数据
4. **M3 USB/UART** — GCS 通信
5. **M4 其余外设** — 按 driver-validation-matrix 优先级
6. 每步：**单模块 diff → 编译 → 烧录 → OpenOCD + CDC**

## 6. 禁止事项

- 禁止未确认 M0 前大规模改 GPIO/SPI/USB 同时提交
- 禁止恢复 STM32 HAL 状态机到 ISR 热路径
- 禁止破坏 CUAV v5 已验证的 MAVLink/参数基线（改前打 tag）
- 禁止修改 Bootloader / IO firmware 二进制

## 7. 成功判据（与 ChibiOS 等价 L0）

- OpenOCD：reset 后 10s halt/resume 无 HardFault
- CDC：pymavlink heartbeat 15s + STANDBY
- `rtt_dbg_main_loop_iterations` 持续增长
- RAW_IMU / ATTITUDE / SYS_STATUS 有效流
- 参数 943 全量下载
