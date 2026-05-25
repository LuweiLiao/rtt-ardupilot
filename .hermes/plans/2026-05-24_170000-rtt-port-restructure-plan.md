# RTT ArduPilot CUAV V5 移植重整规划

> 作者：小马总（Orchestrator）
> 日期：2026-05-24
> 状态：规划草案，待廖博士审议

---

## 1. 总体战略

### 1.1 核心原则
| 原则 | 说明 |
|------|------|
| **保留 RTT RTOS 内核** | RT-Thread 的调度、IPC、设备框架不动 |
| **ChibiOS LLD 风格驱动** | 复用 ChibiOS 已验证的寄存器级驱动代码 |
| **AP_Bootloader** | ArduPilot 自研 bootloader，非 PX4 |
| **构建系统** | 修复 scons 增量编译问题，不迁移 waf |

### 1.2 当前痛点
| 问题 | 根因 | 方案 |
|------|------|------|
| stale object 导致启动失败 | scons 依赖追踪不完整 | 独立构建步骤脚本 |
| ADC 寄存器错位 | F7 ADCv3 vs F4 CMSIS 布局 | ChibiOS ADCv2 LLD 提取 |
| SPI DMA 传输挂死 | 原生 RTT SPI 驱动有 bug | 已有 ChibiOS SPI LLD ✅ |
| bootloader 3秒重启 | PX4 bootloader 机制 | 替换为 AP_Bootloader |
| 增量编译不 boot | 未知对象依赖链 | `--clean` + 完整编译 |

---

## 2. 构建系统

### 2.1 决策：保留 scons，修复增量问题

**不采用 waf 的原因：**
- `scripts/waf` 不存在（ArduPilot waf 用于 ChibiOS 构建，不支持 RTT）
- 重写 waf 支持 RTT 相当于重新实现整个 `scons_ardupilot_sources.py` + 生成 RTT 链接脚本
- 现有 scons 流水线（hwdef→rtconfig→link.lds→rtthread.bin→app_descriptor）工作正常
- 迁移收益 < 成本

**修复方案：**
```bash
# 每次发布前 clean 构建（已验证可工作）
scons --v=ArduCopter --target=cuav-v5 -j$(nproc) --clean
scons --v=ArduCopter --target=cuav-v5 -j$(nproc)
```

**长期方案（可选）：**
- 在 `scons_ardupilot_sources.py` 增加 `.o` 时间戳检查
- 或封装 `make clean && make` 脚本到 `Tools/scripts/rtt_build.sh`

### 2.2 构建流水线
```bash
# 标准构建命令（已验证）
scons --v=ArduCopter --target=cuav-v5 -j$(nproc)

# 输出文件
build/rtt_cuav_v5/rtthread.bin   → 烧录到 0x08008000
build/rtt_deploy/cuav_v5/rt-thread.elf  → 调试用
```

---

## 3. Bootloader

### 3.1 当前状态
| 组件 | 当前 | 目标 |
|------|------|------|
| Bootloader | PX4（CUAV 出厂固件） | AP_Bootloader |
| 启动地址 | 0x08000000 → 跳转到 0x08008000 | 同上 |
| 看门狗 | PX4 3秒 IWDG（导致 reboot 循环） | AP_Bootloader 无看门狗 |
| RTC_BOOT_FWOK | PX4 机制，需要写 backup 寄存器 | AP_Bootloader 不需要 |

**现状问题：** PX4 bootloader 开启 IWDG 3秒。APP 必须在 3秒内写 RTC_BOOT_FWOK，否则 bootloader 认为 APP 崩溃并擦除。由于 RTT 启动较慢（C++ 静态初始化、RTT 调度器启动），经常超时。

### 3.2 AP_Bootloader 支持情况
- `Tools/AP_Bootloader/` 已存在，含 F7 支持（`mcu_f7.h` 识别 STM32F76x_77x=0x451）
- 有 `wscript`（waf 构建），可通过 `./waf bootloader` 构建
- ChibiOS CUAV V5 参考：`libraries/AP_HAL_ChibiOS/hwdef/fmuv5/hwdef-bl.dat`

### 3.3 方案：生成 AP_Bootloader 固件
```python
# 步骤：
1. 创建 CUAV V5 bootloader hwdef: RTT hwdef 的 bootloader 版本
2. 使用 waf 编译（waf 用于 bootloader 构建是可以的）
3. 烧录到 0x08000000
4. APP 烧录到 0x08008000
5. 验证：APP 无需写 RTC_BOOT_FWOK，无 IWDG 复位
```

### 3.4 所需文件
| 文件 | 来源 | 说明 |
|------|------|------|
| `Tools/AP_Bootloader/mcu_f7.h` | ✅ 已有 | STM32F767 识别 |
| AP_Bootloader CUAV V5 配置 | ❌ 需新建 | 参考 fmuv5/hwdef-bl.dat |
| Bootloader 固件 | ❌ 需编译 | `./waf bootloader --board=fmuv5` |

---

## 4. 驱动移植（ChibiOS LLD → RTT）

### 4.1 已完成
| 驱动 | 文件 | 行数 | 状态 |
|------|------|------|------|
| SPI LLD | `drivers/hal_spi_lld_rtt.c` | 269 | ✅ 功能验证通过 |

### 4.2 待移植

| 优先级 | 驱动 | ChibiOS 源文件 | 行数 | RTT 目标 | 依赖 |
|--------|------|----------------|------|----------|------|
| **P0** | **ADC LLD** | `LLD/ADCv2/hal_adc_lld.c` | 460 | `drivers/hal_adc_lld_rtt.c` | 无 |
| **P0** | **IMU SPI 修复** | 已有 SPI LLD | — | `SPIDevice.cpp` 调优 | SPI LLD ✅ |
| P1 | I2C LLD | `LLD/I2Cv2/hal_i2c_lld.c` | 1405 | `drivers/hal_i2c_lld_rtt.c` | 无 |
| P1 | UART LLD | `LLD/USARTv2/hal_serial_lld.c` | 923 | `drivers/hal_uart_lld_rtt.c` | DMA |
| P2 | TIM/PWM LLD | `LLD/TIMv1/hal_pwm_lld.c` | 1293 | `drivers/hal_pwm_lld_rtt.c` | TIM |
| P2 | SDMMC LLD | `LLD/SDMMCv1/hal_sd_lld.c` | ~800 | `drivers/hal_sdmmc_lld_rtt.c` | DMA |

### 4.3 移植模式（已验证的模板）

```c
// ChibiOS 源: SPIDriver *spip → RTT: SPI_TypeDef *spi
// ChibiOS: osalDbgAssert → RTT: 删除
// ChibiOS: palSetLineMode → RTT: GPIO 直接寄存器
// ChibiOS: spi_lld_start → RTT: spi_lld_start_rtt(SPI_TypeDef *spi)
// DMA: 静态配置表（已建立，根据 hwdef 生成）
```

### 4.4 架构图
```
AP_HAL_RTT/
├── SPIDevice.cpp          ← 调用 hal_spi_lld_rtt.c
├── AnalogIn.cpp           ← 调用 hal_adc_lld_rtt.c (P0)
├── I2CDevice.cpp          ← 调用 hal_i2c_lld_rtt.c (P1)
├── UARTDriver.cpp         ← 调用 hal_uart_lld_rtt.c (P1)
├── RCOutput.cpp           ← 调用 hal_pwm_lld_rtt.c (P2)
├── drivers/               ← ChibiOS LLD 适配层
│   ├── hal_spi_lld_rtt.c  ✅ 已完成
│   ├── hal_adc_lld_rtt.c  🔲 P0
│   ├── hal_i2c_lld_rtt.c  🔲 P1
│   ├── hal_uart_lld_rtt.c 🔲 P1
│   └── hal_pwm_lld_rtt.c  🔲 P2
```

### 4.5 当前所有 hacks 清单（最终需移除）

| hack | 文件 | 移除条件 |
|------|------|---------|
| ADC 全 bypass | `AnalogIn.cpp` — `_adc_read()` 返回 0 | ADC LLD 移植完成 |
| GPS init 跳过 | `system.cpp` — 注释 `gps.init()` | UART LLD 移植完成 |
| Compass init 跳过 | `system.cpp` — 注释 `compass.init()` | I2C LLD 移植完成 |
| Baro calibrate 跳过 | `system.cpp` — 注释 `barometer.calibrate()` | I2C LLD + 传感器驱动完成 |
| Storage STUB | `Storage.cpp` — RAM 后端 | FRAM (SPI2) 驱动完成 |
| IMU sensor 跳过 | `AP_InertialSensor.cpp` — skip start() | SPI LLD + ICM20689 驱动完成 |
| ESC cal 跳过 | `esc_calibration.cpp` — 立即返回 | RCInput 驱动完成 |
| AP_Vehicle debug markers | `AP_Vehicle.cpp` — stage 550/551/552/553 | 确定后移除 |

---

## 5. 分阶段执行计划

### Phase 0 — 构建环境固化（1-2天）
```yaml
目标: 稳定可复现的构建 + 烧录流程
任务:
  - 创建 `rtt_build.sh` 封装 clean+compile
  - 创建 `rtt_flash.sh` 封装烧录
  - 验证：连续 3 次 clean 构建 + 烧录均能 boot 到 MAVLink
验收: adc_timeout 持续增长，hal_run=0xBBBBBBBB
```

### Phase 1 — ADC LLD 移植（3-5天）
```yaml
目标: 替换 AnalogIn.cpp 的 ADC bypass 为 ChibiOS ADCv2 LLD
任务:
  - 提取 hal_adc_lld.c(460行) → drivers/hal_adc_lld_rtt.c
  - 适配 RTT（删除 osal，直接 SPI_TypeDef*）
  - AnalogIn.cpp 调用 spi_lld_convert_rtt()
  - 启用芯片温度/电压读取验证 ADC 工作
验收: adc_timeout == 0（无超时），读取到有效 ADC 值
```

### Phase 2 — SPI IMU 驱动调试（2-3天）
```yaml
目标: ICM20689 WHO_AM_I 读取正确
任务:
  - 用已有 ChibiOS SPI LLD 驱动
  - 对比 ChibiOS SPI 初始化顺序（时钟使能、GPIO、速率）
  - 修复 SPI polled 模式数据错误
验收: ICM20689 返回 WHO_AM_I=0x89，IMU 数据流正常
```

### Phase 3 — 启动完整化（3-5天）
```yaml
目标: 移除所有 hacks，系统完整启动 → STANDBY
任务:
  - 逐步启用：Storage(FRAM) → Baro → Compass → GPS
  - 每启用一个，验证 MAVLink 心跳 1Hz
验收: MAVLink system_status=3(STANDBY), fast_loop_count>0
```

### Phase 4 — 扩展驱动（5-7天）
```yaml
目标: PWM输出 + RC输入 + SD卡 + IOMCU
任务:
  - UART LLD 移植 → USART2/3/1/4/6
  - I2C LLD 移植 → I2C3
  - TIM/PWM LLD 移植 → RCOutput
  - SDMMC → sdcard.cpp
验收: RC 通道可读，电机可输出，SD 卡可记录日志
```

---

## 6. 当前阻塞点分析

### 6.1 最紧急：scons 增量编译问题
```bash
# 解决方案（已验证）：
scons --v=ArduCopter --target=cuav-v5 -j$(nproc) --clean
scons --v=ArduCopter --target=cuav-v5 -j$(nproc)
```

### 6.2 第二紧急：bootloader 3秒看门狗
```bash
# 临时：继续写 RTC_BOOT_FWOK（已实现，已验证生效）
# 长期：烧录 AP_Bootloader 彻底解决
```

### 6.3 第三：启动过程中的阻塞点定位
已添加 stage markers 550-553 在 AP_Vehicle::setup()。通过读取 `rtt_dbg_setup_stage` 地址（当前 0x2001b2c0）可精确定位阻塞位置。

---

## 7. 风险与权衡

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| ChibiOS LLD 有 F7 特定寄存器差异 | 中 | 高 | 对照 RM0431 参考手册逐寄存器验证 |
| AP_Bootloader 生成失败 | 低 | 高 | 先用 PX4 bootloader + RTC_BOOT_FWOK 并行 |
| I2C LLD 1405 行较复杂 | 中 | 中 | 先做轮询模式，DMA 后加 |
| scons 构建不稳定 | 高 | 中 | 固定 `--clean` 构建流程，后续修依赖追踪 |

---

## 8. 验证矩阵

| 检查项 | 方法 | 期望值 |
|--------|------|--------|
| 构建完整性 | `scons` 输出 | `Binary integrity check PASSED` |
| 烧录验证 | `openocd program verify` | `Verified OK` |
| 无 HardFault | `mdw 0xE000ED2C` | 0x00000000 |
| SysTick 运行 | `mdw 0x2000d4b8` | > 0 |
| 进入 main loop | `mdw 0x20000100` | 0xBBBBBBBB |
| MAVLink 心跳 | pymavlink `wait_heartbeat()` | status >= 1 |
| MAVLink STANDBY | pymavlink | status = 3 |
| Setup 完成 | `mdw 0x2001b2c0` | > 660 或 0 |
| IMU 数据 | MAVLink RAW_IMU | 加速度/角速度非零 |

---

## 9. 文件变更清单

### 新建
```
libraries/AP_HAL_RTT/drivers/hal_adc_lld_rtt.c    ← ChibiOS ADCv2 LLD
libraries/AP_HAL_RTT/drivers/hal_i2c_lld_rtt.c     ← ChibiOS I2Cv2 LLD (P1)
libraries/AP_HAL_RTT/drivers/hal_uart_lld_rtt.c    ← ChibiOS USARTv2 LLD (P1)
libraries/AP_HAL_RTT/drivers/hal_pwm_lld_rtt.c     ← ChibiOS TIMv1 LLD (P2)
Tools/scripts/rtt_build.sh                         ← 构建封装脚本
Tools/scripts/rtt_flash.sh                         ← 烧录封装脚本
```

### 修改
```
libraries/AP_HAL_RTT/AnalogIn.cpp                  ← 替换为 ADC LLD 调用
libraries/AP_HAL_RTT/SPIDevice.cpp                 ← 已有 SPI LLD 调用
libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat       ← 恢复 ICM20602，启用传感器
ArduCopter/system.cpp                              ← 按阶段移除 hacks
libraries/AP_HAL_RTT/Storage.cpp                   ← 恢复 FRAM (SPI2)
libraries/AP_InertialSensor/AP_InertialSensor.cpp  ← 恢复 IMU start()
```

### 删除
```
libraries/AP_Vehicle/AP_Vehicle.cpp                ← 调试 markers（确定后）
ArduCopter/esc_calibration.cpp                     ← hack（确定后）
```

---

## 10. 下一步行动

```yaml
立即开始:
  - [ ] 封装构建脚本 (rtt_build.sh) → Phase 0
  - [ ] 封装烧录脚本 (rtt_flash.sh) → Phase 0
  
PI 优先级:
  P0: ADC LLD 移植 → 替换 AnalogIn.cpp bypass
  P0: SPI IMU 调试 → ICM20689 WHO_AM_I 修复
  P1: I2C LLD 移植 → Compass/GPS 恢复
  P1: AP_Bootloader 构建 → 消除 IWDG 问题
  P2: UART/PWM LLD → 完整硬件支持

待确认:
  - 是否需要先做 AP_Bootloader（Phase 0.5），还是先完成 P0 驱动再处理
  - scons waf 混合：bootloader 用 waf，app 用 scons，是否可接受
```
