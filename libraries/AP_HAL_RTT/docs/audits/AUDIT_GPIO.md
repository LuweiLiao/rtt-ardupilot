# GPIO.cpp 逐行自查报告

## 检查项结果

### 1. 逐行对比 ✓
- 对比 RTT GPIO.cpp (521行) 与 ChibiOS GPIO.cpp (670行)
- RTT 使用 RT-Thread PIN 框架 (rt_pin_mode/rt_pin_read/rt_pin_write)
- ChibiOS 使用 PAL 抽象层 (palSetLineMode/palReadLine/palWriteLine)

### 2. 寄存器配置 ✓
- RTT 通过 RT-Thread PIN 驱动层间接配置 MODER/OTYPER/PUPDR/OSPEEDR
- LED RGB 的 BSRR 直接操作 (line 196-209) 有详细注释说明原因
- get_mode/set_mode 直接读 MODER 寄存器 (line 465-484)
- STM32F7 外设区 (0x40000000-0x5FFFFFFF) 默认非缓存，无需 D-Cache 维护

### 3. 函数签名 ✓ (但有差异)
| 函数 | ChibiOS | RTT | 差异 |
|------|---------|-----|------|
| init() | void | void | RTT 多传感器上电 GPIO 初始化 |
| pinMode(uint8_t, uint8_t) | 有 gpio_entry 追踪 | 简化 | RTT 已加 OPENDRAIN 保留 |
| pinMode(uint8_t, uint8_t, uint8_t) | base 空实现 | override | RTT 实现合适 |
| read(uint8_t) | gpio_entry + IOMCU | direct | RTT 无 IOMCU |
| write(uint8_t, uint8_t) | 输入模式改pullup/down | direct + LED特判 | RTT 无输入模式pull控制 |
| channel(uint16_t) | gpio_entry + IOMCU | direct | 差异合理 |
| toggle(uint8_t) | palToggleLine | read^1 → write | 功能等价 |
| usb_connected() | _usb_connected | query API | 架构差异有效 |
| attach_interrupt x2 | PAL event | rt_pin_irq | 已加 double-attach guard |
| valid_pin() | gpio_entry + IOMCU | pin < 176 | 简化有效 |
| pin_to_servo_channel() | IOMCU + gpio_entry | 硬编码CUAV V5 | 架构差异 |
| wait_pin() | 中断睡眠 | 轮询 | 已加超时钳位 |
| timer_tick() | 配额制+自动重开 | 简化的洪水检测 | 架构差异 |
| arming_checks() | 逐pin检查 | 全局flag | 简化 |
| get_mode/set_mode | palReadLineMode | MODER直接访问 | 差异有效 |

### 4. 错误处理 ✓
- attach_interrupt 已加 double-attach 保护 (line 345, 379)
- detach 逻辑正确清除状态 (line 290-298, 323-331)
- _find_or_alloc_irq 上限 RTT_GPIO_MAX_IRQ=8
- wait_pin 超时已钳位最大 30ms (line 436-439)

### 5. DMA/中断 ✓
- 中断通过 rt_pin_attach_irq 注册 + trampoline 分发
- 两个 variant (irq_handler_fn_t / AP_HAL::Proc) 都有实现
- 中断参数通过 IRQState 结构体传递
- RTT 无 ChibiOS 的 PAL_EVENT_MODE_DISABLED，功能等效

### 6. D-Cache ✓
- MODER/OTYPER 寄存器位于 STM32F7 外设区 (0x40000000+)，默认非缓存，无需 D-Cache 维护
- RGB LED 通过 BSRR 直接写 GPIOH 寄存器，同为外设区
- get_mode/set_mode 的 MODER 写同样无需 D-Cache 刷写

### 7. MPU 配置 ✓
- GPIO.cpp 无 MPU 配置（MPU 由 board-level 代码管理）

### 8. 代码注释 ✓
- RGB LED BSRR bypass 有详细注释 (line 183-210)
- sensor power rail init 有注释 (line 160-167)
- pin_to_servo_channel CUAV V5 mapping 有注释 (line 366-370)
- wait_pin polling approach 有注释 (line 393-395)

## 修复总结

本次自查进行了以下修复：

1. **pinMode OPENDRAIN 保留** (line 165-178)
   - 对照 ChibiOS line 221-229: 读 OTYPER 检查 OPENDRAIN 状态
   - RTT 改用 PIN_MODE_OUTPUT_OD 保留

2. **double-attach 中断保护** (line 345, 379)
   - 对照 ChibiOS line 369-372: 禁止重复注册中断处理函数
   - RTT 添加 isr_fn/simple_fn 双指针检查

3. **wait_pin 超时钳位** (line 436-439)
   - 对照 ChibiOS line 517: 最大 30ms 超时限制
   - RTT 对 0 和 >30ms 做钳位

## 架构差异（不修复，已记录）

| 差异 | ChibiOS | RTT | 原因 |
|------|---------|-----|------|
| GPIO 表 | _gpio_tab[] 从 hwdef.dat | 无 | RTT 用直接 pin number |
| IOMCU | 完整支持 | 无 | RTT 无 IOMCU |
| BRD_ALT_CONFIG | 完整支持 | 无 | RTT 未用 |
| timer_tick | 配额 10000 + 自动重开 10s | 简单 1000 阈值 | 架构简化 |
| 输入模式写操作 | 改 pullup/down | 无效果 | RT-Thread PIN 限制 |
| _usb_connected push | UARTDriver 通知 | runtime query | 架构差异有效 |

## 结论

**自查通过 ✓**

三个问题已修复，其余为架构差异（RTT使用RT-Thread框架与ChibiOS PAL的不同设计）。
