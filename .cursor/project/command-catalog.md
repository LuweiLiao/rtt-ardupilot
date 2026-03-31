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
- 成功判据：`libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/hwdef.h` 刷新
- 注意：修改 `hwdef.dat` 后必须手动执行此命令再编译

## OpenOCD 一步烧录（推荐）
- 用途：直接烧录 + 验证 + 复位，无需持久 OpenOCD 服务器
- 命令：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_deploy/cuav_v5/rtthread.bin verify 0x08008000 reset exit"`
- 成功判据：`Verified OK` + `Resetting Target`
- 常见失败：旧 OpenOCD 进程占端口（`pkill -f openocd`）；首次使用需 `sudo udevadm control --reload-rules && sudo udevadm trigger` 或直接 `sudo` 运行
- 环境：Ubuntu 24.04 物理机直接可用，无需 usbipd

## OpenOCD 启动调试服务器
- 用途：启动 GDB 调试服务器
- 命令：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg`
- 成功判据：端口 `:3333` 可被 GDB 连接
- 常见失败：`claim interface failed`、旧 openocd 占用

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
| ArduPilot CDC | 1209:5741 | /dev/ttyACM1 | usb-ArduPilot_CUAVv5_RTT_* | MAVLink 通信 |

## 当前推荐的验证顺序
1. **UART7 msh**：`picocom -b 115200 /dev/ttyACM0` — RT-Thread 底层状态
2. **MAVLink**：`mavproxy.py --master=/dev/ttyACM1` — 参数 / 传感器验证
3. **GDB**：`arm-none-eabi-gdb -batch -ex "target remote | openocd ..." ...` — 底层诊断
