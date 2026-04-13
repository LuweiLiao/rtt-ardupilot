# Current Focus

## 当前阶段
`CUAV v5` 的 RT-Thread ArduPilot 已完成首次全量 ArduCopter boot 并进入**硬件验证与收尾**阶段。主循环 ~400Hz 稳定运行，CPU 空闲 99%，USB CDC 枚举成功。

## 重大突破（2026-04-12）
- **App 自由运行验证通过**：使用正确 bootloader（ArduPilot CUAVv5_bl.bin），等 12 秒后 app 完全运行
- **主循环 11279 次迭代（60 秒内）**，VTOR=0x08008000，PC 在 app 代码区
- **之前"回到 bootloader"是假象**：bootloader 正常上电等 5 秒才跳转，测试只等了 1~3 秒
- **构建/烧录链路已完整**：scons → openocd program bootloader(0x08000000) + app(0x08008000)

## 当前主线目标
- **UART/USB MAVLink 通信验证**：确认 GCS 能连上
- **RCInput SBUS 验证**：接 SBUS 接收机
- **RCOutput PWM 验证**：通过 GCS 命令驱动电调/舵机
- 完整系统级回归测试

## 当前优先级
1. **串口/MAVLink 验证**：USB CDC 或 UART 确认 GCS 连接
2. **RCInput SBUS 验证**：接 SBUS 接收机验证 RC 通道
3. **Servo 输出验证**：通过 GCS 命令驱动电调/舵机
4. 完整系统级回归测试

## 推荐起手动作
1. 连接 USB 到 PC，等 12 秒后检查 `/dev/ttyACM*` 是否出现
2. MAVLink 测试：`python3 -c "from pymavlink import mavutil; m=mavutil.mavlink_connection('/dev/ttyACM1'); print(m.wait_heartbeat())"`
3. 编译：`python3 -m SCons --target=cuav-v5 -j16`
4. 烧录 bootloader：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program Tools/bootloaders/CUAVv5_bl.bin 0x08000000 verify reset exit"`
5. 烧录 app：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 verify reset exit"`
