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
- 常见失败：ST-Link 未连入 WSL2（需先 `usbipd attach`）、旧 OpenOCD 进程占端口

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
- Windows 端：用 PuTTY / MobaXterm / 终端打开 COM33，波特率 **115200**
- WSL2 注意：CH343（VID:PID 1A86:55D3）在 WSL2 无驱动，必须从 Windows 端读
- PowerShell 快速读取：
  ```powershell
  $port = New-Object System.IO.Ports.SerialPort('COM33', 115200, 'None', 8, 'One')
  $port.Open(); Start-Sleep -ms 500
  $port.Write("`r`n"); Start-Sleep -ms 1000
  Write-Output $port.ReadExisting()
  $port.Close()
  ```
- 常用 msh 命令：
  - `list thread` — 查看线程列表
  - `list device` — 查看设备列表
  - `free` — 查看内存使用
  - `ls /sd` — 查看 SD 卡挂载
  - `version` — RT-Thread 版本

## MAVProxy 连接测试
- 用途：在 WSL2 中验证 MAVLink 通信
- 前提：先 `usbipd attach --wsl --busid <busid>` 连入 ArduPilot USB
- 命令：`mavproxy.py --master=/dev/ttyACM0 --baudrate=115200 --non-interactive`
- 成功判据：`Received 943 parameters (ftp)`
- 注意：WSL2 usbipd 下偶尔断链，Windows 侧验证更可信

## usbipd 设备管理
- 列出设备：`powershell.exe -Command "usbipd list"`
- 绑定并连入 WSL2：`powershell.exe -Command "usbipd bind --busid <id> --force; usbipd attach --wsl --busid <id>"`
- 断开：`powershell.exe -Command "usbipd detach --busid <id>"`

## 当前已知 USB 设备 busid
| busid | 设备 | 用途 |
|-------|------|------|
| 3-1 | STM32 STLink (0483:3748) | 烧录/调试 |
| 3-2 | CH343 USB-TTL (1A86:55D3) COM33 | UART7 调试串口 |
| 3-4 | ArduPilot (1209:5741) COM32 | USB CDC MAVLink |

## 当前推荐的验证顺序
1. **UART7 msh**：COM33 / 115200 — RT-Thread 底层状态
2. **Windows 侧**：MissionPlanner 连接 COM32 — MAVLink 功能验证
3. **WSL2**：MAVProxy `/dev/ttyACM0` — 辅助验证
4. **GDB**：OpenOCD + GDB 读变量 — 底层诊断
