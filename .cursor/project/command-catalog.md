# Command Catalog

## 干净环境编译（新电脑首次）
- 前提：安装 `arm-none-eabi-gcc`（PATH 中）、`python3` + `pip install scons`、`git`
- 命令：
  ```bash
  git clone --recursive -b staging/pogo-rtt git@gitee-pogo:pogouav/ardupilot.git pogo-apm
  cd pogo-apm
  python3 -m SCons --target=cuav-v5 -j16
  ```
- 成功判据：生成 `build/rtt_deploy/cuav_v5/rt-thread.elf` 与 `rtthread.bin`
- 说明：自动从 GitHub 下载 STM32F7 HAL/CMSIS packages，自动生成 ap_config.h，无需手动 waf configure
- 注意：首次 clone 约 4 分钟（含所有子模块），首次编译约 1.5 分钟

## 完整编译 RTT 固件
- 用途：构建 `CUAV v5` 的 RT-Thread ArduPilot 固件
- 命令：`python3 -m SCons --target=cuav-v5 -j16`
- 说明：自动完成 BSP 部署、packages 下载、MAVLink 生成、编译链接
- 成功判据：生成 `build/rtt_deploy/cuav_v5/rt-thread.elf` 与 `rtthread.bin`，ROM < 2016KB
- 常见失败：RT-Thread packages 缺失、工具链路径、include 冲突

## 生成 hwdef.h
- 用途：从 `hwdef.dat` 重新生成 `hwdef.h`
- 命令：`python3 libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py`
- 成功判据：`libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.h` 刷新（部署后亦见于 `build/rtt_deploy/cuav_v5/hwdef.h`）
- 注意：修改 `hwdef.dat` 后必须手动执行此命令再编译

## OpenOCD 一步烧录（推荐）
- 用途：直接烧录 + 验证 + 复位，**命令结束即退出**，不占用 ST-Link
- 命令：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify reset" -c "resume" -c "exit"`
- 成功判据：`Verified OK` + `Resetting Target`；**进程必须退出**（`pgrep openocd` 为空）
- **强制规范**：调试会话结束必须关 OpenOCD（`Ctrl+C` 或 `pkill -f openocd`）；禁止留 `openocd ... -c init -c reset run` 后台占 ST-Link，否则后续烧录/GDB 会 `init failed`
- 烧录前检查：`pgrep -a openocd || echo OK`
- 常见失败：旧 OpenOCD 进程占 ST-Link（`pkill -f openocd`）；首次使用需 udev 规则或 `sudo`

## OpenOCD 启动调试服务器
- 用途：长时间 GDB 调试；**用完必须关闭**
- 命令：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg`
- 成功判据：端口 `:3333` 可被 GDB 连接
- **结束**：调试完成后 `pkill -f openocd` 或终端 `Ctrl+C`；提交 trace/切换任务前执行 `pgrep openocd` 确认无残留
- 常见失败：`claim interface failed`、旧 openocd 占用（根因同上）

## GDB 单次快速检查（推荐）
- 用途：不启动持久 OpenOCD 服务器，一次性连接检查后断开
- 命令：`arm-none-eabi-gdb -batch -ex "target remote | openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c 'gdb_port pipe'" -ex "monitor halt" -ex "p rtt_dbg_main_loop_iterations" -ex "monitor resume" build/rtt_deploy/cuav_v5/rt-thread.elf`
- 成功判据：读到变量值，板子继续运行
- 注意：多次 halt/resume 可能触发 IBUSERR HardFault（DMA 状态不一致），建议单次 halt-check-resume

## UART7 串口调试（msh 控制台）
- 用途：通过 UART7 调试串口连接 RT-Thread msh shell
- 硬件连接：飞控 PE8(TX) → USB-TTL(RX)，飞控 PF6(RX) → USB-TTL(TX)
- **Ubuntu 物理机**：CH343 映射为 `/dev/ttyACM0`，用 `picocom -b 115200 /dev/ttyACM0` 连接
- 区分设备：`ls /dev/serial/by-id/` → `usb-1a86_USB_Single_Serial_*` 为 CH343
- Windows 端：用 PuTTY / MobaXterm / 终端打开 COM33，波特率 **115200**
- WSL2 注意：CH343（VID:PID 1A86:55D3）在 WSL2 无驱动，必须从 Windows 端读
- 常用 msh 命令：
  - `list thread` — 查看线程列表
  - `list device` — 查看设备列表
  - `free` — 查看内存使用
  - `ls /sd` — 查看 SD 卡挂载
  - `version` — RT-Thread 版本
  - `ap_rate` — 查看 ArduPilot CPU/主循环统计（`cpu_idle/load/loop_us/loop_hz/work_us/overrun/iterations`）

## 低频定位（UART7 + GDB + MAVLink）
- 用途：区分“CPU 真忙 / 主循环真低 / 默认消息流率太低 / 串口链路问题”
- 第一步（UART7）：`ap_rate` + `list thread` + `free`
- 第二步（GDB）：读取 `rtt_cpu_idle_pct`、`rtt_dbg_loop_time_us`、`rtt_dbg_work_time_us`、`rtt_dbg_overrun_count`
- 第三步（MAVLink）：统计 `HEARTBEAT/ATTITUDE/RAW_IMU/SYS_STATUS` 实际频率；若怀疑消息率配置，再发 `MAV_CMD_SET_MESSAGE_INTERVAL`
- 当前 RTT/Copter 经验值：
  - `cpu_idle` 正常约 98-99%
  - `loop_us` 正常量级约 2.1-2.8ms（约 350-450Hz）
  - 若默认 `ATTITUDE/RAW_IMU/SYS_STATUS` 频率远低于主循环，而 `SET_MESSAGE_INTERVAL` 后能立刻升高，则优先怀疑 `streamRates[]`/GCS 请求链，而不是 USB CDC 吞吐

## MAVProxy / pymavlink 连接测试
- 用途：验证 MAVLink 通信
- **Ubuntu 物理机**（推荐）：
  - ArduPilot USB CDC 映射为 `/dev/ttyACM1`（可通过 `ls /dev/serial/by-id/` 中 `usb-ArduPilot_*` 确认）
  - 命令：`mavproxy.py --master=/dev/ttyACM1 --baudrate=115200 --non-interactive`
  - 成功判据：收到心跳 + 参数下载（约 941 个）
- **Windows 端**：MissionPlanner 连接对应 COM 口
- **WSL2**：需先 `usbipd attach --wsl --busid <busid>`，偶尔不稳定

## USB 设备（Ubuntu 物理机）
| 设备 | VID:PID | /dev 路径 | serial/by-id | 用途 |
|------|---------|-----------|-------------|------|
| ST-LINK V2 | 0483:3748 | — | — | OpenOCD 烧录/调试 |
| CH343 USB-TTL | 1A86:55D3 | /dev/ttyACM0 | usb-1a86_USB_Single_Serial_* | UART7 msh 控制台 |
| ArduPilot CDC | 1209:5741 | /dev/ttyACM1 | `usb-ArduPilot_CUAVv5_RTT_*` 或 Cherry 下 `usb-APM_CUAV_V5_CDC_1_*` | MAVLink 通信 |

## 分层模块测试构建（bring-up / USB gate）

- 用途：全量验证前，按 `libraries/AP_HAL_RTT/test/README.md` 与 `Tools/scripts/rtt_test_manifest.py` 做**可构建**门禁（不替代实机分层跑测）
- 命令（**串行**执行，勿并行两个 `--test=`）：
  ```bash
  cd /home/llw/firmare/pogo-apm
  python3 -m SCons --target=cuav_v5 --test=L0_system -j$(nproc)
  python3 -m SCons --target=cuav_v5 --test=L4_spi -j$(nproc)
  python3 -m SCons --target=cuav_v5 --test=L7_cherryusb_cdc -j$(nproc)
  ```
- 成功判据：`scons: done building targets`；产物 `build/rtt_deploy/cuav_v5/rtthread.bin` 与 `rt-thread.elf`
- 常见失败：并行 `--test=` 导致 `build/kernel/...` 对象缺失 → 对失败项**单独重跑**该 test

## USB 后端选择与 L0 验证（2026-05-28）

- 用途：在 **native / cherryusb / tinyusb** 间切换 SERIAL0 栈（互斥编译，单一 `OTG_FS_IRQHandler`）
- 环境变量：`RTT_USB_BACKEND` = `native`（默认，省略即可）| `cherryusb` | `tinyusb`
- 编译示例（主仓 Cherry L0 基线）：
  ```bash
  RTT_USB_BACKEND=cherryusb python3 -m SCons --target=cuav-v5 -j8
  # 全量机型：RTT_USB_BACKEND=cherryusb python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)
  # 默认 native：不设 RTT_USB_BACKEND
  ```
- 成功判据（构建）：生成 `build/rtt_cuav_v5/rtthread.bin`（或 `build/rtt_deploy/cuav_v5/rt-thread.elf`）；`arm-none-eabi-nm build/rtt_deploy/cuav_v5/rt-thread.elf | rg OTG_FS_IRQHandler` 仅 **一行**
- **主仓 Cherry L0 一键 gate**（脚本在 `/tmp`，非仓库内；需 ST-Link + CDC）：
  ```bash
  POGO_APM_ROOT=/home/llw/firmare/pogo-apm \
  RTT_USB_BACKEND=cherryusb \
  /tmp/cherryusb_main_l0_gate.sh --skip-build --skip-bl --json --wait 30
  ```
  - 成功判据：**exit 0**；JSON 内 STANDBY(3)、参数达标、30s 流 types≥预期、post-L0 VTOR=`0x08008000`、CFSR/HFSR=0、IWDGRSTF=0
  - 已构建时加 `--skip-build`；仅重烧 app 时可配合 OpenOCD `0x08008000`
- L0 烧录（手工）：BL `0x08000000` + app `0x08008000`，上电等待 **≥12s**（bootloader 窗口）
- L0 MAVLink（**仅 L0，非全量回归**）：
  - `lsusb -d 1209:5741` 可见 CUAV V5 CDC
  - 设备 by-id（**两种枚举名均可能**，gate 已兼容）：
    - `usb-APM_CUAV_V5_CDC_1_*`（CherryUSB 主仓 L0 实测）
    - `usb-ArduPilot_CUAVv5_RTT_*`（历史 native 命名）
  - 常为 **`/dev/ttyACM1`**（勿与 ST-Link/CH343 的 ACM0 混淆）
  - ```bash
    python3 -c "from pymavlink import mavutil; m=mavutil.mavlink_connection('/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00', baud=115200); m.wait_heartbeat(timeout=15); print(m.target_system)"
    ```
  - 主仓 Cherry L0 实测：STANDBY(3)、FORMAT_VERSION=120.0、**904/904** 参数、30s 约 1798 msg / 22 types
  - Post-L0：OpenOCD halt → VTOR/CFSR/HFSR/IWDGRSTF（gate 经 SCB 读 fault）
- 常见失败：gate 要求旧 `usb-ArduPilot_*` 而固件枚举为 `usb-APM_*`（已修 gate）；resolve 把日志打进 stdout 污染设备路径（已改 stderr）；`ACM0` 非 ArduPilot CDC；并行 scons 无 `rtt_ar_archive` 时 ARG_MAX
- 清理清单：`.cursor/usb-cleanup-inventory.md`

## 当前推荐的验证顺序
1. **UART7 msh**：`picocom -b 115200 /dev/ttyACM0` — RT-Thread 底层状态
2. **MAVLink**：`mavproxy.py --master=/dev/ttyACM1` — 参数 / 传感器验证（**确认 backend 与预期一致**）
3. **GDB**：`arm-none-eabi-gdb -batch -ex "target remote | openocd ..." ...` — 底层诊断
