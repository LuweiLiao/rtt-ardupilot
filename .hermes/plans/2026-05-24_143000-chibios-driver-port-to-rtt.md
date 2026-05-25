# ChibiOS LLD 驱动移植到 RTT — 计划

## 核心理念

**保留 RTT RTOS 内核**（调度器、线程、信号量、定时器），**用 ChibiOS LLD 替换 RTT 原生驱动**。

ChibiOS LLD（低层驱动）是纯寄存器操作的硬件抽象层，**零 RTOS 依赖**（已验证：SPI LLD 0个RTOS调用，ADC LLD 0个）。直接提取+加 RTT 适配头文件即可。

## 为什么这么做

| 问题 | RTT 原生驱动 | ChibiOS LLD |
|------|------------|-------------|
| SPI DMA 挂死 | ✅ 自己写的，有bug | ✅ 已在 CUAV V5 上跑通 |
| ADC ADRDY 不置位 | ❌ 寄存器抄错 | ✅ F7 ADCv2 完美适配 |
| IOMCU UART | ❌ 有问题 | ✅ 已验证 |
| I2C 时序 | ❌ 软件 I2C 慢 | ✅ 硬件 I2C v2 |
| 代码质量 | 995 行，含 bounce buffer | 726 行，纯寄存器 |

## 架构方案

```
┌─────────────────────┐
│   AP_HAL_RTT        │  ← 保留！提供 ArduPilot HAL 接口
│   (SPIDevice.cpp)   │
└────────┬────────────┘
         │ 调用
┌────────▼────────────┐
│   ChibiOS LLD Wrapper│  ← 新增！移植的 ChibiOS LLD + RTT 适配层
│   (新文件: hal_spi_  │
│    lld_rtt.c)       │
└────────┬────────────┘
         │ 寄存器操作
┌────────▼────────────┐
│   STM32F7 硬件       │
└─────────────────────┘
```

### 适配层原理

ChibiOS LLD 需要的唯一 RTT 原生功能：
- **osalDbgAssert()** → 去掉或 `RTT_ASSERT()`
- **DMA 流分配** → 静态配置（SPI1→DMA2 Stream2/5）
- **中断回调** → RTT 中断处理中调用 ChibiOS 回调函数
- **CRITICAL 区** → 不需要（纯寄存器操作在关中断下运行）

## 分阶段计划

### Phase 1: SPI 驱动移植（2h，Claude Code 执行）

**目标**：用 ChibiOS `hal_spi_lld.c` 替换 RTT SPIDevice.cpp 的 DMA 传输逻辑

**具体步骤**：

1. **提取 ChibiOS LLD 核心代码**
   - 复制 `modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv2/hal_spi_lld.c` → `libraries/AP_HAL_RTT/drivers/hal_spi_lld.c`
   - 保留所有 `spi_lld_*()` 函数
   - 去掉 ChibiOS 特有的 SPIDriver 结构，替换为 RTT 兼容结构

2. **替换 ChibiOS 类型和宏**
   - `SPIDriver *spip` → `SPI_TypeDef *spi`（直接传 SPIx 基址）
   - `spip->spi` → 直接使用 `spi`
   - `osalDbgAssert()` → 删掉

3. **适配到 RTT AP_HAL**
   - `spi_lld_start()` → 从 RTT SPIDevice.cpp 现有配置中读取 SPI 参数（speed, mode, cs_pin）
   - `spi_lld_exchange()` → 直接被 `RTT::SPIDevice::transfer()` 调用
   - DMA 中断 → 注册到 RTT 中断向量表

4. **简化 RTT SPIDevice.cpp**
   - 保留：CS 控制、速度切换、semaphore、bounce buffer
   - 替换：`_spi_dma_xfer()` → 调用 `spi_lld_exchange()`
   - 去掉：DMA 寄存器直接操作

**变更文件**：
| 文件 | 操作 |
|------|------|
| `libraries/AP_HAL_RTT/drivers/hal_spi_lld.c` | **新建** — 从 ChibiOS 移植 |
| `libraries/AP_HAL_RTT/SPIDevice.cpp` | **修改** — 简化，调 LLD |
| `libraries/AP_HAL_RTT/SPIDevice.h` | **可能改** | 

**验证**：
- 编译通过
- OpenOCD halt 检查：SPI1 寄存器配置与 ChibiOS 一致
- ICM20689 WHO_AM_I 读回 0x89
- setup_stage 从 680 往前走

---

### Phase 2: ADC 驱动移植（1h）

**目标**：用 ChibiOS ADCv2 LLD 替换 `AnalogIn.cpp` 中手写的 ADC 寄存器代码

**步骤**：
1. 提取 `hal_adc_lld.c`（460行）
2. 适配到 F7 ADCv2 的 DMA 模式
3. 集成到 `AnalogIn::_adc_init_once()` 和 `_adc_read()`

**变更文件**：
| 文件 | 操作 |
|------|------|
| `libraries/AP_HAL_RTT/drivers/hal_adc_lld.c` | **新建** |
| `libraries/AP_HAL_RTT/AnalogIn.cpp` | **修改** |

**验证**：
- ADC 正常转换（非 0 值）
- `rtt_adc_timeout_count` 不增长（无 timeout）
- IMU 数据流正常

---

### Phase 3: UART/IOMCU 驱动移植（1h）

**目标**：用 ChibiOS USART LLD 替换 UART8（IOMCU）驱动

**步骤**：
1. 提取 `hal_uart_lld.c` → 适配
2. 使能 hwdef 中的 IOMCU_UART（取消注释 PE0/PE1）

**变更文件**：
| 文件 | 操作 |
|------|------|
| `libraries/AP_HAL_RTT/drivers/hal_uart_lld.c` | **新建** |
| `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` | **修改** |

**验证**：
- IOMCU 通信正常
- RC 输入可读

---

### Phase 4: I2C 驱动移植（1h）

**目标**：用 ChibiOS I2Cv2 LLD 替换 RTT 软件 I2C bitbang 或原生 I2C 驱动

**步骤**：
1. 提取 `hal_i2c_lld.c`
2. 适配到 STM32F7 I2C v2 外设

**变更文件**：
| 文件 | 操作 |
|------|------|
| `libraries/AP_HAL_RTT/drivers/hal_i2c_lld.c` | **新建** |
| `libraries/AP_HAL_RTT/I2CDevice.cpp` | **修改** |

**验证**：
- IST8310 磁力计在 I2C3 上可读
- 罗盘数据流正常

---

## 执行方案

**用 Claude Code 来干**——具体原因是：

| 任务 | 谁执行 | 原因 |
|------|--------|------|
| ChibiOS LLD 代码提取+适配 | **Claude Code** | 完整终端+文件访问，逐行移植 |
| RTT AP_HAL 接口调整 | **Hermes (我)** | 需要理解 ArduPilot HAL 整体架构 |
| 编译+烧录+验证 | **Claude Code** | 需要 gdb/openocd 交互 |
| 整体进度管理 | **Hermes (我)** | kanban 调度、飞书汇报 |

**推荐工作流**：
```
Hermes: [kanban] dispatch CC-PHASE1-SPI
  └→ CC: 提取 hal_spi_lld.c → 适配 → 编译 → 烧录 → 验证
      └→ CC: kanban_complete(summary="SPI LLD移植完成, WHO_AM_I=0x89")
Hermes: 验收 → [kanban] dispatch CC-PHASE2-ADC
  ...
```

## 风险与权衡

| 风险 | 缓解 |
|------|------|
| ChibiOS SPIDriver 结构体耦合深 | 改接口为直接传寄存器基址 |
| DMA 中断处理与 RTT 嵌套中断不兼容 | 使用 RTT 中断注册 API |
| ChibiOS LLD 依赖 STM32 HAL | 已集成在 modules/ 中 |
| 工作量预估不足 | Phase 1 先执行，评估后再决定是否继续 |

## 开问题

1. **AP_HAL_RTT 结构要不要保留？** 建议保留——ArduPilot 的 AP_HAL 层抽象不变，只替换底层的硬件驱动代码
2. **Driver 目录结构**：`libraries/AP_HAL_RTT/drivers/` 还是 `libraries/AP_HAL_RTT/LLD/`？建议前者
3. **Phase 1 完成后是否自动推进？** 建议看结果再决定

## 立即行动

先执行 Phase 1（SPI，最关键）：
1. 读 ChibiOS `hal_spi_lld.c` 完整源码
2. 读当前 RTT `SPIDevice.cpp` 
3. 提取 ChibiOS 核心代码 + 适配
4. 编译 → 烧录 → 验证 ICM20689 WHO_AM_I
5. 如有 IMU 数据 → 自动进 Phase 2
