# CUAV V5 RT-Thread ArduPilot 分支说明

这个仓库分支用于把 ArduPilot 移植到 CUAV V5 的 RT-Thread 平台，并把主循环、USB、CAN、MAVLink、MAVFTP、IMU/INS、磁力计、气压计、SDCard 和日志链路做成可复现的台架验收状态。

这不是 ArduPilot 上游通用 README。本文件只记录当前 RTT 分支的目标、实现方式、验收结果和复现方法。

当前分支：

```text
issue/rtt-spi-lld-real-activation
```

## 分支目标

本分支解决 CUAV V5 RTT 固件在 Mission Planner 中暴露的两个核心问题：

```text
PreArm: Main loop slow (约 190-230Hz < 400Hz)
IMU0: fast sampling enabled 0.0kHz/0.0kHz
```

验收时必须同时满足：

- `SCHED_LOOP_RATE` 保持 400。
- 不关闭 arming check、system check。
- 不通过降低 loop rate 或隐藏告警来假修复。
- 主循环稳定达到 `>=380Hz`，最好接近 400Hz。
- Mission Planner / MAVLink STATUSTEXT 不再出现 `Main loop slow`。
- `IMU0: fast sampling enabled 0.0kHz/0.0kHz` 不再出现，并且原因已解释。
- USB MAVLink CDC 正常，参数下载要在短时间完成。
- USB SLCAN CDC 正常。
- MAVLink FTP 正常。
- 标准 CAN 和 DroneCAN-like 扩展帧正常。
- IMU/INS、磁力计、气压计、SDCard、日志链路不回退。
- 工作空间清晰，失败实验和过程材料进入专用回收区。

## 当前结论

当前 release 固件的台架验收结论：

```text
主循环性能: GREEN
SCHED_LOOP_RATE=400: GREEN
IMU fast-sampling 0.0kHz 告警: 已消除
USB MAVLink CDC: GREEN
USB 参数下载: GREEN
MAVLink FTP: GREEN
USB SLCAN -> SocketCAN: GREEN
标准 CAN cansend: GREEN
DroneCAN / pydronecan NodeStatus: GREEN
IMU/INS: GREEN
磁力计: GREEN
气压计: GREEN
SDCard / 日志: GREEN
CAN 测试后 MAVLink 回归: GREEN
```

边界说明：

- 台架未接 RC，未做 3D accel / compass calibration，未解除硬件安全开关，因此 Mission Planner 仍可能显示对应 PreArm 项。
- 这些 PreArm 项属于飞行前实机条件，不是本分支的 RTT driver-level 失败。
- 本分支当前声明的是 `CUAV V5 RT-Thread ArduPilot driver-level 软件台架验收通过`，不是声明已经满足实机解锁起飞条件。

## 关键实现

### 1. 主循环修复：RT-Thread hrtimer 对齐 ChibiOS 思路

ChibiOS 的 `delay_microseconds_boost()` 不是简单忙等。它会提升主线程优先级，然后用高精度定时等待，让 SPI/IMU producer 等线程仍然有机会运行。

RTT 之前的问题在于只有两种不理想路径：

- 用 RT-Thread tick 级 delay，1kHz tick 粒度太粗，400Hz 主循环容易抖动。
- 用 DWT busy-wait，主线程不让出 CPU，IMU producer 容易被饿死。

本分支新增 STM32F7 high-resolution clock-time 后端：

```text
libraries/AP_HAL_RTT/hwdef/common/board/rtt_clock_time_stm32f7.c
```

实现方式：

- DWT `CYCCNT` 作为 high-resolution time source。
- TIM5 配成 1MHz one-shot event timer。
- 启用 RT-Thread `RT_USING_CLOCK_TIME`。
- 在 RTT `Scheduler::delay_microseconds_boost()` 中优先使用 `rt_clock_hrtimer_udelay()`。
- hrtimer 失败时保留已有 fallback，不把主循环绑死在单一路径上。

相关配置：

```text
libraries/AP_HAL_RTT/hwdef/common/.config
libraries/AP_HAL_RTT/hwdef/common/rtconfig.h
libraries/AP_HAL_RTT/hwdef/common/board/SConscript
libraries/AP_HAL_RTT/Scheduler.cpp
```

这使 RTT 的 boosted micro-delay 接近 ChibiOS 行为：主循环能按 400Hz 节拍等待，同时不饿死 IMU/INS 数据生产线程。

### 2. USB MAVLink 与参数下载

USB MAVLink CDC 保持 Mission Planner / pymavlink 可直接连接。参数下载使用 MAVLink 参数协议完整拉取，验收要求不是“能收到几个参数”，而是：

- 参数总数完整。
- 无缺失 index。
- 首包延迟短。
- 全量下载在秒级完成。
- 下载过程中 MAVLink 心跳和外设流不崩。

### 3. MAVLink FTP 与 SDCard

MAVFTP 验收覆盖：

- list `/`
- list `/APM`
- 读取 `@PARAM/param.pck`
- 解码参数包
- 在 `/APM` 下创建、读取、删除测试文件
- FTP 后心跳继续稳定

这同时验证 MAVFTP、SDCard 文件系统和 MAVLink 长事务处理。

### 4. USB SLCAN 与 CAN

飞控枚举两个虚拟串口：

```text
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00  # MAVLink CDC
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02  # SLCAN CDC
```

SLCAN 验收使用两层：

- 直接 SLCAN ASCII 命令层：飞控端 `C/S8/O/F/V/N` 响应正常。
- Linux SocketCAN：`slcand` 把飞控 USB SLCAN 接成 `can_rtt0`，再用 `cansend/candump/pydronecan` 验证标准 CAN 和 DroneCAN-like 扩展帧。

当前外接 `1a86:55d3 USB Single Serial` 调试器在多个串口波特率下不响应 Lawicel/SLCAN ASCII 命令，因此没有把它作为“双向 ASCII SLCAN 调试器”通过项。但飞控自身 USB SLCAN 已通过 SocketCAN 和官方 `dronecan` Python 包验证。

## 验收证据

所有最终 GREEN 证据保留在：

```text
results/execution/
```

失败实验、被替代实验和旧过程材料已归档到：

```text
archive/recycle/20260622_main_loop_perf_rejected_runs/
```

该目录有 `manifest.json` 记录移动清单。`results/execution` 与 `archive/recycle` 按项目规则不提交 Git，只作为本机验收证据区。

### 主循环性能

诊断构建证据：

```text
results/execution/rtt_hrtimer_diag_gate_20260621T181359Z/loop_rate_gate.json
```

关键结果：

```text
verdict=GREEN
reason=loop_rate_ok
avg_loop_hz=403.66
ins_debug_loop_rate_hz=400
main_loop_slow_text=[]
imu_banner_text=[]
rtt_dbg_boost_hrtimer_count=5434
rtt_dbg_clock_time_irq_count=5434
```

release 构建证据：

```text
results/execution/rtt_hrtimer_release_gate_20260621T181809Z/loop_rate_gate.json
```

关键结果：

```text
verdict=GREEN
reason=loop_rate_ok
avg_loop_hz=406.45
ins_debug_loop_rate_hz=400
main_loop_slow_text=[]
imu_banner_text=[]
rtt_dbg_boost_hrtimer_count=5209
rtt_dbg_clock_time_irq_count=5208
```

CAN 测试后的回归证据：

```text
results/execution/rtt_post_can_loop_rate_20260621T183328Z/loop_rate_gate.json
```

关键结果：

```text
verdict=GREEN
reason=loop_rate_ok
SCHED_LOOP_RATE=400
ins_debug_loop_rate_hz=400
main_loop_slow_text=[]
imu_banner_text=[]
rtt_dbg_boost_hrtimer_count=224545
rtt_dbg_clock_time_irq_count=224521
```

说明：该 post-CAN 历史证据只包含单次 OpenOCD 快照，未形成独立平均 loop rate 基线；当前 `rtt_loop_rate_gate.py` 已默认启用前后双快照，新回归测试会输出 `openocd_average_loop_rate.avg_loop_hz`，完整平均主循环频率以诊断和 release gate 的 `403.66Hz / 406.45Hz` 为准。

### 参数下载

证据：

```text
results/execution/rtt_release_param_download_20260621T181930Z/param_download.json
```

关键结果：

```text
verdict=GREEN
reason=complete_fast
reported_count=947
unique_indices=947
missing_count=0
elapsed_s=1.25
first_response_latency_s=0.006
rate_params_s=757.7
max_gap_s_observed=0.01
```

结论：USB CDC 参数下载为秒级完成，Mission Planner 参数页不应再出现长时间卡住。

### MAVLink FTP

证据：

```text
results/execution/rtt_release_mavftp_20260621T181943Z/mavftp_gate.json
```

关键结果：

```text
verdict=GREEN
reason=mavftp_ok
list "/" 成功
list "/APM" 成功
@PARAM/param.pck 读取成功
decoded_count=947
/APM/test_ftp.tmp 写入、读回、删除成功
FTP 后 heartbeat 稳定
```

### 外设数据流

证据：

```text
results/execution/rtt_release_mavlink_peripherals3_20260621T182253Z/peripherals.json
```

关键结果：

```text
verdict=GREEN
reason=peripheral_streams_ok
RAW_IMU=180
SCALED_IMU=180
ATTITUDE=179
SCALED_PRESSURE=90
EKF_STATUS_REPORT=36
SYS_STATUS=179
raw_imu_present=true
accel_nonzero=true
gyro_backend_present=true
gyro_samples_available=true
mag_nonzero=true
baro_present=true
ins_loop_rate_400=true
no_main_loop_slow_text=true
no_imu_zero_banner=true
```

稳定状态文本证据：

```text
results/execution/rtt_release_status_text_stable_20260621T182424Z/status_text.json
```

关键结果：

```text
verdict=GREEN
reason=stable_status_text_ok
heartbeat_seen=true
status_standby=true
no_main_loop_slow=true
no_imu_zero_banner=true
no_iomcu_unhealthy=true
statustext=[]
```

### SDCard 与日志

证据：

```text
results/execution/rtt_release_log_download_20260621T182521Z/log_download_gate.json
```

关键结果：

```text
verdict=GREEN
reason=log_list_download_restore_ok
selected_log.id=497
selected_log.size=93696
LOG_BACKEND_TYPE=1
```

结论：SDCard / DataFlash 日志链路可以列日志并下载非空日志。

### SLCAN、标准 CAN 与 DroneCAN-like

SocketCAN / DroneCAN 证据：

```text
results/execution/rtt_release_socketcan_dronecan_sudo_20260621T183023Z/socketcan_dronecan_gate.json
```

关键结果：

```text
verdict=GREEN
reason=socketcan_slcan_dronecan_gate_ok
slcand interface=can_rtt0
cansend standard 123#1122334455667788 rc=0
cansend extended 1F015508#1122334455667788 rc=0
candump extended_frame_count=15
candump source_node_ids=[10]
pydronecan NodeStatus seen
dronecan source_node_id=10
dronecan health=0
dronecan mode=0
```

直接 ASCII SLCAN 调试器证据：

```text
results/execution/rtt_release_slcan_ascii_20260621T182844Z/slcan_ascii_gate.json
```

结论：

```text
飞控 SLCAN 命令层: OK
外接 1a86 USB Single Serial 调试器: 未响应 Lawicel/SLCAN 命令
双向 ASCII 调试器验收: RED
```

这不是飞控 USB SLCAN 失败。飞控 SLCAN 已经通过 `slcand + cansend + candump + pydronecan` 完整验证。外接调试器若要作为双向 SLCAN 对端，需要确认它实际刷的是 Lawicel/SLCAN 固件，并处于 SLCAN 模式。

### CAN 后 MAVLink 回归

证据：

```text
results/execution/rtt_post_can_heartbeat_20260621T183152Z/heartbeat_gate.json
results/execution/rtt_post_can_fast_green_20260621T183249Z/fast_green.json
```

关键结果：

```text
heartbeat_count=103
duration_after_first_s=35.99
last_status=3
fast_green verdict=GREEN
RAW_IMU=125
ATTITUDE=126
EKF_STATUS_REPORT=125
SCALED_PRESSURE=125
max_silence=1.001
baro_hpa=1000.66
zacc_mg=-992
```

结论：CAN/SLCAN 会话结束后，MAVLink CDC 和核心外设流仍稳定。

## 构建与烧录

只使用 SCons，不使用 waf。

构建 CUAV V5 ArduCopter：

```bash
python3 -m SCons --target=cuav-v5 -j16
```

带主循环诊断符号构建：

```bash
env HAL_RTT_LOOP_DIAG=1 python3 -m SCons --target=cuav-v5 -j16
```

OpenOCD 烧录应用固件到 `0x08008000`：

```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify reset run" \
  -c "shutdown"
pkill -9 -x openocd 2>/dev/null || true
pkill -9 -f '[o]penoccd' 2>/dev/null || true
```

主要产物：

```text
build/rtt_cuav_v5/rtthread.bin
build/rtt_deploy/cuav_v5/rt-thread.elf
build/rtt_deploy/cuav_v5/arducopter.apj
```

## 验收命令

以下命令均建议使用 `nohup timeout`，避免测试进程残留。

主循环：

```bash
out=results/execution/manual_loop_rate_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$out"
nohup timeout 120 python3 Tools/scripts/rtt_loop_rate_gate.py \
  --port /dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00 \
  --outdir "$out" \
  --sample-s 20 \
  --target-loop-rate 400 \
  --min-loop-rate 380 >"$out/loop_rate_gate.log" 2>&1
pkill -9 -x openocd 2>/dev/null || true
pkill -9 -f '[o]penoccd' 2>/dev/null || true
```

参数下载：

```bash
out=results/execution/manual_param_download_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$out"
nohup timeout 90 python3 Tools/scripts/rtt_param_download_gate.py \
  --port /dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00 \
  --outdir "$out" >"$out/param_download.log" 2>&1
```

MAVFTP：

```bash
out=results/execution/manual_mavftp_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$out"
nohup timeout 120 python3 Tools/scripts/rtt_mavftp_gate.py \
  --port /dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00 \
  --outdir "$out" >"$out/mavftp_gate.log" 2>&1
```

SocketCAN / DroneCAN：

```bash
out=results/execution/manual_socketcan_dronecan_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$out"
nohup timeout 180 python3 Tools/scripts/rtt_socketcan_dronecan_gate.py \
  --port /dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02 \
  --iface can_rtt0 \
  --outdir "$out" \
  --pre-listen-s 5 \
  --listen-s 8 \
  --dronecan-listen-s 12 \
  --require-rx \
  --require-dronecan \
  --dronecan-any-node \
  --sudo-system-tools >"$out/socketcan_dronecan.log" 2>&1
```

心跳稳定性：

```bash
out=results/execution/manual_heartbeat_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$out"
nohup timeout 90 python3 Tools/scripts/rtt_heartbeat_gate.py \
  --port /dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00 \
  --observe 35 \
  --min-duration 30 \
  --min-heartbeats 25 \
  --json "$out/heartbeat_gate.json" >"$out/heartbeat_gate.log" 2>&1
```

## 工作区规则

- 源码、脚本和 README 保持在正常目录。
- `results/execution/` 保存当前验收证据。
- `archive/recycle/` 保存废弃文件、失败实验、旧过程文件和被替代方案。
- 不把失败过程材料堆在 ArduPilot 根目录。
- OpenOCD 操作后必须清理残留进程，避免占住 USB CDC。

## 已知限制

- 外接 `1a86:55d3 USB Single Serial` 设备当前不响应 Lawicel/SLCAN ASCII 命令；需要确认该设备固件和模式后，才能作为双向 SLCAN 调试器使用。
- 台架未做飞行前校准，RC 与安全开关也不在本次软件验收范围内。
- 本分支没有修改 ArduPilot 原生 bootloader 和 IO firmware。
