# RTT 全驱动 CMSIS 寄存器重写方案

> 廖博士 2026-05-25 下达
> **铁律**：所有外设驱动用 CMSIS 寄存器直写，风格贴 ChibiOS LLD。不用 CherryUSB，不用 STM32 HAL。
> RT-Thread 框架层（设备注册/线程/信号量）保留，只替换外设寄存器控制层。

---

## 1. 现状快照

| 项目 | 值 |
|------|-----|
| 项目路径 | `/data/firmare/pogo-apm` |
| 目标板 | CUAV V5 (STM32F767) |
| Flash 地址 | `0x08008000` |
| 编译命令 | `scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` |
| 上次固件验证 | `setup_stage=0x28b` (setup 完成), `main_loop=0` (SPI IMU 不通) |
| ADC | ✅ 已寄存器重写（DMA 循环采样） |
| SPI | ❌ 原版 RT-Thread `drv_spi.c`（HAL 库、FRXTH=0） |
| USB CDC | ❌ CherryUSB（CSRSTDONE bug、无 SOF 自愈） |
| UART | ❌ STM32 HAL `drv_usart.c` |
| I2C | ❌ 软 bitbang（非寄存器硬件 I2C） |
| 启动/连接脚本 | ❌ RTT 16KB 单栈 vs ChibiOS 1KB+1KB 双栈 |
| 时钟初始化 | ❌ STM32 HAL SystemClock_Config vs CMSIS SystemInit |
| 看门狗 | ❌ HAL 库 IWDG vs ChibiOS 寄存器级 |

## 2. 架构总纲

```
┌──────────────────────────────────────────────────┐
│ AP_HAL_RTT/ 层（C++ 框架）─ 不动结构              │
│   SPIDevice.cpp/h    ─ 保持现有 API 签名          │
│   UARTDriver.cpp/h   ─ 保持现有 API 签名          │
│   I2CDevice.cpp/h    ─ 保持现有 API 签名          │
│   ...                                              │
└──────────────┬───────────────────────────────────┘
               │ 调用
┌──────────────▼───────────────────────────────────┐
│ modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/ │
│   drivers/                                         │
│     drv_spi.c      ─ 只改寄存器控制函数体          │
│     drv_usart.c    ─ 只改寄存器控制函数体          │
│     ...                                            │
└──────────────┬───────────────────────────────────┘
               │ 直接操作
┌──────────────▼───────────────────────────────────┐
│ STM32F7 外设寄存器（CMSIS）                        │
│   SPI_TypeDef *spi = SPI1;                        │
│   spi->CR1 = ...;  spi->DR = ...;                 │
│   USART1->CR1 = ...;                              │
└──────────────────────────────────────────────────┘
```

**USB CDC 例外**：CherryUSB 整个替换掉，接入已有 `hal_usb_lld_rtt.c`（724行 DWC2 纯寄存器驱动）。

## 3. 关键 ChibiOS 架构差异（必须对齐）

### 3.1 链接脚本 — 双 RAM 区域 + 双栈

| 项目 | RTT 当前 | ChibiOS 方式 | 对齐价值 |
|------|---------|-------------|---------|
| RAM 区域 | 单区域 `0x20000000-0x20080000` 512K | 分 `SRAM1(0x20020000,384K)` + `DTCM(0x20000000,128K)` | DTCM 存放 .bss 和栈，SRAM1 放 .data 和堆，DMA 缓冲区强制在 SRAM1 |
| 栈 | 单栈 16KB（`_system_stack_size = 0x4000`）| **双栈**：MSP 1KB (ISR) + PSP 1KB (主线程) | 释放 14KB 堆给 RT-Thread 线程 |
| .data 位置 | 在 RAM 区域开头 | **SRAM1**（DMA 可达） | `.data` 中的变量可被 DMA 写入 |
| .bss 位置 | 在 .stack 后面 | **DTCM**（CPU 零等待，无 DMA） | BSS 变量（全局状态、计数器）无等待访问 |
| .nocache 段 | 无 | DTCM 中的 NOLOAD 段 | DMA 缓冲区的天然位置（无 D-Cache 冲突问题） |

**ChibiOS 链接脚本模板**（`STM32F76xxI.ld`）：
```ld
MEMORY {
    ram0 (wx) : org = 0x20020000, len = 384k   /* SRAM1+2: DATA, HEAP */
    ram3 (wx) : org = 0x20000000, len = 128k   /* DTCM-RAM: stacks, BSS, NOCACHE */
}

REGION_ALIAS("MAIN_STACK_RAM", ram3);      /* MSP 在 DTCM */
REGION_ALIAS("PROCESS_STACK_RAM", ram3);    /* PSP 在 DTCM */
REGION_ALIAS("DATA_RAM", ram0);             /* .data 在 SRAM1 */
REGION_ALIAS("BSS_RAM", ram3);              /* .bss 在 DTCM */
REGION_ALIAS("HEAP_RAM", ram0);             /* 堆在 SRAM1 */
```

**RTT 需要改**：
1. 分 RAM 区域（SRAM1 + DTCM）
2. 双栈：`.mstack` (1KB) + `.pstack` (1KB) 替代单 `.stack` (16KB)
3. `.data` → SRAM1, `.bss` → DTCM
4. `.sram1` 段强制 DMA 缓冲区在 SRAM1
5. 堆定义在 SRAM1 空闲空间

**改动文件**：`libraries/AP_HAL_RTT/hwdef/common/board/linker_scripts/link.lds`

### 3.2 启动文件 — 双栈模式 + 寄存器级硬件初始化

**ChibiOS `crt0_v7m.S` 启动序列（第 200-350 行）：**

```asm
_crt0_entry:
    cpsid i                          ; 关中断

    ; 1. MSP = __main_stack_end__    ; 1KB 在 DTCM
    ldr r0, =__main_stack_end__
    msr MSP, r0

    ; 2. PSP = __process_stack_end__  ; 1KB 在 DTCM（RTT 完全缺失！）
    ldr r0, =__process_stack_end__
    msr PSP, r0

    ; 3. VTOR = _vectors (由链接脚本提供)
    ldr r0, =_vectors
    ldr r1, =SCB_VTOR
    str r0, [r1]

    ; 4. FPU 初始化 — 直接写寄存器，不调任何库
    movw r0, #CRT0_FPCCR_INIT
    str r0, [SCB_FPCCR]              ; FPCCR = ASPEN|LSPEN
    movw r0, #CRT0_CPACR_INIT
    str r0, [SCB_CPACR]              ; CPACR = FULL_ACCESS  (cp10|cp11)
    mov r0, #0
    vmsr FPSCR, r0
    str r0, [SCB_FPDSCR]

    ; 5. CONTROL = PSP | PRIVILEGED   ← 进入双栈模式！
    movs r0, #CRT0_CONTROL_INIT      ; 0x02 (USE_PSP)
    msr CONTROL, r0
    isb

    ; 6. Core init: I-cache, D-cache, branch prediction
    bl __cpu_init

    ; 7. Early init: clock (HSE→PLL→216MHz)
    bl __early_init

    ; 8. Fill stacks with 0x55555555
    ; 9. Copy .data to SRAM1
    ; 10. Zero .bss in DTCM
    ; 11. _crt0_main → hwinit → main
```

**RTT 当前启动（`startup_rtt_override.S`）差距：**

| 阶段 | ChibiOS | RTT 当前 | 影响 |
|------|---------|---------|------|
| 栈模式 | **双栈** MSP(异常)+PSP(主线程) | 单栈 MSP 16KB | ISR 和主线程争同一个栈，栈越大 + 堆越少 |
| MSP 大小 | 1KB (DTCM) | 16KB (RAM 开头) | 14KB 浪费 |
| VTOR 设置 | linker 符号 `_vectors` | 硬编码 `0x08008000` | 一致 |
| FPU 初始化 | 寄存器直写（FPCCR/CPACR/FPDSCR） | 在 HSE 初始化路径中（`SystemClock_Config` 内） | FPU 状态不确定更久 |
| 双栈切换 | `msr CONTROL, #2` | **缺失** | 始终单栈模式 |
| I/D-Cache | `__cpu_init` 在 crt0 中 | ？可能在 SystemInit 中 | 顺序差异 |
| 时钟 | `__early_init` → 寄存器直写 | `SystemClock_Config()` via HAL | HAL 库有额外层 |

**RTT 需要改**：`startup_rtt_override.S` → 增加 PSP 设置、双栈模式、直接寄存器 FPU 初始化

### 3.3 时钟初始化 — CMSIS SystemInit 直写

ChibiOS 的 `__early_init` 调 `system_stm32f7xx.c` 中的 `SystemInit()`（**标准 CMSIS 实现**，纯寄存器）：

```c
void SystemInit(void) {
    // 1. FPU 已在 crt0 中初始化
    // 2. 配置 Flash 等待周期 (FLASH_ACR = LATENCY_7 | PRFTEN | ARTEN)
    FLASH->ACR = 0x07 | (1<<8) | (1<<9);
    // 3. 配置 RCC: HSE→PLL→216MHz
    RCC->CR = RCC_CR_HSION | RCC_CR_HSEON;     // 使能 HSI(备份) + HSE
    while(!(RCC->CR & RCC_CR_HSERDY));          // 等 HSE 就绪
    RCC->PLLCFGR = ...;                         // PLL M/N/P/Q
    RCC->CR |= RCC_CR_PLLON;
    while(!(RCC->CR & RCC_CR_PLLRDY));
    RCC->CFGR = ...;                             // SW = PLL
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}
```

**RTT 当前**：`stm32f7_clock_ll.c` 中有 `SystemClock_Config()` — 也是 CMSIS 寄存器，但有 3 级 fallback（HSE→HSE_BYPASS→HSI）。**这可能是不稳定的来源**。

**需确认**：是否真用了 HAL，还是已经 CMSIS 了。如果已是 CMSIS，则差距不大。

### 3.4 看门狗初始化 — 寄存器级 IWDG

**ChibiOS `stm32_watchdog.c`**：
```c
// IWDG 初始化 — 直接寄存器
void stm32_watchdog_init(void) {
    // 写 IWDG_KR 解锁：0x5555 允许写 PR/RLR
    IWDG->KR = IWDG_KR_KEY_WRITE;      // 0x5555
    IWDG->PR = IWDG_PR_PR_128;         // 预分频 /128
    IWDG->RLR = IWDG_RLR_RL(1250);     // 重载值 ≈ 2s @ LSIRDY
    IWDG->KR = IWDG_KR_KEY_RELOAD;     // 0xAAAA — Reload
    IWDG->KR = IWDG_KR_KEY_ENABLE;     // 0xCCCC — Start
}

void stm32_watchdog_pat(void) {
    IWDG->KR = IWDG_KR_KEY_RELOAD;     // 0xAAAA — 喂狗
}
```

**RTT 当前**：`set_system_initialized()` 中用 HAL 调用启动 IWDG，且在 setup 之后，时序不对。

**RTT 需要改**：在 `HAL_RTT_Class.cpp` 中，setup 之前用**寄存器直写**启动 IWDG，setup 完成后 timer tick 每 1ms 用寄存器喂狗。

## 4. 各外设驱动重写方案

### P0. SPI — `drv_spi.c` 两个函数体

| 函数 | 当前（HAL） | 改为（CMSIS 寄存器） |
|------|------------|-------------------|
| `stm32_spi_init()` | `HAL_SPI_Init(&hspi)` + `HAL_SPI_TransmitReceive` | CR1 = MSTR\|SSM\|SSI\|CPOL\|CPHA\|BR; CR2 = FRXTH\|DS_8BIT; 直接读写 DR |
| `stm32_spi_send_recv()` | `HAL_SPI_TransmitReceive` | 轮询 TXE/RXNE/BSY 标志，读写 DR |
| `stm32_spi_send()` | `HAL_SPI_Transmit` | 同上，只写 |
| `stm32_spi_recv()` | `HAL_SPI_Receive` | 同上，只读 |

**关键寄存器配置**（ChibiOS `spi_lld_start()` 参考）：

```c
// CR1
spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM;  // 主模式，软件 NSS
if (mode & SPI_MODE_CPHA) spi->CR1 |= SPI_CR1_CPHA;
if (mode & SPI_MODE_CPOL) spi->CR1 |= SPI_CR1_CPOL;
if (data_width == 16)     spi->CR1 |= SPI_CR1_DFF;
spi->CR1 |= derive_br(frequency) * SPI_CR1_BR_0;  // 动态波特率

// CR2 — 之前遗漏的就是这一步！
spi->CR2 = SPI_CR2_FRXTH | SPI_CR2_DS_8BIT;

// 开启 SPI
spi->CR1 |= SPI_CR1_SPE;

// ChibiOS 经典操作：dummy read 清 RXNE
(void)spi->DR;
```

**传输轮询**（直接 ChibiOS `spi_lld_polled_transfer` 逻辑）：

```c
for (size_t i = 0; i < len; i++) {
    while (!(spi->SR & SPI_SR_TXE));   // 等待 TX 空
    *(uint8_t*)&spi->DR = tx[i];
    while (!(spi->SR & SPI_SR_RXNE));  // 等待 RX 满（FRXTH=1 确保单字节触发）
    rx[i] = *(uint8_t*)&spi->DR;
}
while (spi->SR & SPI_SR_BSY);          // 等待传输完成
```

**✅ 改动量**：~120 行，只改 `drv_spi.c` 中 4 个函数体
**✅ 不动**：`AP_HAL_RTT/SPIDevice.cpp/h`、`WSPIDeviceManager`、`DeviceBus`

---

### P0. USB CDC — 替换 CherryUSB

| 组件 | 当前（CherryUSB） | 改为 |
|------|-----------------|------|
| DWC2 底层 | `usb_dc_dwc2.c` (CherryUSB port) | 接入已有 `hal_usb_lld_rtt.c` (724 行纯寄存器) |
| CDC ACM 协议 | `usbd_serial.c` | 新建 `usb_cdc_rtt.c`（贴 ChibiOS `usb_cdc.c`） |
| UARTDriver 集成 | 通过 CherryUSB 回调 | 直接调 LLD 的 EP write/read |

**`hal_usb_lld_rtt.c` 已有资产：**
- `usb_lld_init()` → DWC2 core reset + FIFO 分配 + 中断使能
- `usb_lld_start_out()` → OUT 端点配置
- `usb_lld_start_in()` → IN 端点配置
- `usb_lld_write_in()` → 写 IN FIFO
- `usb_lld_read_out()` → 读 OUT FIFO
- `usb_lld_serve_interrupt()` → GINTSTS 分发

**`usb_cdc_rtt.c` 需要编写：**
- SetLineCoding/GetLineCoding 请求处理
- DTR 检测（`g_dtr_active`/`dbg_dtr_set_cnt`）
- Bulk IN 数据发送（从 UARTDriver 的 ringbuffer 取数据）
- Bulk OUT 数据接收（写入 UARTDriver 的读缓冲区）
- SOF 1kHz 看门狗（ChibiOS `sduSOFHookI` 等价）

**去掉 CherryUSB 依赖：**
- 从 `rt_hw_usb_init()` 中移除 CherryUSB 初始化
- 从 `.config` 和 `rtconfig.h` 中移除 `BSP_USING_CHERRYUSB`

**✅ 改动量**：~500 行（新文件 `usb_cdc_rtt.c` + 改 `UARTDriver.cpp` + 改链接/配置）

---

### P1. I2C — 硬件 I2C 替代软 bitbang

当前 I2C3 是软 bitbang（`I2CDevice.cpp` 内的 GPIO 模拟）。改为硬件 I2C：

```c
I2C3->CR1 = I2C_CR1_PE;
I2C3->TIMINGR = calc_timing(100000, PCLK1);  // 100kHz
// 主传输：CR2 = SADD|RD_WRN|NBYTES|START|AUTOEND
// 等待 TXIS/RXNE → 读写 TXDR/RXDR
// 等待 TC/STOPF
```

**参考**：ChibiOS `i2c_lld.c` + RM0430 §38 I2C 寄存器

**✅ 改动量**：~200 行，新增 `drv_i2c_rtt.c`（或改 `I2CDevice.cpp` 内硬件初始化）

---

### P2. 其余外设（等确认后再细化）

| 模块 | 现状 | 寄存器方案 | 优先级 |
|------|------|-----------|--------|
| RCOutput (PWM) | 需确认 | TIM1/TIM4/TIM8 寄存器：ARR/CCR/BDTR | P2 |
| RCInput | 需确认 | TIM 输入捕获：CCMR/CCER/SR | P2 |
| ADC | ✅ 已寄存器化 | DMA 循环采样已验证 | ✅ 跳过 |
| Flash/Storage | 需确认 | Flash 控制器：FLASH_CR/FLASH_SR | P2 |
| CAN | 需确认 | bxCAN：CAN_TIxR/CAN_RxIR/CAN_ESR | P3 |
| GPIO | 需确认 | BSRR/ODR/IDR/MODER | P2 |

## 2. 架构总纲

```
┌──────────────────────────────────────────────────┐
│ AP_HAL_RTT/ 层（C++ 框架）─ 不动结构              │
│   SPIDevice.cpp/h    ─ 保持现有 API 签名          │
│   UARTDriver.cpp/h   ─ 保持现有 API 签名          │
│   I2CDevice.cpp/h    ─ 保持现有 API 签名          │
│   ...                                              │
└──────────────┬───────────────────────────────────┘
               │ 调用
┌──────────────▼───────────────────────────────────┐
│ modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/ │
│   drivers/                                         │
│     drv_spi.c      ─ 只改寄存器控制函数体          │
│     drv_usart.c    ─ 只改寄存器控制函数体          │
│     drv_i2c.c      ─ 只改寄存器控制函数体          │
│     ...                                            │
└──────────────┬───────────────────────────────────┘
               │ 直接操作
┌──────────────▼───────────────────────────────────┐
│ STM32F7 外设寄存器（CMSIS）                        │
│   SPI_TypeDef *spi = SPI1;                        │
│   spi->CR1 = ...;  spi->DR = ...;                 │
│   USART1->CR1 = ...;                              │
└──────────────────────────────────────────────────┘
```

**USB CDC 例外**：CherryUSB 整个替换掉，接入已有 `hal_usb_lld_rtt.c`（724行 DWC2 纯寄存器驱动）。

## 3. 各驱动重写方案

### P0. SPI — `drv_spi.c` 两个函数体

| 函数 | 当前（HAL） | 改为（CMSIS 寄存器） |
|------|------------|-------------------|
| `stm32_spi_init()` | `HAL_SPI_Init(&hspi)` + `HAL_SPI_TransmitReceive` | CR1 = MSTR\|SSM\|SSI\|CPOL\|CPHA\|BR; CR2 = FRXTH\|DS_8BIT; 直接读写 DR |
| `stm32_spi_send_recv()` | `HAL_SPI_TransmitReceive` | 轮询 TXE/RXNE/BSY 标志，读写 DR |
| `stm32_spi_send()` | `HAL_SPI_Transmit` | 同上，只写 |
| `stm32_spi_recv()` | `HAL_SPI_Receive` | 同上，只读 |

**关键寄存器配置**（ChibiOS `spi_lld_start()` 参考）：

```c
// CR1
spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM;  // 主模式，软件 NSS
if (mode & SPI_MODE_CPHA) spi->CR1 |= SPI_CR1_CPHA;
if (mode & SPI_MODE_CPOL) spi->CR1 |= SPI_CR1_CPOL;
if (data_width == 16)     spi->CR1 |= SPI_CR1_DFF;
spi->CR1 |= derive_br(frequency) * SPI_CR1_BR_0;  // 动态波特率

// CR2 — 之前遗漏的就是这一步！
spi->CR2 = SPI_CR2_FRXTH | SPI_CR2_DS_8BIT;

// 开启 SPI
spi->CR1 |= SPI_CR1_SPE;

// ChibiOS 经典操作：dummy read 清 RXNE
(void)spi->DR;
```

**传输轮询**（直接 ChibiOS `spi_lld_polled_transfer` 逻辑）：

```c
for (size_t i = 0; i < len; i++) {
    while (!(spi->SR & SPI_SR_TXE));   // 等待 TX 空
    *(uint8_t*)&spi->DR = tx[i];
    while (!(spi->SR & SPI_SR_RXNE));  // 等待 RX 满（FRXTH=1 确保单字节触发）
    rx[i] = *(uint8_t*)&spi->DR;
}
while (spi->SR & SPI_SR_BSY);          // 等待传输完成
```

**✅ 改动量**：~120 行，只改 `drv_spi.c` 中 4 个函数体
**✅ 不动**：`AP_HAL_RTT/SPIDevice.cpp/h`、`WSPIDeviceManager`、`DeviceBus`

---

### P0. USB CDC — 替换 CherryUSB

| 组件 | 当前（CherryUSB） | 改为 |
|------|-----------------|------|
| DWC2 底层 | `usb_dc_dwc2.c` (CherryUSB port) | 接入已有 `hal_usb_lld_rtt.c` (724 行纯寄存器) |
| CDC ACM 协议 | `usbd_serial.c` | 新建 `usb_cdc_rtt.c`（贴 ChibiOS `usb_cdc.c`） |
| UARTDriver 集成 | 通过 CherryUSB 回调 | 直接调 LLD 的 EP write/read |

**`hal_usb_lld_rtt.c` 已有资产：**
- `usb_lld_init()` → DWC2 core reset + FIFO 分配 + 中断使能
- `usb_lld_start_out()` → OUT 端点配置
- `usb_lld_start_in()` → IN 端点配置
- `usb_lld_write_in()` → 写 IN FIFO
- `usb_lld_read_out()` → 读 OUT FIFO
- `usb_lld_serve_interrupt()` → GINTSTS 分发

**`usb_cdc_rtt.c` 需要编写：**
- SetLineCoding/GetLineCoding 请求处理
- DTR 检测（`g_dtr_active`/`dbg_dtr_set_cnt`）
- Bulk IN 数据发送（从 UARTDriver 的 ringbuffer 取数据）
- Bulk OUT 数据接收（写入 UARTDriver 的读缓冲区）
- SOF 1kHz 看门狗（ChibiOS `sduSOFHookI` 等价）

**去掉 CherryUSB 依赖：**
- 从 `rt_hw_usb_init()` 中移除 CherryUSB 初始化
- 从 `.config` 和 `rtconfig.h` 中移除 `BSP_USING_CHERRYUSB`

**✅ 改动量**：~500 行（新文件 `usb_cdc_rtt.c` + 改 `UARTDriver.cpp` + 改链接/配置）

---

### P1. UART — `drv_usart.c` 寄存器化

| 函数 | 当前（HAL） | 改为 |
|------|------------|------|
| `stm32_uart_init()` | `HAL_UART_Init` | CR1=UE\|TE\|RE\|RXNEIE; BRR=SYSCLK/baud; 直接寄存器 |
| `stm32_uart_putc()` | `HAL_UART_Transmit` | 轮询 TXE，写 TDR |
| `stm32_uart_getc()` | `HAL_UART_Receive` | 轮询 RXNE，读 RDR |

**参考**：ChibiOS `uart_lld.c` 的 USART 配置 → 直接数据手册寄存器。

**✅ 改动量**：~150 行，只改 `drv_usart.c` 函数体

---

### P1. I2C — 硬件 I2C 替代软 bitbang

当前 I2C3 是软 bitbang（`I2CDevice.cpp` 内的 GPIO 模拟）。改为硬件 I2C：

```c
I2C3->CR1 = I2C_CR1_PE;
I2C3->TIMINGR = calc_timing(100000, PCLK1);  // 100kHz
// 主传输：CR2 = SADD|RD_WRN|NBYTES|START|AUTOEND
// 等待 TXIS/RXNE → 读写 TXDR/RXDR
// 等待 TC/STOPF
```

**参考**：ChibiOS `i2c_lld.c` + RM0430 §38 I2C 寄存器

**✅ 改动量**：~200 行，新增 `drv_i2c_rtt.c`（或改 `I2CDevice.cpp` 内硬件初始化）

---

### P2. 其余外设（等确认后再细化）

| 模块 | 现状 | 寄存器方案 | 优先级 |
|------|------|-----------|--------|
| RCOutput (PWM) | 需确认 | TIM1/TIM4/TIM8 寄存器：ARR/CCR/BDTR | P2 |
| RCInput | 需确认 | TIM 输入捕获：CCMR/CCER/SR | P2 |
| Flash/Storage | 需确认 | Flash 控制器：FLASH_CR/FLASH_SR | P2 |
| CAN | 需确认 | bxCAN：CAN_TIxR/CAN_RxIR/CAN_ESR | P3 |
| GPIO | 需确认 | BSRR/ODR/IDR/MODER | P2 |

---

## 执行顺序（按优先级）

```
Phase 0 ─ 启动链路对齐 ChibiOS
  ├─ 链接脚本：分 SRAM1/DTCM 区域，双栈 .mstack(1KB)+.pstack(1KB)
  ├─ 启动文件：PSP 设置、双栈模式、FPU 寄存器直写、__cpu_init
  ├─ 时钟：确认 SystemInit 已 CMSIS（不需改则跳过）
  ├─ 看门狗：寄存器级 IWDG，setup 前启动
  ├─ 编译 ✅ → OpenOCD 验证 RAM 布局 + 栈指针 + 时钟频率
  └─ 这部分改完即使 IMU 不通，内存释放 14KB 也有意义

Phase 1 ─ SPI 重写（改 drv_spi.c 4 个函数体）
  ├─ 改 stm32_spi_init → CMSIS 寄存器（含 CR2.FRXTH=1）
  ├─ 改 stm32_spi_send_recv → 轮询 TXE/RXNE/BSY
  ├─ 编译 ✅ → 烧录 ✅ → 验证 SPI1 CR1/CR2 寄存器
  └─ 通过后 MAVLink 应有 RAW_IMU 数据

Phase 2 ─ USB CDC 替换
  ├─ 编写 usb_cdc_rtt.c（CDC ACM 协议层）
  ├─ 接入 hal_usb_lld_rtt.c（DWC2 寄存器 LLD）
  ├─ 修改 UARTDriver.cpp CDC 通路
  ├─ 移除 CherryUSB 依赖
  ├─ 编译 ✅ → 烧录 ✅ → MAVLink HEARTBEAT 验证
  └─ 与 SPI 一起确认 main_loop 正常迭代

Phase 3 ─ UART 寄存器化（drv_usart.c）
  └─ 不影响 main_loop，优化性能

Phase 4 ─ I2C 硬件化（I2C3 寄存器）
  └─ 磁力计/外设通信
```

## 5. 验证方案

每 Phase 完成后必须通过以下验证：

### L1. 编译完整性
```bash
scons --v=ArduCopter --target=cuav_v5 -j$(nproc) → exit 0
```

### L2. 寄存器级验证（OpenOCD）
```bash
# SPI1 CR1: MSTR(bit2)=1, SPE(bit6)=1, BR bits 正确
echo "mdw 0x40013000 1" | nc -q1 localhost 4444

# SPI1 CR2: FRXTH(bit12)=1, DS(bit8-11)=0111
echo "mdw 0x40013004 1" | nc -q1 localhost 4444

# 无 HardFault
echo "mdw 0xE000ED28 1; mdw 0xE000ED2C 1" | nc -q1 localhost 4444
# → CFSR=0, HFSR=0
```

### L3. 功能验证
```bash
# MAVLink HEARTBEAT
python3 -c "
from pymavlink import mavutil
m = mavutil.mavlink_connection('/dev/ttyACM1')
m.wait_heartbeat(timeout=30)
print(f'HEARTBEAT ✅: type={m.type}')
"

# RAW_IMU 非零
m.recv_match(type='RAW_IMU', blocking=True, timeout=10)
# → xacc/yacc/zacc 至少一个非零
```

### L4. 持久性验证
```bash
# 独立 reset 后仍正常
echo -e "reset run" | nc -w2 localhost 4444
sleep 20
ls /dev/ttyACM* && python3 -c "from pymavlink import mavutil; ..."
# → 60s 后仍 HEARTBEAT
```

## 6. 风险与权衡

| 风险 | 等级 | 缓解 |
|------|------|------|
| SPI 寄存器配置不完整（如上次 FRXTH 遗漏） | 中 | 逐位对照 ChibiOS `spi_lld_start()`，读回寄存器验证 |
| D-Cache 导致 GPIO MODER RMW 被吞 | 中 | 写寄存器后加 DSB 屏障；需读回验证 |
| USB LLD 接入后端点配置不匹配 | 中 | 对照 ChibiOS `usb_cdc.c` 的端点描述符 |
| I2C 软→硬切换后时序不匹配 | 低 | 对照 I2C 时序分析仪的 logic analyzer |
| scons 构建 vs waf 构建二进制差异 | **高** | 如 Phase 1 通过后仍不工作，做二进制级对比 |
| ICM42688 替代 ICM20689 后 IMU 驱动不同 | 高 | 需要 ChibiOS CUAVv5 的 `AP_InertialSensor_Invensensev3` 驱动 |

## 7. 开放问题

1. **ICM42688 vs ICM20689**：CUAV V5 硬件实际焊接的是哪个？ChibiOS CUAVv5 用 ICM42688(PF11)，RTT hwdef 配的 ICM20689(PF2) — 需确认硬件事实。
2. **RCOutput/RCInput/Flash 当前用的什么驱动？** — 需要读代码确认后才能写方案。
3. **scons vs waf 二进制差异** — 如果 Phase 1 SPI 重写后 MAVLink 仍不工作，需做二进制级对比。

---

## 8. 执行计划

```
Step 0: 确认 ICMM42688 vs ICM20689 硬件事实（读 ChibiOS hwdef/原理图/板子）
Step 1: 改 drv_spi.c 中 stm32_spi_init() — 寄存器配置 + FRXTH
Step 2: 改 stm32_spi_send_recv/send/recv — 轮询 TXE/RXNE
Step 3: 编译 → 寄存器验证 → 烧录 → MAVLink 验证
Step 4: 若 SPI 通过 → 开始 USB CDC 替换
Step 5: 后续按优先级执行
```

**每个 Step 完成 = 编译通过 + 寄存器读回 + 烧录验证 + 向廖博士汇报进度。**
