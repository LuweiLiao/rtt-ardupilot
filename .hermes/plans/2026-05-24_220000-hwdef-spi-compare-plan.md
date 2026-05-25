# Plan: RTT cuav_v5 hwdef + SPI Driver → 编译烧录验证

## 现状

ICM42688 已添加（PF11 CS、SPIDEV、IMU 探测行），但 `wait_for_sample()` 仍挂死。
根因：**SPIDevice 驱动 `_do_transfer()` 无法与 IMU SPI 通信**，而非 hwdef 缺失。

---

## Phase 0: 剩余 hwdef 差异分析

### 0.1 ICM20602 状态

- **ChibiOS CUAVv5 实际效果**：`undef IMU` + `SPIDEV icm42688` + `IMU icm20602` → **icm20602 的 DEVID2 被 icm42688 覆盖**（同一 SPI1 + DEVID2），icm20602 的硬件 CS(PF3) 不会被驱动。icm20602 的 IMU probe 会因 WHO_AM_I 不匹配而失败 fallthrough。
- **RTT 当前**：icm20602 注释掉 → 与 ChibiOS 实际运行时等价。
- **结论**：**不需要启用**。保留注释状态即可。

### 0.2 BMI055 / BMI088 状态

- **ChibiOS CUAVv5**：DEVID3/4 保留给 bmi055_g/a。CUAVv5 硬件上确实有 BMI055 陀螺+加速度计（PF4 + PG10 CS）。
- **RTT 当前**：SPIDEV 条目存在（DEVID3/4），IMU 探测行注释掉。
- **结论**：可开启，但前提是 SPIDevice 驱动能够基本运作（SPI 通信正常）。**非阻塞项**。

### 0.3 DRDY 引脚状态

| DRDY 信号 | ChibiOS fmuv5 | RTT cuav_v5 | 状态 |
|-----------|:---:|:---:|:----:|
| DRDY1 ICM20689 (PB4) | ✅ | ✅ (L273) | ✅ |
| DRDY4 ICM20602 (PC5) | ✅ | ✅ (L276) | ✅ |
| DRDY5 BMI055_GYRO (PC13) | ✅ | ✅ (L87) | ✅ |
| DRDY6 BMI055_ACC (PD10) | ✅ | ✅ (L88) | ✅ |
| DRDY7 EXTERNAL1 (PD15) | ✅ | ✅ (L89) | ✅ |
| DRDY8 NC (PE7) | ✅ | ✅ (L90) | ✅ |
| DRDY2 BMI055_GYRO (PB14) | ✅ | ✅ (L274) | ✅ |
| DRDY3 BMI055_ACC (PB15) | ✅ | ✅ (L275) | ✅ |

**结论：DRDY 引脚已全，无需修改。** ✅

### 0.4 SPI 引脚状态

| 信号 | ChibiOS fmuv5 | RTT cuav_v5 | 状态 |
|------|:---:|:---:|:----:|
| SPI1_SCK (PG11) | ✅ | ✅ (AF5) | ✅ |
| SPI1_MISO (PA6) | ✅ | ✅ (AF5) | ✅ |
| SPI1_MOSI (PD7) | ✅ | ✅ (AF5) | ✅ |
| SPI2_SCK (PI1) | ✅ | ✅ (AF5) | ✅ |
| SPI2_MISO (PI2) | ✅ | ✅ (AF5) | ✅ |
| SPI2_MOSI (PI3) | ✅ | ✅ (AF5) | ✅ |
| SPI4_SCK (PE2) | ✅ | ✅ (AF5) | ✅ |
| SPI4_MISO (PE13) | ✅ | ✅ (AF5) | ✅ |
| SPI4_MOSI (PE6) | ✅ | ✅ (AF5) | ✅ |

**结论：SPI 引脚已全，无需修改。** ✅

### 0.5 电源/使能引脚

| 信号 | ChibiOS | RTT | 状态 |
|------|:---:|:---:|:----:|
| VDD_3V3_SENSORS_EN (PE3) | ✅ | ✅ | ✅ |
| VDD_5V_RC_EN (PG5) | ✅ | ✅ | ✅ |
| VDD_3V3_SD_CARD_EN (PG7) | ✅ | ✅ | ✅ |
| nVDD_5V_HIPOWER_EN (PF12) | ✅ | ✅ (L267) | ✅ |
| nVDD_5V_PERIPH_EN (PG4) | ✅ | ✅ (L268) | ✅ |
| VDD_5V_WIFI_EN (PG6) | ✅ | ✅ (L86) | ✅ |
| HEATER_EN (PA7) | ✅ | ✅ | ✅ |

**结论：电源引脚已全，无需修改。** ✅

### 0.6 汇总

| 项目 | 评估 | 动作 |
|------|:----:|:----:|
| ICM42688 (PF11) | ✅ 已添加 | — |
| ICM20602 | ❌ hwdef DEVID2 被 ICM42688 覆盖，无需启用 | 保持注释 |
| BMI055/BMI088 | ⏳ 可启用但非阻塞 | 暂缓 |
| DRDY 引脚 | ✅ 已全 | — |
| SPI 引脚 | ✅ 已全 | — |
| 电源/使能 | ✅ 已全 | — |

**结论：hwdef 已基本对齐 ChibiOS CUAVv5。** 当前阻塞的问题不来自 hwdef，而是 SPIDevice 驱动本身。

---

## Phase 1: SPIDevice 驱动故障诊断

### 现象
- `rtt_dbg_setup_stage = 0x28b` → setup() 完成
- `rtt_dbg_main_loop_entry_called = 0x12345678` → 主循环入口
- `rtt_dbg_main_loop_iterations = 0` → **loop() 从未返回**
- 卡在 `AP::ins().wait_for_sample()` → SPI IMU 无数据

### 诊断方法

1. **检查 SPI 寄存器配置**
   用 OpenOCD 读 SPI1->CR1/CR2 寄存器，验证是否已配置：
   ```
   SPI1 基址: 0x40013000
   CR1 @ 0x40013000: 需要 SPE(bit6)=1, MSTR(bit2)=1, BR[2:0] 已设
   CR2 @ 0x40013004: 需要 FRXTH(bit12), 以及 DS[3:0] (数据大小)
   ```

2. **检查 SPI MOSI/MISO 电气连接**
   用逻辑分析仪或在 SPIDevice::_do_transfer() 中增加调试输出，看 MOSI 是否有时钟和数据变化，MISO 是否有响应。

3. **检查 GPIO 复用功能**
   验证 PG11(SCK), PA6(MISO), PD7(MOSI) 是否已配置为 AF5（SPI1）。
   ```
   GPIOG MODER @ 0x40021800 → PG11 bits [23:22] = 10 (AF)
   GPIOG AFRH @ 0x40021824 → PG11 bits [15:12] = 0101 (AF5=SPI1_SCK)
   ```

4. **CS 引脚时序**
   检查 ICM20689_CS(PF2) 和 ICM42688_CS(PF11) 的 BSRR 控制是否正常工作。

5. **ChibiOS 参考对比**
   对照 ChibiOS `SPIDevice::_do_transfer()` 的 SPI 寄存器操作方法：
   - 等待 TXE、RXNE
   - SPI_DR 读写顺序
   - 片选时序

### 验证 SPI IMU 的最简方法

在 OpenOCD/GDB 中添加断点于 `_do_transfer()`，单步执行一次对 icm20689 WHO_AM_I (0x75) 的读取：
- 发送 0x80 | 0x75（读命令 + WHO_AM_I 地址）
- 发送 0x00（dummy，接收数据）
- 期望返回 0xAE（icm20689 WHO_AM_I）

---

## Phase 2: 编译 → 烧录 → 验证

### 步骤

| Step | 动作 | 预期结果 |
|:----:|------|:--------|
| 1 | `scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` | 编译通过 |
| 2 | OpenOCD 烧录 `rtthread.bin @ 0x08008000` | Verified OK |
| 3 | 复位板子 + 等待 10s | 板子运行 |
| 4 | OpenOCD 读 debug 变量 | setup_stage=0x28b, hal_run=0x11111111, main_loop_iterations>0 |
| 5 | `cat /dev/ttyACM1` | 检查 MAVLink 心跳或 RTT 控制台输出 |
| 6 | 如果失败：读 SPI1 寄存器 + GPIO AFR | 确认 SPI 配置 |

### 验证成功标准
- MAVLink 心跳通过 USB CDC 发出
- main_loop_iterations > 0
- 无 HardFault

---

## Phase 3: 风险/备选方案

### 如果 SPI 驱动修复后 icm20689 仍不通
- **尝试切换为 ICM42688 为主要 IMU**（对应 ChibiOS CUAVv5 的实际硬件布局）
- 方法：在 hwdef 中将 icm20689 移到 ICM42688 之后，或禁用 icm20689

### 如果 SPI 完全不通（寄存器未使能）
- 检查 HAL_RTT_Class 中 SPI 时钟使能：`RCC->AHB1ENR |= RCC_AHB1ENR_SPI1EN`
- 检查 GPIO 时钟使能
- 对照 ChibiOS `stm32_spi_init()` 的完整初始化序列

### 备选：启用 icm20602 + BMI055
- 如果 icm20689 + icm42688 都不通，尝试启用 icm20602（换 CS 引脚）
- 或启用 BMI055（DEVID3/4，不同的 SPI 驱动栈）

---

## 文件改动清单

| 文件 | 改动类型 | 说明 |
|------|:-------:|------|
| `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` | ✅ 已完成 | ICM42688 添加 |
| `libraries/AP_HAL_RTT/SPIDevice.cpp` | ⚠️ 可能需修改 | `_do_transfer()` 寄存器序列 |
| `libraries/AP_HAL_RTT/SPIDevice.h` | ⚠️ 可能需修改 | SPI 配置结构体 |

---

## 执行计划（步骤顺序）

1. **Phase 0 已确认** — hwdef 无需进一步修改
2. **Phase 1 诊断** → CEO 决定是否进入 SPIDevice 驱动修复
3. **Phase 2 编译烧录** → repeat until MAVLink heartbeat appears
4. **Phase 3 备选** → 仅在 Phase 1+2 失败后启用
