# RCInput.cpp 逐行自查报告

**文件**: libraries/AP_HAL_RTT/RCInput.cpp (106 行)
**参考**: libraries/AP_HAL_ChibiOS/RCInput.cpp (163 行)
**日期**: 2026-05-23
**审查者**: ce-rcinput

---

## 检查项 1: 逐行对比 ✅

### 1.1 文件头与许可证 (ChibiOS L1-16 vs RTT L1-7)
- ChibiOS: GPLv3 标准头
- RTT: 自定义 RTT 头注释 + 设计说明
- **判定**: ✅ RTT 使用自己的文件头是合理的。RTT 注释解释了设计理念（local buffer + mutex 模式），这比 ChibiOS 更清晰。

### 1.2 include 部分 (ChibiOS L18-29 vs RTT L9-17)
| ChibiOS | RTT | 差异 |
|---------|-----|------|
| `#include <hal.h>` | — | ChibiOS 特有的 hal.h |
| `#include "RCInput.h"` | `#include "RCInput.h"` | ✅ 相同 |
| `#include "hal.h"` | — | ChibiOS 特有的 local hal.h |
| `#include "hwdef/common/ppm.h"` | — | PPM 解码在 ChibiOS 中直接集成 |
| `#include <AP_RCProtocol/AP_RCProtocol_config.h>` | `#include <AP_RCProtocol/AP_RCProtocol_config.h>` | ✅ 相同 |
| `#include <AP_Math/AP_Math.h>` | — | 未使用在 RTT 的 RCInput.cpp (ChibiOS 也不直接使用库) |
| `#include <GCS_MAVLink/GCS.h>` | — | 未使用在 RTT 的 RCInput.cpp (ChibiOS 也不直接使用库) |
| — | `#include <AP_HAL/AP_HAL.h>` | RTT 需要此头文件 (用于 AP_HAL::micros()) |
| — | `#include <AP_RCProtocol/AP_RCProtocol.h>` | RTT 显式包含 (ChibiOS 在 .h 中包含) |
| — | `#include <string.h>` | RTT 显式包含 (ChibiOS 可能隐式包含) |

**判定**: ✅ 差异合理，RTT 针对 RT-Thread 环境做了必要的调整。

### 1.3 宏定义与命名空间 (ChibiOS L30-32 vs RTT L19-20)
- ChibiOS L30: `#define SIG_DETECT_TIMEOUT_US 500000` — 未在 RCInput.cpp 中使用，用于 sig_reader
- ChibiOS L31: `using namespace ChibiOS;` — 命名空间使用
- ChibiOS L32: `extern const AP_HAL::HAL& hal;` — 未在此文件中实际使用
- RTT L19-20: `namespace RTT { ... }` — RTT 命名空间包裹

**判定**: ✅ RTT 省略了 ChibiOS 专有的宏和声明，使用自己的命名空间。

### 1.4 init() (ChibiOS L33-51 vs RTT L22-28)

| 功能 | ChibiOS | RTT | 一致？ |
|------|---------|-----|--------|
| AP::RC().init() | L36-37 | L24-26 | ✅ |
| ICU timer attach | L39-43 (HAL_USE_ICU) | — | 🔄 RTT 无 ICU |
| EICU init | L45-48 (HAL_USE_EICU) | — | 🔄 RTT 无 EICU |
| pulse_input_enabled = true | L42, L47 | — | 🔄 RTT 无 ICU |
| _init = true | L50 | L27 | ✅ |

**判定**: ✅ RTT 无 ICU 硬件抽象，省略涉及 ICU/EICU 的代码是正确的。

### 1.5 pulse_input_enable() (ChibiOS L57-65 vs RTT L80-83)
- ChibiOS: 设置 `pulse_input_enabled` 标志 + 条件调用 `sig_reader.disable()`
- RTT: `(void)enable;` 空操作

**判定**: 🔶 RTT 无 sig_reader/signal capture，空实现是合理的。**但需要注释说明**。

### 1.6 new_input() (ChibiOS L67-80 vs RTT L30-42)
完全一致: `_init` 检查 → WITH_SEMAPHORE → 比较/更新 timestamp → 返回 valid

**判定**: ✅ 完全一致。

### 1.7 num_channels() (ChibiOS L82-88 vs RTT L44-50)
完全一致: `_init` 检查 → 返回 `_num_channels`

**判定**: ✅ 完全一致。

### 1.8 read(uint8_t) (ChibiOS L90-101 vs RTT L52-63)
完全一致: `_init` + 通道边界检查 → mutex → 读取 `_rc_values[channel]` → 返回

注意: RTT 参数名 `ch` vs ChibiOS `channel` — 功能无差异。RTT 的 .h 中也是 `ch`。

**判定**: ✅ 功能完全一致。

### 1.9 read(uint16_t*, uint8_t) (ChibiOS L103-117 vs RTT L65-78)

| 步骤 | ChibiOS | RTT | 一致？ |
|------|---------|-----|--------|
| _init 检查 | L105-107 | L67-68 | ✅ |
| len 上限裁剪 | L109-111 | L70-72 | ✅ |
| WITH_SEMAPHORE + memcpy | L112-115 | L73-76 | ✅ |
| **返回** | L116: `return len;` | L77: `return MIN(len, _num_channels);` | ❌ **不一致** |

**判定**: ❌ **Bug — 返回值不一致**。ChibiOS 返回请求的 `len`，RTT 返回实际可用的 `MIN(len, _num_channels)`。
虽然 RTT 行为更符合语义（返回实际填充的通道数），但根据 1:1 移植铁律应匹配 ChibiOS。

### 1.10 _timer_tick() (ChibiOS L119-161 vs RTT L85-104)

| 功能 | ChibiOS | RTT | 一致？ |
|------|---------|-----|--------|
| _init 检查 | L121-123 | L87-89 | ✅ |
| AP_RCProtocol &rcprot = AP::RC() | L125 | L91 | ✅ |
| ICU pulse_list 处理 (L127-136) | `sig_reader.sigbuf.readptr()` → `process_pulse_list()` | — | 🔄 RTT 无 ICU |
| EICU pulse 处理 (L138-145) | `sig_reader.read()` → `process_pulse()` | — | 🔄 RTT 无 EICU |
| **rcprot.update()** | — | L92 | 🔄 RTT 用 polling 替代硬件捕获 |
| rcprot.new_input() 检查 | L147 | L94 | ✅ |
| WITH_SEMAPHORE + 数据读取 | L148-155 | L95-102 | ✅（_rssi, _rx_link_quality, _num_channels, _rc_values） |

**判定**: ✅ RTT 用 `rcprot.update()` 替代 ICU 脉冲捕获路径是架构上必需的。RTT 无 ICU 硬件周期捕获能力，通过软件轮询协议解码（适合 SBUS/DSM/CRSF over UART）。

---

## 检查项 2: 寄存器/外设配置 ✅
- RCInput.cpp 不直接操作寄存器
- ICU 定时器配置在 ChibiOS 中在 hwdef.h/halconf.h 级别，不在 RCInput.cpp 中
- RTT 无 ICU，不适用

## 检查项 3: 函数签名 ✅
所有函数签名一致:
| 函数 | ChibiOS | RTT | 一致？ |
|------|---------|-----|--------|
| init() | `void init() override` | `void init() override` | ✅ |
| new_input() | `bool new_input() override` | `bool new_input() override` | ✅ |
| num_channels() | `uint8_t num_channels() override` | `uint8_t num_channels() override` | ✅ |
| read(uint8_t) | `uint16_t read(uint8_t ch) override` | `uint16_t read(uint8_t ch) override` | ✅ |
| read(uint16_t*, uint8_t) | `uint8_t read(uint16_t*, uint8_t) override` | `uint8_t read(uint16_t*, uint8_t) override` | ✅ |
| pulse_input_enable() | `void pulse_input_enable(bool) override` | `void pulse_input_enable(bool) override` | ✅ |
| _timer_tick() | `void _timer_tick(void)` | `void _timer_tick(void)` | ✅ |
| get_rssi() | `int16_t get_rssi(void) override` | `int16_t get_rssi(void) override` | ✅ |
| get_rx_link_quality() | `int16_t get_rx_link_quality(void) override` | `int16_t get_rx_link_quality(void) override` | ✅ |

## 检查项 4: 错误处理/超时逻辑 ✅
- `_init` 检查: 全部一致 (new_input, num_channels, read 单通道, read 批量, _timer_tick)
- 通道边界检查: 全部一致 (`MIN(RC_INPUT_MAX_CHANNELS, _num_channels)`)
- mutex 保护: 全部一致
- `SIG_DETECT_TIMEOUT_US` 未在 ChibiOS RCInput.cpp 中使用（在 sig_reader 中），RTT 无 sig_reader → 不适用

## 检查项 5: DMA/中断处理 ✅
- ChibiOS 的 DMA 由 SoftSigReader 管理 (在 `sig_reader.attach_capture_timer()` 中配置)
- RTT 不使用 DMA/ICU 路径，使用 `rcprot.update()` 软件轮询
- **判定**: 架构差异，非 RCInput.cpp 自身问题

## 检查项 6: D-Cache (STM32F7) ✅
- ChibiOS 在 SoftSigReader 中处理 DMA buffer 的 D-Cache 同步，不在 RCInput.cpp 中
- RTT 不使用 DMA 共享 buffer，不涉及 D-Cache 问题
- **判定**: 无遗漏

## 检查项 7: MPU 配置 ✅
- 无 MPU 配置在 RCInput.cpp 级别
- **判定**: 不适用

## 检查项 8: 代码注释 ✅
- RTT 文件头注释 (L1-7) 解释了设计模式和目的，优于 ChibiOS
- `pulse_input_enable()` 缺少注释说明它是 stubbed

---

## 发现问题汇总

| # | 严重程度 | 行号 (RTT) | 描述 | ChibiOS 参考行 |
|---|---------|-----------|------|---------------|
| **1** | **中** | **L77** | `read(uint16_t*, uint8_t)` 返回 `MIN(len, _num_channels)`，ChibiOS 返回 `len` | L116 |
| 2 | 低 | L80-83 | `pulse_input_enable()` 无注释说明 stubbed 原因 | L57-65 |

---

## 修复计划
1. **修复 Issue #1**: 将 L77 的 `return MIN(len, _num_channels);` 改为 `return len;`
2. **Issue #2**: 添加注释说明 RTT 无 ICU 所以 pulse_input_enable 是空操作
3. 编译验证 → git commit

