# Command Catalog

## 完整编译 RTT 固件
- 用途：构建 `CUAV v5` 的 RT-Thread ArduPilot 固件
- 命令：`python3 -m SCons --v=ArduCopter --target=cuav_v5 -j16`
- 成功判据：生成 `build/rtt_deploy/cuav_v5/rt-thread.elf` 与 `rtthread.bin`
- 常见失败：RT-Thread packages、工具链路径、include 冲突、`ap_config.h` 生成链路

## 生成 hwdef 和源列表
- 用途：更新 `hwdef.h` 与 ArduPilot 源文件列表
- 命令：`./waf configure --board rtt_cuav_v5 && ./waf copter`
- 成功判据：相关生成文件刷新，无明显 `hwdef` 或源表错误
- 常见失败：板型名错误、waf 生成链与 SCons 预期不一致

## OpenOCD 连接 STM32F7
- 用途：启动调试服务器
- 命令：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg`
- 成功判据：端口 `:3333` 可被 GDB 连接
- 常见失败：`claim interface failed`、旧 openocd 占用、调试器未绑进 WSL

## GDB 烧录 RTT 应用
- 用途：将 `rtthread.bin` 写入 `0x08008000`
- 命令：`arm-none-eabi-gdb -batch -ex "target remote :3333" -ex "monitor halt" -ex "monitor reset halt" -ex "monitor flash write_image erase build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000" -ex "monitor reset run" build/rtt_deploy/cuav_v5/rt-thread.elf`
- 成功判据：应用区烧录完成，目标能重新运行
- 常见失败：应用地址写错、OpenOCD 未就绪、目标处于异常状态

## GDB 连接运行中的 RTT 固件
- 用途：查看停点、变量、计数器
- 命令：`arm-none-eabi-gdb build/rtt_deploy/cuav_v5/rt-thread.elf`
- 成功判据：`target remote :3333` 后可读到符号与变量
- 常见失败：ELF 与板上固件不一致、调试器被占用

## 读取 Windows 上的 ArduPilot 串口
- 用途：确认飞控 USB CDC 在 Windows 侧是否出现
- 命令：`powershell.exe -Command "Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match 'ArduPilot' } | Select-Object DeviceID, Description"`
- 成功判据：出现带 `ArduPilot` 描述的 `COMx`
- 常见失败：设备未枚举、被占用、仍停留在 WSL 侧

## 列出 usbipd 设备
- 用途：确认 ST-Link / CMSIS-DAP / USB 设备是否可转入 WSL2
- 命令：`powershell.exe -Command "usbipd list"`
- 成功判据：目标设备出现在列表中
- 常见失败：Windows 侧未识别、设备仍被其他应用占用

## 当前推荐的 MAVLink 验证顺序
- 用途：避免把 WSL2 环境问题误判为固件问题
- 顺序：先看 Windows COM / MissionPlanner 或 Windows 侧 pymavlink，再考虑 WSL2 桥接
- 成功判据：`HEARTBEAT`、关键消息、参数读取在 Windows 侧成立
- 常见失败：直接从 WSL2 结果反推固件错误；环境优先级的正式取舍见 `decision-log.md`

## 当前推荐的逐驱动验证入口
- 用途：在整机之外按驱动、总线、子系统逐层验证
- 入口：先看 `.cursor/project/driver-validation-matrix.md`，再看 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 与 `rtt-driver-validation` Skill
- 成功判据：先确认当前问题已有对应门禁，再决定是补 example/test 规划还是继续整机 smoke
- 常见失败：把 driver-validation 手册误写进 `status`、`open-issues` 或长 trace
