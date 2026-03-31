# Current Focus

## 当前阶段
`CUAV v5` 的 RT-Thread ArduPilot **已完成首次全量 ArduCopter boot**。主循环 ~402Hz 稳定运行，CPU 空闲 99%，USB CDC 枚举成功。当前进入"MAVLink 验证 + SD 卡 + 日志 + 系统级收尾"阶段。

## 当前主线目标
- 对齐度约 **97%**（从 75-80% 经多轮深度对齐提升）
- 全量 ArduCopter 固件首次完整 boot：setup() 完成 → 主循环 402Hz → CPU idle 99%
- USB CDC 成功枚举（`ArduPilot CUAVv5 RTT` @ `/dev/ttyACM3`）
- 剩余：MAVLink 验证（需串口权限）→ SD 卡验证 → 日志 → 完整系统级验证

## 当前优先级
1. **串口权限**：`sudo usermod -a -G dialout llw` 并重新登录（ttyACM3 需 dialout 组）
2. MAVLink 连通验证：心跳 + 参数下载 + 传感器数据
3. SD 卡物理挂载验证
4. 日志写入验证
5. 完整系统级回归测试（MAVLink 压测）

## 本轮已修复的关键问题
- ROM overflow: -Os + 禁用 ETH（2.06MB → 1.27MB）
- SPI4 DMA 传输挂起：改用轮询模式
- .sram1_bss 与堆重叠：HEAP_BEGIN 改用 &_end
- C++ 构造函数在堆前执行：startup_rtt_override.S 跳过 __libc_init_array

## 暂不处理
- CAN 总线（当前硬件不需要）
- IOMCU（需要独立固件支持）
- DShot 完整协议（需 DMA + 定时器捕获）
- SPI DMA 模式修复（轮询模式已足够）

## 推荐起手动作
1. `sudo usermod -a -G dialout llw` + 注销重登
2. MAVLink 测试：`python3 -c "from pymavlink import mavutil; m=mavutil.mavlink_connection('/dev/ttyACM3'); print(m.wait_heartbeat())"`
3. GDB 验证：`openocd` + `arm-none-eabi-gdb` 连接后读 `rtt_dbg_*` 变量
4. 编译：`python ./waf copter --board rtt_cuav_v5 -j$(nproc)`
