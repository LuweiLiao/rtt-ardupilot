# RTT 控制闭环架构

按自动控制原理组织 CUAV V5 RTT 固件 bring-up 的自我闭环调试系统。

## 信号流

```text
Setpoint (期望)
  ├─ main 循环 iter ≥ 50，loop_entry = 0x12345678
  ├─ usb_init = 2，configured = 1
  └─ 主机 lsusb 出现 1209:5741 (USB CDC)

        ↓ 误差 e(t)

Plant (被控对象)
  └─ STM32F767 上的 RT-Thread ArduPilot 固件

        ↑ 反馈 y(t)

Sensors (测量)
  ├─ UART7 / CH340 → RTT_CTL 行（1 Hz，主循环内 + ap_ctl）
  └─ OpenOCD/GDB  → rtt_dbg_*、PC、CFSR、configured

        ↓

Controllers ×10 (并行诊断/控制律)
  C0 Supervisor … C8 HostCDC，C9 Actuator

        ↓

Actuator (执行)
  └─ OpenOCD：scons 编译 + program @ 0x08008000（结束释放 ST-Link）
```

## 端口说明

| 设备 | 用途 | 典型 VID:PID |
|------|------|----------------|
| PA11/12 OTG | SERIAL0 CDC（setpoint） | 1209:5741 |
| UART7 + CH340 | RT 控制台 / RTT_CTL | 1a86:55d3 → `/dev/ttyACM0` |
| ST-Link | 烧录 + OpenOCD 传感器 | 0483:3748 |

**注意**：`/dev/ttyACM0` 在多数环境下是 CH340，不是飞控 CDC。

## 固件侧

- `libraries/AP_HAL_RTT/rtt_ctl_telemetry.c`：UART7 **硬件直写**（`usart_ll` poll TX，不依赖 rt_kprintf/DMA）
- 上电即输出：`[BOARD-INIT] uart7 hw lane up`（`rt_board_init` 内）
- 每秒一行 `RTT_CTL ...`；MSH：`ap_ctl`；主循环：`rtt_ctl_telemetry_tick(1000)`
- `UARTDriver` 对 console `uart7` **跳过 CMSIS 寄存器改写**，避免破坏调试口

## 主机侧 Agent 目录

```text
Tools/scripts/rtt_control_loop/
  agents/
    sensor_uart7.py      # 反馈传感器
    sensor_openocd.py
    sensor_host_cdc.py
    controllers.py       # C0–C9 并行（ThreadPoolExecutor×10）
    actuator_openocd.py  # 执行器（烧录）
    orchestrator.py      # 闭环编排
  rtt_control_loop.py    # CLI 入口
  sensors/               # 底层采样实现
  actuators/             # flash.sh / flash_full.sh
```

```bash
# 单次闭环（默认复用 OpenOCD 会话；UART+OpenOCD+git 三线程并行）
python3 Tools/scripts/rtt_control_loop/rtt_control_loop.py --once --run-seconds 22

# 多轮闭环（OpenOCD 只启动一次，每轮省 ~2–3s）
python3 Tools/scripts/rtt_control_loop/rtt_control_loop.py --cycles 3 --run-seconds 22

# 带执行器
python3 Tools/scripts/rtt_control_loop/rtt_control_loop.py --cycles 3 --auto-actuator

# 每轮重启 OpenOCD（旧行为，较慢）
python3 Tools/scripts/rtt_control_loop/rtt_control_loop.py --once --no-session
```

## 10 个 Controller Agent

| ID | 名称 | 关注点 |
|----|------|--------|
| C0 | Supervisor | 总误差、最大分项 |
| C1 | Boot/Reset | PC 在 app / bootloader |
| C2 | Startup | hal_run、setup_stage |
| C3 | HardFault | hf_pc、CFSR、hardfault_hang |
| C4 | IWDG | 早期 init / 看门狗窗口 |
| C5 | USB/Init | usb_init、USBRST |
| C6 | EP0/Enum | setup_stup、configured |
| C7 | MainLoop | iter、loop_hz、overrun |
| C8 | HostCDC | lsusb 1209:5741 |
| C9 | Actuator | 编译+烧录建议/执行 |

当前 C1–C8 以**诊断与行动建议**为主；自动改代码需后续把 `FIX_EP0` 等 action 接到补丁 agent。C9 在 `--auto-actuator` 下可闭环烧录。

## 成功判据

`rtt_control_loop.py` 退出码 0：`error.total == 0`（setpoint 全满足）。
