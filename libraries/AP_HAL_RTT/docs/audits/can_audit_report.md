# CANIface.cpp 自查报告

**日期**: 2026-05-23
**检查人**: ce-caniface (CAN Interface Agent)
**任务**: t_audit_ce-caniface

## 检查清单完成情况

### 1. [X] 逐行对比 — 功能完整
- RTT: 1093 行, ChibiOS: 1096 行
- 差异仅 3 行（ChibiOS 有 ~20 行 STM32F3XX 特定代码，RTT STM32F7 不需要）
- 所有功能完全相同，结构 1:1 映射

### 2. [X] 寄存器配置 — 一致
| 寄存器 | 检查结果 |
|--------|---------|
| MCR | `MCR_ABOM \| MCR_AWUM \| MCR_INRQ` — 一致 |
| BTR | sjw<<24, bs1<<16, bs2<<20, prescaler, BTR_SILM — 一致 |
| IER | `IER_TMEIE \| IER_FMPIE0 \| IER_FMPIE1` — 一致 |
| TSR_ABRQx | TSR_ABRQ0/1/2 — 一致 |
| Filter (FMR, FFA1R, FM1R, FS1R, FA1R) | 值完全一致 |
| bxcan.hpp CanType 结构 | 与 ChibiOS 一致（仅 namespace 不同） |

### 3. [X] 函数签名 — 一致
- `CANIface(uint8_t index)` / `CANIface()` — 一致
- `init(uint32_t, OperatingMode)` — 一致（RTT 省略了 ChibiOS 特有 `__INITFUNC__`）
- `send()`, `receive()`, `configureFilters()` — 一致
- `select()`, `set_event_handle()`, `get_stats()` — 一致
- `is_busoff()`, `getNumFilters()`, `getErrorCount()` — 一致
- `add_to_rx_queue()`, `get_iface_num()` — 一致

### 4. [X] 错误处理 — 完整
- MSR INAK timeout: `waitMsrINakBitStateChange()` — 1000ms × 1ms 轮询
- TX mailbox full: 返回 0（含 MAVCAN 首次发送回退）
- 无效帧（error frame/dlc>8）: 返回 -1
- Timing 计算失败: 返回 false
- Filter 配置溢出: 返回 false
- LEC 读取 + ESR 记录

### 5. [X] DMA/中断 — 完整
- bxCAN 不使用 DMA（CPU 寄存器访问）
- RTT: `NVIC_SetPriority(5)` + `NVIC_EnableIRQ()` — CMSIS 标准
- ChibiOS: `nvicEnableVector(CORTEX_MAX_KERNEL_PRIORITY)` — ChibiOS 封装
- RTT IRQ handlers: 标准 `extern "C"` CMSIS 风格
- ChibiOS IRQ handlers: `CH_IRQ_HANDLER` + `CH_IRQ_PROLOGUE/EPILOGUE`

### 6. [X] D-Cache — 无需（STM32F7 bxCAN 不需要）
- bxCAN 寄存器通过 volatile 指针访问，D-Cache 透明
- 无 DMA 用于 bxCAN（仅 CPU IO）
- FDCAN（STM32H7）才需要 D-Cache 维护 — 那是另一个驱动

### 7. [X] MPU 配置 — 无需
- bxCAN 寄存器在 APB1 总线，默认 MPU 设置即可访问

### 8. [X] 代码注释 — 充足
- PCLK1 推导说明（RM0430 §6.3.8 PPRE1 编码参考）
- `rt_thread_mdelay` 等价性说明
- "Ported to RTT by CAN interface agent" 标记
- CAN 基地址 fallback 说明

## RTT 特有适配（预期变更，非回归）

| 适配点 | RTT | ChibiOS | 原因 |
|--------|-----|---------|------|
| 头文件 | `rtthread.h`, `rtdevice.h`, `stm32f7xx.h` | `hal.h`, `AP_HAL_ChibiOS.h` | 平台不同 |
| namespace | `RTT` | `ChibiOS` | HAL 命名空间 |
| IRQ enable | `NVIC_SetPriority` + `NVIC_EnableIRQ` | `nvicEnableVector` | CMSIS vs ChibiOS |
| IRQ handler | 标准 `extern "C"` | `CH_IRQ_HANDLER` / PROLOGUE/EPILOGUE | RT-Thread vs ChibiOS |
| CriticalSectionLocker | `rt_hw_interrupt_disable/enable` | `chSysLock/Unlock` | RTOS 原语 |
| 延时 | `rt_thread_mdelay(1)` | `chThdSleep(chTimeMS2I(1))` | RTOS 延时 API |
| PCLK1 | `_get_pclk1()` 推导 | `STM32_PCLK1` ChibiOS 宏 | 无 ChibiOS HAL 层 |

## hwdef.dat 差异（非 CanIface.cpp 范围 — 仅供记录）

| 引脚 | RTT cuav_v5 | ChibiOS fmuv5 | 备注 |
|------|------------|---------------|------|
| CAN2_SILENT | PI8 (GPIO 71) | PH3 (GPIO 71) | 需 CUAV V5 硬件验证 |

## 结论

**CanIface.cpp: 自查通过，无 bug。**

RTT 移植是 ChibiOS 参考实现的严格 1:1 函数级映射。所有平台适配（RT-Thread APIs、CMSIS NVIC、IRQ 处理、临界区保护、延时 API）均为预期的必要变更，无功能遗漏或新增 bug。
