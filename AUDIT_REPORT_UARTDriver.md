# UARTDriver.cpp 逐行自查报告

**对比**: RTT `libraries/AP_HAL_RTT/UARTDriver.cpp` (685行)  
         vs ChibiOS `libraries/AP_HAL_ChibiOS/UARTDriver.cpp` (1836行)  
**日期**: 2026-05-23  
**状态**: 2 issue found (1 bug + 1 missing feature)  

---

## 逐项结果

### 1. [PASS] 逐行功能完整性

| RTT 函数 | 行号 | ChibiOS 对标 | 结论 |
|----------|------|-------------|------|
| `_begin()` | 146-282 | 233-519 | ✅ 结构等价（RTOS差异） |
| `_end()` | 284-304 | 643-663 | ✅ 等价 |
| `_flush()` | 306-309 | 665-675 | ✅ 等价 |
| `_available()` | 311-321 | 711-725 | ⚠️ 无 owner-thd 检查 |
| `_read()` | 415-423 | 754-773 | ⚠️ 无 owner-thd 检查 |
| `_write()` | 425-453 | 776-789 | ✅ 等价 |
| `_discard_input()` | 455-462 | 735-752 | ⚠️ 无 dropped bytes stats |
| `is_initialized()` | 464-467 | 677-680 | ✅ 等价 |
| `tx_pending()` | 469-472 | 682 | ✅ 等价 |
| `txspace()` | 474-480 | 727-733 | ✅ 等价 |
| `wait_timeout()` | 387-413 | 795-809 | ✅ 等价 |
| `set_flow_control()` | 589-592 | 1290-1374 | ❌ **STUB** |
| `configure_parity()` | 649-652 | 1408-1468 | ❌ **STUB** |
| `set_stop_bits()` | 654-657 | 1473-1504 | ❌ **STUB** |
| `set_RTS_pin()` | 659-663 | 1780-1793 | ⚠️ 无硬件时正确 |
| `set_CTS_pin()` | 665-669 | 1761-1774 | ⚠️ 无硬件时正确 |
| `set_options()` | 632-636 | 1560-1715 | ⚠️ 极简 stub |
| `receive_time_constraint_us()` | 671-678 | 1527-1536 | ❌ **BUG** |
| `get_usb_baud()` | 615-621 | 688-696 | ⚠️ 硬编码 |
| `get_usb_parity()` | 623-626 | 701-709 | ⚠️ 硬编码 |
| `bw_in_bytes_per_second()` | 100-119 | 122-127 | ✅ 等价 |
| `get_baud_rate()` | header:31 | header:129 | ✅ 等价 |

### 2. [PASS] 寄存器配置

RTT 使用 RT-Thread 设备框架 (`rt_device_open` / `rt_device_control`)，不直接操作 USART 寄存器（除 STM32F7 寄存器级 TX 绕过）。ChibiOS 使用 `sdStart()` + `sercfg`。不同层次但功能等价。

**发现**: RTT `_begin()` 的 `receive_time_constraint_us()` 方向反了（见第6项）。

### 3. [PASS] 函数签名

所有 `override` 的 API 函数签名与 `AP_HAL::UARTDriver` 基类完全一致。
头文件 `UARTDriver.h` 与 `AP_HAL_ChibiOS/UARTDriver.h` 接口一致。

### 4. [PASS] 错误处理/超时逻辑

- `wait_timeout()`: 有完整超时回退和 elapsed-time 追踪 ✅
- `_drain_writebuf_to_dev()`: dsb debug counters, zero-return 处理 ✅
- USB write fail count: 500 tick 阈值后清缓冲 ✅
- `_begin()` 中的 deferred open: 非阻塞，防止 setup 卡死 ✅

### 5. [PASS] DMA/中断

RTT 不使用 DMA TX/RX（设计如此——RT-Thread serial V1 层处理）。  
STM32F7 `uart_poll_tx()` 使用寄存器轮询替代 DMA，带超时(50000 nops) ✅  
无 D-Cache 同步问题（不使用 DMA）✅  

### 6. **[BUG] receive_time_constraint_us() 符号错误**

**RTT UARTDriver.cpp:671-678**:
```cpp
uint64_t last_receive_us = AP_HAL::micros64();
if (_baudrate > 0) {
    last_receive_us += ((uint64_t)nbytes * 1000000ULL * 10) / _baudrate;
}
return last_receive_us;
```

**ChibiOS UARTDriver.cpp:1527-1536**:
```cpp
uint64_t last_receive_us = _receive_timestamp[_receive_timestamp_idx];
if (_baudrate > 0 && !sdef.is_usb) {
    uint32_t transport_time_us = (1000000UL * 10UL / _baudrate) * (nbytes + available());
    last_receive_us -= transport_time_us;
}
return last_receive_us;
```

**差异**: 
1. RTT 用 `AP_HAL::micros64()`（当前时间），ChibiOS 用 `_receive_timestamp[]`（上次接收时间戳）
2. RTT **加上**传输时间，ChibiOS **减去**传输时间
3. RTT 不考虑 `available()` 队列中已有的数据

**影响**: 函数文档说"return timestamp estimate in microseconds for when the start of a nbytes packet arrived"。算出来的时间应该是过去（`micros64() - transport_time`），但 RTT 返回 **未来**时间（`micros64() + transport_time`）。这会导致 MAVLink 协议的时间和到达约束计算错误。

**修复**: 
- 方式A（追齐 ChibiOS）：添加 `_receive_timestamp` 存储并在 `_drain_rx_to_readbuf()` 中更新
- 方式B（最小修复）：`last_receive_us -= ...` 使用当前时间减传输时间（精确度较低但方向正确）

### 7. **[WARNING] set_flow_control() 是纯 stub**

RTT 不配置 USART_CR3.RTSE/CTSE，不操作 RTS/CTS 硬件引脚。  
ChibiOS 有完整的 DISABLE/AUTO/ENABLE/RTS_DE 四模式。

**影响**: 如果硬件 UART 需要硬件流控（如 TELEM1 GPS 模块），RTT 无法启用 RTS/CTS 握手。USB CDC 流控通过 CherryUSB 缓冲压力间接实现。

### 8. **[WARNING] configure_parity() + set_stop_bits() 是 no-op**

RTT 完全不配置 USART 奇偶校验或停止位。ChibiOS 使用 `sdStop()` + `sdStart()` 动态重配。

**影响**: 如果外设需要奇偶校验或 2 停止位，RTT 不会生效。但这些参数通常在 hwdef/board 配置中固定，运行时动态调整的需求较少。

### 9. [INFO] D-Cache / MPU

RTT UARTDriver 不使用 DMA，因此不需要 `stm32_cacheBufferInvalidate` / `stm32_cacheBufferFlush`。  
MPU 配置在 board 初始化中全局设置。✅ 无遗漏。

### 10. [INFO] 代码注释质量

RTT 文件有详细的中文/英文注释，标注了：
- 每个 RTT 特有陷阱（rt_completion_wait 死锁、deferred open 原因、D-Cache 写入问题）✅
- USB 诊断计数器 ✅
- debug counters 用于 GDB 检查 ✅

---

## 汇总

| 严重度 | 数量 | 描述 |
|--------|------|------|
| ❌ BUG | 1 | `receive_time_constraint_us()` 方向错误（加→减） |
| ⚠️ MISSING | 3 | `set_flow_control()` stub, `configure_parity()` no-op, `set_stop_bits()` no-op |
| ⚠️ WEAK | 4 | `set_options()` 极简, `get_usb_baud()` 硬编码, `_available()` 无 owner 检查, `_discard_input()` 无 stats |

## 修复计划

1. **[必须]** 修复 `receive_time_constraint_us()` 符号
2. **[考虑]** `set_flow_control()` 增加硬件 USART CR3 配置
3. **[考虑]** `configure_parity()` + `set_stop_bits()` 增加 RT-Thread serial 控制
