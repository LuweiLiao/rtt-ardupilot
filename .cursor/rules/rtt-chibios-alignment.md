---
description: "ArduPilot RT-Thread HAL 对齐 ChibiOS 的守护规则。当用户在 pogo-apm 项目中讨论 RTT/RT-Thread/ChibiOS/HAL/飞控/参数/主循环/调度器 相关话题时自动激活。"
globs:
  - "libraries/AP_HAL_RTT/**"
  - "libraries/AP_HAL_ChibiOS/**"
  - "modules/rt-thread/**"
alwaysApply: false
---

# 守护目标: RTT HAL 100% 对齐 ChibiOS

## 使命

将 `AP_HAL_RTT`（RT-Thread HAL）在 CUAV V5 (STM32F767) 上的用户体验做到与 `AP_HAL_ChibiOS` 完全一致。对用户来说，就像换了一个 RTOS 的 ArduPilot —— 功能、性能、响应速度完全相同。

## 核心原则

**除了 RTOS 层（AP_HAL_RTT、RT-Thread BSP/drivers），其他 ArduPilot 代码不应修改。**

## 当前状态文件

- 主入口仍以 `.cursor/project/current-focus.md`、`.cursor/project/status.md`、`.cursor/project/open-issues.md` 为准
- 所有对齐进度: `.cursor/alignment-status.md`（专项对齐档案）
- 问题分析记录: `.cursor/alignment-issues.md`（专项对齐档案）
- 编译/刷写/调试指令: `.cursor/skills/rtt-build-flash-debug/SKILL.md`

## 核心方法论

### 1. 系统性思维，而非死抠细节
- 遇到性能问题，先从**架构层面**分析（调度模型、优先级体系、DMA/中断路径）
- 不要只修改一个文件，要理解**整条数据流**
- 对比 ChibiOS 的实现作为参考标准

### 2. 多维度并行分析（3次未解决规则）
- **当一个问题尝试解决 3 次以上仍未解决时，必须启动至少 3 个 subagent 从不同维度分析：**
  - Agent A: 硬件/驱动层（SPI/DMA/USB/DWC2寄存器/时钟/中断）
  - Agent B: OS 调度层（优先级/上下文切换/tick/互斥锁/死锁/饿死）
  - Agent C: ArduPilot 应用层（Scheduler/GCS/AP_Param/INS/wait_for_sample）
- 汇总各 agent 结论后形成系统性修复方案
- 不允许继续单一方向死抠

### 3. 每次运行前的守护检查
每次开始工作前，必须：
1. 先完成主 bootstrap：读取 `.cursor/project/current-focus.md`、`.cursor/project/status.md`、`.cursor/project/open-issues.md`
2. 再读取 `.cursor/alignment-status.md` 了解对齐专项进度
3. 再读取 `.cursor/alignment-issues.md` 了解已知问题和失败记录
4. 确认当前工作方向不在失败记录中（避免重蹈覆辙）
5. 工作完成后更新专项档案与 project 文档中对应层级的稳定结论

### 4. 编译→刷写→测试 闭环
每次修改后必须：
1. `scons --v=ArduCopter --target=cuav_v5 -j16`
2. 通过 OpenOCD+GDB 刷写（bootloader 0x08000000 + app 0x08008000）
3. 用 GDB 变量/pymavlink 验证效果
4. 更新 alignment-status.md 和 alignment-issues.md

### 5. 知识沉淀
每解决一个重要问题，将关键发现更新到：
- `.cursor/project/status.md` / `open-issues.md` / `decision-log.md` — 主治理系统中的稳定事实、未关闭问题和设计取舍
- `.cursor/alignment-status.md` — 对齐专项状态和进度
- `.cursor/alignment-issues.md` — 对齐专项问题分析和失败记录
- `.cursor/skills/` — 可复用工作流
- `.cursor/rules/` — 新的约束或规范

## 关键技术约束

### 线程优先级映射（当前方案，RTT 数字小=高优先级）
| 用途 | RTT 定义 | 值 | 说明 |
|------|----------|-----|------|
| MONITOR | RTT_PRIO_MONITOR | 5 | 最高 |
| BOOST | RTT_PRIO_BOOST | 6 | delay_microseconds_boost |
| TIMER/SPI | RTT_PRIO_TIMER/SPI | 7 | thread_create(PRIORITY_SPI) |
| MAIN | RTT_PRIO_MAIN | 8 | 主循环正常运行 |
| SPI_BUS | RTT_PRIO_SPI_BUS | 9 | DeviceBus SPI 轮询线程 |
| CAN | RTT_PRIO_CAN | 10 | |
| RCIN | RTT_PRIO_RCIN | 11 | |
| I2C | RTT_PRIO_I2C | 12 | |
| UART | RTT_PRIO_UART | 20 | UART RX/TX 线程 |
| STORAGE | RTT_PRIO_STORAGE | 21 | |
| IO | RTT_PRIO_IO | 22 | |

**注意**: RT_MAIN_THREAD_PRIORITY=10 是 RT-Thread 的 main 线程初始优先级。
ArduPilot 在 `_main_loop_entry()` 中将其提升到 RTT_PRIO_MAIN(8)。
`boost_end()` 恢复到 RTT_PRIO_MAIN(8)，不是 RT_MAIN_THREAD_PRIORITY(10)。

### 已知坑点（失败记录摘要）
1. **tx_pkt 512B**: DWC2 TX FIFO 只有 64B，增大 tx_pkt 导致 HardFault
2. **boost_end 用 RT_MAIN_THREAD_PRIORITY**: 导致主线程降到 prio 10 被 SPI bus(8) 饿死
3. **DeviceBus prio 9 + tick 补偿**: SPI 高频唤醒饿死主线程
4. **flash 到 0x08000000**: 必须保留 bootloader，app 从 0x08008000 开始
5. **GDB halt 干扰 USB**: halt MCU 会导致 USB 中断停止，host 认为设备断开
6. **usbipd detach/attach**: 产生多次 USB RESET，可能导致端点 MPS 被清零

### 关键文件
- `libraries/AP_HAL_RTT/Scheduler.h` — 优先级定义
- `libraries/AP_HAL_RTT/Scheduler.cpp` — 调度器核心
- `libraries/AP_HAL_RTT/DeviceBus.cpp` — SPI/I2C 设备周期线程
- `libraries/AP_HAL_RTT/UARTDriver.cpp` — USB/串口驱动
- `libraries/AP_HAL_RTT/HAL_RTT_Class.cpp` — HAL 入口和主线程优先级
- `modules/rt-thread/components/drivers/usb/cherryusb/platform/rtthread/usbd_serial.c` — USB CDC
- `modules/rt-thread/components/drivers/usb/cherryusb/demo/cdc_acm_rttchardev_template.c` — USB 事件
- `modules/rt-thread/components/drivers/usb/cherryusb/port/dwc2/usb_dc_dwc2.c` — DWC2 驱动
- `libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/rtconfig.h` — RT-Thread 内核配置

## 守护 Agent 工作协议

### 启动条件
当用户说"继续对齐"、"优化 RTT"、"守护任务" 或打开 AP_HAL_RTT 相关文件时：
1. 先做主 bootstrap，再补专项对齐档案
2. 找到下一个未完成的对齐项
3. 如果问题已尝试 3 次未解决 → 创建 3+ subagent 多维度分析
4. 实施修改 → 编译 → 刷写 → 测试
5. 更新 project 文档与专项状态文件

### 问题上报
如果遇到硬件断连、编译错误等阻塞问题，立即通知用户，不要自己循环重试超过 3 次。
