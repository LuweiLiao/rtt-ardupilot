---
name: rtt-build-flash-debug
description: >-
  提供 pogo-apm 上 RT-Thread 版 ArduPilot（CUAV V5 / STM32F767）的 scons 全量编译、OpenOCD+GDB
  烧录与调试命令，以及 WSL2 usbipd、USB 串口与 MAVLink 参数测试要点。在用户询问 RTT CUAV V5
  编译、烧录、flash、OpenOCD、GDB、ST-Link、CMSIS-DAP、0x08008000、rtt_deploy 或 WSL2 调试时使用。
---

# RT-Thread ArduPilot · CUAV V5 (STM32F767) 构建 / 烧录 / 调试

适用：**pogo-apm** 仓库根目录；板型 **cuav_v5**（Waf 板名 `rtt_cuav_v5`）。硬件定义见 `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat`（前 32KB 为 ArduPilot bootloader，应用区 **`0x08008000`**，16MHz HSE）。

## 工作流概览

1. **SCons** 一条命令完成 BSP 部署 + packages 下载 + MAVLink 生成 + 编译链接
2. **OpenOCD** 占用调试器 → **GDB** 烧录 / 调试  
3. Windows / WSL2 下用 **usbipd** 把 ST-Link 或 DAP 绑进 WSL；GCS 多在 Windows COM 上连 USB CDC

下文以 **`$AP_ROOT`** 表示仓库根目录（例如 `/home/pix/firmare/pogo/pogo-apm`）。

---

## 1. 编译

### 1.0 新电脑首次（干净 clone）

前提：`arm-none-eabi-gcc` 在 PATH 中，`python3` + `pip install scons`，`git`。

```bash
git clone --recursive -b staging/pogo-rtt git@gitee-pogo:pogouav/ardupilot.git pogo-apm
cd pogo-apm
python3 -m SCons --target=cuav-v5 -j16
```

自动完成：BSP 部署（copytree）→ STM32F7 HAL/CMSIS packages 下载 → MAVLink 头文件生成 → ap_config.h 创建 → 编译链接。首次 clone 约 4 分钟，首次编译约 1.5 分钟。

### 1.1 环境

- **`RTT_ROOT`**：未设置时，根目录 `SConstruct` 会把 `modules/rt-thread` 写入环境并调用 scons；若自定义 RT-Thread 路径，可先 `export RTT_ROOT=...`。
- **`RTT_EXEC_PATH`**：若 scons 找不到 `arm-none-eabi-gcc`，把该变量设为工具链 **bin** 所在目录（SConstruct 会尝试从 PATH 自动推断）。
- 并行：文档与历史实践推荐 **`-j16`**（或按 CPU 核数调整）。

### 1.2 Waf（仅在需要重新生成 hwdef.h 时）

```bash
cd "$AP_ROOT"
./waf configure --board rtt_cuav_v5
```

注意：SCons 编译路径已能自动创建 `ap_config.h` 并复制 `hwdef.h`，日常编译不再需要先跑 Waf。

### 1.3 完整固件（SCons，推荐日常一条命令）

```bash
cd "$AP_ROOT"
python3 -m SCons --target=cuav-v5 -j16
```

`--target` 别名：`cuav-v5`、`cuav_v5`、`rtt_cuav_v5` 等，见根目录 `SConstruct`。

### 1.4 产物路径

| 文件 | 路径 |
|------|------|
| **主 ELF** | `$AP_ROOT/build/rtt_deploy/cuav_v5/rt-thread.elf` |
| **主 BIN** | `$AP_ROOT/build/rtt_deploy/cuav_v5/rtthread.bin` |
| BIN 副本（便于上传脚本） | `$AP_ROOT/build/rtt_cuav_v5/rtthread.bin`（由 SConstruct 从 deploy 目录复制） |

### 1.5 清理 BSP 构建

```bash
cd "$AP_ROOT"
python3 -m SCons -c --target=cuav_v5
```

---

## 2. 刷写（OpenOCD + GDB）

### 2.1 调试器

- **ST-Link V2**：常见 USB ID **`0483:3748`**
- **CMSIS-DAP**：使用对应 OpenOCD interface 配置

### 2.2 WSL2：usbipd 绑定 USB（在 Windows PowerShell / CMD）

先列出设备：

```powershell
usbipd list
```

将调试器绑到 WSL（把 `<BUSID>` 换成列表中的总线 ID）：

```powershell
powershell.exe -Command "usbipd bind --busid <BUSID> --force"
powershell.exe -Command "usbipd attach --wsl --busid <BUSID>"
```

### 2.3 启动 OpenOCD

**ST-Link：**

```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg &
```

**CMSIS-DAP：**

```bash
openocd -f interface/cmsis-dap.cfg -f target/stm32f7x.cfg &
```

### 2.4 用 GDB `monitor flash` 烧录应用（偏移 **0x08008000**）

Bootloader 占 **32KB**，应用 **不得** 写到 `0x08000000`（除非整镜像已含 bootloader 且布局一致）。

```bash
arm-none-eabi-gdb -batch \
  -ex "target remote :3333" \
  -ex "monitor halt" \
  -ex "monitor reset halt" \
  -ex "monitor flash write_image erase $AP_ROOT/build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000" \
  -ex "monitor reset run" \
  $AP_ROOT/build/rtt_deploy/cuav_v5/rt-thread.elf
```

### 2.5 OpenOCD 不稳定时

- 先结束旧进程再重试：`pkill -f openocd`
- 若报 **claim interface failed**：关闭 STM32CubeProgrammer、其它 OpenOCD/GDB/IDE 调试会话，拔插调试器；必要时在 Linux 侧检查 **udev** 权限或对脚本使用 `sudo`（仅当环境需要）。

---

## 3. 调试（GDB）

### 3.1 交互式连接

```bash
arm-none-eabi-gdb $AP_ROOT/build/rtt_deploy/cuav_v5/rt-thread.elf
```

在 GDB 内：

```text
(gdb) target remote :3333
(gdb) monitor halt
(gdb) continue
```

常用验证符号：`_main_loop_entry`、`rt_hw_board_init`、`Error_Handler` / `_Error_Handler`（时钟初始化失败时会进入）；调试变量见下节。

### 3.2 批量读取主循环计数（运行中采样）

```bash
arm-none-eabi-gdb -batch \
  -ex "target remote :3333" \
  -ex "monitor halt" \
  -ex "p rtt_dbg_main_loop_iterations" \
  -ex "monitor resume" \
  $AP_ROOT/build/rtt_deploy/cuav_v5/rt-thread.elf
```

相关符号在 `HAL_RTT_Class.cpp`：`rtt_dbg_main_loop_iterations`、`rtt_dbg_main_loop_entry_called`。

### 3.3 线程优先级（示例，依赖 GDB 与 RT-Thread 结构体布局）

若字段报错，以当前 RT-Thread 版本头文件中的 `struct rt_thread` 为准。

```text
set $mt = (struct rt_thread *)schedulerInstance._main_thread_id
p $mt->sched_thread_ctx.sched_thread_priv.current_priority
p $mt->sched_thread_ctx.sched_thread_priv.init_priority
```

### 3.4 USB CDC 相关计数（CherryUSB 路径）

```text
p dbg_serial_bulkin_cnt
p dbg_serial_kicktx_cnt
```

---

## 4. USB 与串口（WSL2 / Windows）

### 4.1 列出 Windows 上 ArduPilot USB 串口

```powershell
powershell.exe -Command "Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match 'ArduPilot' } | Select-Object DeviceID, Description"
```

### 4.2 列出 usbipd 设备

```powershell
powershell.exe -Command "usbipd list"
```

说明：USB CDC 飞控常在 **Windows COM**；代码与 GCS 在 WSL2 时可用仓库内 **`scripts/README_MAVLINK_WSL2.md`** 的 TCP 桥接方案（不依赖 WSL 内 `/dev/ttyACM*`）。

---

## 5. 参数下载 / MAVLink 冒烟测试

- 使用 **MAVProxy** 或 **pymavlink** 连接对应串口（或 WSL2 桥接后的 TCP），观察参数列表拉取速度与是否完整。
- 若参数极慢：与 `UARTDriver` 波特/带宽限制、`GCS_Param` 排队、`GPIO::usb_connected()` 等与 USB 相关的逻辑有关；详见 `docs/AP_HAL_RTT_STATUS.md` 与 `hwdef.dat` 中 **SERIAL_ORDER**（CUAV V5 为 **OTG1** 优先）。

---

## 6. 重要注意事项（速查）

| 项 | 说明 |
|----|------|
| 应用烧录地址 | **`0x08008000`**（32KB bootloader 之后） |
| 内核 tick | **`RT_TICK_PER_SECOND 10000`**（`libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/rtconfig.h`），约 **100µs** 粒度，利于 400Hz 主循环等 |
| OpenOCD | 易因占用或版本组合不稳定；失败时 **`pkill -f openocd`** 后重试 |
| 产物 | 以 **`build/rtt_deploy/cuav_v5/rt-thread.elf`** 与 **`rtthread.bin`** 为准；`build/rtt_cuav_v5/rtthread.bin` 为便于上传的副本 |
| ST-Link | 出现 **claim interface** 时优先释放其它占用进程与拔插设备 |

---

## 7. UART7 调试串口（msh 控制台）

### 7.1 硬件连接

CUAV V5 上 UART7 引脚（与 ChibiOS fmuv5 hwdef 一致）：

| 信号 | MCU Pin | AF | 说明 |
|------|---------|-----|------|
| TX | PE8 | AF8 | 连接 USB-TTL 的 RX |
| RX | PF6 | AF8 | 连接 USB-TTL 的 TX |

波特率：**115200**

### 7.2 Windows 端连接

用 PuTTY、MobaXterm 或任何终端工具打开对应 COM 口（当前为 **COM33**，CH343 USB-TTL），配置 115200/8N1。

PowerShell 快速读取：

```powershell
$port = New-Object System.IO.Ports.SerialPort('COM33', 115200, 'None', 8, 'One')
$port.Open(); Start-Sleep -ms 500
$port.Write("`r`n"); Start-Sleep -ms 1000
Write-Output $port.ReadExisting()
$port.Close()
```

### 7.3 WSL2 注意

CH343（VID:PID `1A86:55D3`）在 WSL2 内核无驱动（ch341.ko 只匹配 `1A86:7523`），attach 后不会出现 `/dev/ttyUSB*`。必须从 **Windows 端**读写串口。如需 WSL2 内使用，需自编内核或 socat 桥接。

### 7.4 常用 msh 命令

| 命令 | 说明 |
|------|------|
| `list thread` | 查看线程列表（优先级、栈使用率） |
| `list device` | 查看设备列表（引用计数） |
| `free` | 查看内存使用 |
| `ls /sd` | 查看 SD 卡挂载 |
| `version` | RT-Thread 版本信息 |

### 7.5 配置文件

| 文件 | 关键配置 |
|------|----------|
| `rtconfig.h` | `RT_CONSOLE_DEVICE_NAME "uart7"`, `BSP_USING_UART7` |
| `.config` | `CONFIG_RT_CONSOLE_DEVICE_NAME="uart7"`, `CONFIG_BSP_USING_UART7=y` |
| `stm32f7xx_hal_msp.c` | `HAL_UART_MspInit` 中 UART7 GPIO 初始化 |
| `config/f7/uart_config.h` | `UART7_CONFIG` 宏定义 |

---

## 8. 可选：串口上传（需 bootloader / 工具链支持）

若构建时带 **`--upload`**，根 `SConstruct` 会将 `rtthread.bin` 转为 **apj** 并调用上传脚本（需正确串口与板型）：

```bash
cd "$AP_ROOT"
python3 -m SCons --v=ArduCopter --target=cuav_v5 -j16 --upload --port=/dev/ttyACM0
```

具体是否适用于当前 bootloader 与镜像布局，以仓库内 `Tools/scripts` 与板子现状为准。
