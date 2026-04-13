# Agent Trace

## 2026-04-11 Session: Bootloader jump_to_app → App 自由运行崩溃定位

### 2026-04-11 22:50 — Bootloader 分析完成，app 自由运行崩溃确认

**已完成：**
- 单步验证 jump_to_app() 全路径正确：
  - 地址范围：Reset_Handler(0x0810cfb1) < 0x08200000 → PASS
  - VTOR=0x08008000 设置正确
  - SP=0x2000d498, Reset=0x0810cfb1 正确加载
  - dsb/isb + 中断清理 + bx r4 成功跳转
- 断点模式验证 app 各阶段可达：
  - entry() ✓ → rtthread_startup() ✓ → rt_hw_board_init() ✓ → rtt_run_cpp_ctors() ✓ → main() ✓
- 自由运行验证：
  - rtt_dbg_main_loop_iterations 写入 0xDEADBEEF 后 1~3 秒仍为 0xDEADBEEF → **主循环从未执行**
  - CSR 复位标志清除后无新标志 → **不是芯片复位**
  - WWDG_CR=0x7F (WDGA=0) → **看门狗未启动**
  - PC=0x08003628 (bootloader idle) → **app 崩溃后回到 bootloader**
- Reset_Handler 反汇编确认：`bl entry` 后接 `bx lr`（如果 entry 返回会跳到 bootloader）
- 在 `bx lr`(0x0810cff0) 设断点超时 → entry() 不是简单返回

**当前结论：**
- App 在 C++ 构造函数或 board_init 到 main 之间的某处崩溃（自由运行）
- 崩溃不是芯片复位，而是异常 handler 将 CPU 带回 bootloader
- 断点模式通过 → 时序敏感问题，高度怀疑是 SPI DMA 中断在初始化期间触发（空 DMA handle）

### 2026-04-12 00:30 — 根因确认：bootloader 5秒等待期

**根因：ArduPilot bootloader 在正常上电时先等 5 秒接受固件上传，然后才 jump_to_app！**
- `main()` 中 `check_fast_reboot()` 返回 0（非快速重启）→ `stm32_was_watchdog_reset()` 返回 0（非看门狗复位）→ r5=0 → `cbz r5, skip_jump` → 跳过首次 jump_to_app
- 先执行 `bootloader(5000)` 等 5 秒，然后再 `jump_to_app()`
- 之前所有测试只等了 1~3 秒 halt，bootloader 还在等待，app 还没启动

**验证：**
- 等 3 秒 halt：0x200000F0=0x12345678（magic 不变，app 没跑）→ 确认 app 还没启动
- 等 12 秒 halt：0x200000F0=0x0813e974（被 app .data init 覆盖）→ app 已运行
- 等 60 秒 halt：rtt_dbg_main_loop_iterations=11279 → **主循环正常运行！**

**结论：App 完全正常运行，没有 HardFault、没有看门狗复位、没有回到 bootloader！**
之前所有的"回到 bootloader"现象都是因为测试等待时间不够（< 5 秒）。

**下一步：**
1. 更新 status.md 和 open-issues.md
2. 继续 RCInput SBUS 和 RCOutput PWM 硬件验证
3. 串口/MAVLink 通信验证

## 2026-04-12 13:45 Session: IOMCU 驱动链路审查 + 文档更新

### IOMCU UART8 全链路确认

**审查结论：驱动链路从 hwdef 到 AP_IOMCU 实例化完整无缺。**

| 层级 | 状态 | 细节 |
|------|------|------|
| hwdef.dat | ✅ | PE0/PE1 UART8 AF8, IOMCU_UART UART8, HAL_WITH_IO_MCU 1, PG5 VDD_5V_RC_EN |
| rtt_hwdef.py | ✅ | 重构为一次性计算 total_serial，SERIAL_PORT_COUNT 只输出一次 |
| 生成 hwdef.h | ✅ | SERIAL_PORT_COUNT=3, HAL_UART_IOMCU_IDX=2, DEVICE_LIST="uart7","usb-acm0","uart8" |
| RTT BSP | ✅ | rtconfig.h BSP_USING_UART8, uart_config.h UART8_CONFIG |
| UARTDriver | ✅ | _device_names[] index 2 → "uart8" |
| HAL_RTT_Class.cpp | ✅ | ioUartDriver(HAL_UART_IOMCU_IDX=2) → AP_IOMCU iomcu(ioUartDriver) |
| AP_IOMCU.cpp | ✅ | 已完全适配 RTT API (rt_event_send/recv, rt_thread_self) |

**阻塞项**：`AP_IOMCU::init()` 需要 ROMFS 中的 `io_firmware.bin`，RTT 构建系统尚未实现 ROMFS。

### 文档更新
- open-issues.md：新增 IOMCU 驱动链路确认段落 + ROMFS 待办
- alignment-status.md：更新 IOMCU 状态为"UART8 驱动链路完整，阻塞于 ROMFS"
- agent-trace.md：追加本次会话记录

### 2026-04-12 13:45 IOMCU 实现完成编译链接通过
- 动作：完成 IOMCU 完整实现链路
  - hwdef: 添加 PE0/PE1(UART8 AF8), IOMCU_UART UART8, PG5(VDD_5V_RC_EN)
  - rtt_hwdef.py: 添加 IOMCU_UART 解析 → 生成 HAL_UART_IOMCU_IDX, HAL_WITH_IO_MCU
  - AP_IOMCU.h: thread_t 类型按 HAL board 条件定义
  - AP_IOMCU.cpp: 6 处 ChibiOS API 全部替换为 RTT
    - ch.h → rtthread.h
    - chEvtSignal → rt_event_send(&iomcu_event, ...)
    - chThdGetSelfX → rt_thread_self()
    - chEvtWaitAnyTimeout → rt_event_recv(&iomcu_event, ...)
    - 添加 struct rt_event iomcu_event 成员 + rt_event_init 初始化
    - 添加 EVENT_MASK(n) 宏定义
  - HAL_RTT_Class.cpp: 添加 AP_IOMCU iomcu(ioUartDriver) 实例
  - AP_BoardConfig.h: RTT 板添加 sbus_out_rate 字段
- 编译结果：ROM 1,402,824/1,540,096 (91.09%), RAM 177,664/524,288 (33.89%)
- 烧录验证：bootloader + app 烧录成功，MAVLink 正常（734消息/15秒）
- 下一步：需要连接 SBUS 接收机验证 RC 通道（RC_CHANNELS 消息检查）
