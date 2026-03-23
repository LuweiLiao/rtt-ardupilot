---
name: ArduPilot RTT 移植子任务拆解
overview: 将「ArduPilot 移植到 RT-Thread」拆成若干可交给 subagent 的子任务：先保证能运行到 RTT 的 main（ap_main 线程的 setup/loop），再保证 USB MAVLink 正常工作。当前已知阻塞点为 board.c 中 SystemClock_Config 在 USB EP0 路径上触发 Error_Handler。
todos: []
isProject: false
---

# ArduPilot 移植到 RTT：子任务拆解计划

## 当前状态与阻塞点

- **启动链**：Bootloader(0x08000000) → 跳转 0x08008000 → `Reset_Handler` → `entry`(RT-Thread) → `hal.run()` → `rt_hw_board_init()` → 延时 → `rt_components_init()`(含 CherryUSB) → 调度器/主线程 → `_main_loop_entry`(setup + loop)。
- **实际卡死位置**：OpenOCD 调试显示停在 `_Error_Handler`，调用栈为 `__usbd_event_ep0_setup_complete_handler` → `_Error_Handler("board/board.c", 43)`。即 **USB EP0 Setup 完成回调** 执行路径上，有代码触发了 [board.c:43](libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/board/board.c) 的 `Error_Handler()`（对应 `HAL_RCC_OscConfig` 失败）。
- **时钟配置**：CUAV v5 hwdef 为 8MHz 晶振；[board.c](libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/board/board.c) 使用 `RCC_HSE_BYPASS`（外部时钟源），若板子实际接的是无源晶振，应改为 `RCC_HSE_ON`，否则 HSE 可能不起振导致 OscConfig 失败。

## 目标 1：能运行到 RTT 的 main

**含义**：从复位到 ArduPilot 主线程入口 `_main_loop_entry` 的 `setup()` 被调用且不崩溃，即完成：entry → rt_hw_board_init → rt_components_init（含 USB 初始化）→ 创建并启动 ap_main 线程 → 进入 setup()。

**建议子任务（可交给 subagent）：**

1. **Subagent A - 修时钟与 Error_Handler 路径**
  - 查清 CUAV v5 原理图/文档：HSE 是外部有源时钟(BYPASS) 还是无源晶振(ON)；必要时将 [board.c](libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/board/board.c) 中 `RCC_HSE_BYPASS` 改为 `RCC_HSE_ON`，并确认 PLL 参数与 8MHz 一致。
  - 确认 `SystemClock_Config` 是否在 USB 初始化或 EP0 回调路径上被二次调用；若会，则改为幂等（仅首次配置）或从 USB 路径移除对 RCC 的重复配置，避免在 EP0 回调里再次调用导致 OscConfig 失败并触发 Error_Handler。
  - 若仍失败，在 `_Error_Handler` 或 `Error_Handler` 调用处加简单日志（如通过现有 LOG_E）便于后续用 OpenOCD 定位。
2. **Subagent B - 用 OpenOCD 验证跑到 main**
  - 在 [rtt_debug_cuav_v5.gdb](scripts/rtt_debug_cuav_v5.gdb) 中设断点：`rt_hw_board_init`、`rt_components_init` 或 CherryUSB 初始化入口、`_main_loop_entry`、`setup`（或 Copter 的 setup 符号）。
  - 运行 OpenOCD + GDB，确认：先进入 `rt_hw_board_init`，再进入 `rt_components_init`，最后命中断点在 `_main_loop_entry` 或 `setup`；若在某步之前就进 `_Error_Handler`，把最新调用栈和断点位置反馈给 Subagent A。
  - 文档化「能稳定跑到 setup() 的烧录与调试步骤」（含 bootloader + RTT 固件地址、OpenOCD cfg、GDB 命令）。

## 目标 2：USB MAVLink 正常工作

**含义**：USB CDC 枚举为 `usb-acm0`，ArduPilot 将 serial0 映射到该设备，GCS 通过该串口与飞控进行 MAVLink 通信（心跳、参数等）。

**建议子任务（可交给 subagent）：**

1. **Subagent C - USB CDC 枚举与 serial0**
  - 在「能跑到 main」的基础上，确认 [HAL_RTT_UART_DEVICE_LIST](libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py) 中 serial0 为 `usb-acm0`（cuav_v5 hwdef 已为 OTG1 优先，生成应为 `"usb-acm0", ...`）。
  - 确认 CherryUSB CDC 在 `rt_components_init` 中正确初始化且注册为 RTT 设备名 `usb-acm0`；必要时用 OpenOCD 在 USB 枚举前后设断点，或通过主机端 `ls /dev/ttyACM`* / 日志确认设备出现。
  - 在应用层做一次简单测试：主线程或测试线程通过 serial0 写固定字符串，用主机串口工具接收，确认 USB 收发路径畅通。
2. **Subagent D - MAVLink over USB**
  - 确认 ArduPilot 的 GCS 串口选择逻辑中，serial0 作为 MAVLink 端口（通常为默认）。
  - 用 QGC 或 MAVProxy 连接 USB 串口，验证：心跳(HEARTBEAT)、参数列表/读写、简单命令；若有丢包或校验错误，再排查缓冲区与 D-Cache 一致性（[usb_config.h](libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/board/ports/cherryusb/usb_config.h) 中已开 CONFIG_USB_DCACHE_ENABLE）。

## 依赖关系与顺序

```mermaid
flowchart LR
  A[Subagent_A_Clock_ErrorHandler] --> B[Subagent_B_OpenOCD_Verify_Main]
  B --> C[Subagent_C_USB_CDC_serial0]
  C --> D[Subagent_D_MAVLink_over_USB]
```



- A 与 B 可部分并行：A 改代码，B 用现有/更新固件做断点验证并反馈；B 依赖 A 修掉 EP0 路径上的 Error_Handler 才能稳定进入 main。
- C 依赖「能跑到 main」；D 依赖 C 的 USB 串口可用。

## 交付物与验收

- **目标 1 验收**：上电/复位后，用 OpenOCD 能在 `_main_loop_entry` 或 `setup` 处命中断点，且不再因 board.c:43 的 Error_Handler 卡死。
- **目标 2 验收**：主机识别 USB CDC 设备；GCS 通过该端口收到心跳并能进行参数/命令交互。

## 建议的 subagent 调用方式

- 每个 subagent 分配上述一个子任务（A/B/C/D），在任务描述中写明：当前仓库状态、已知的 board.c:43 与 USB EP0 调用栈、以及本子任务要达成的具体结果和交付物。
- A、B 优先；A 完成后再或并行启动 B；B 通过后再做 C、D。

