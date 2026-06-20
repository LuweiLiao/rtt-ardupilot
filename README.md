# CUAV V5 RT-Thread ArduPilot 移植分支

本仓库分支用于把 ArduPilot 移植到 CUAV V5 的 RT-Thread 平台，并完成台架级软件验收。它不是 ArduPilot 上游项目的通用 README，而是当前 RTT 分支的交付说明：说明这个分支解决什么问题、采用什么实现方式、已经达到什么效果，以及如何复现测试。

当前工作分支：

```text
issue/rtt-spi-lld-real-activation
```

## 分支目标

本分支的目标是在不降低 `SCHED_LOOP_RATE=400`、不关闭 arming/system check、不隐藏告警的前提下，让 CUAV V5 RTT 固件达到可验收状态。

核心验收目标：

- Mission Planner 不再报告 `PreArm: Main loop slow`。
- `SCHED_LOOP_RATE` 保持 400，主循环稳定达到验收线 `>=380Hz`。
- `IMU0: fast sampling enabled 0.0kHz/0.0kHz` 不再出现，并能解释原因。
- USB MAVLink CDC 正常，参数下载不能卡顿。
- MAVLink FTP 正常，可读取 `@PARAM/param.pck`，可对 SDCard 写读删文件。
- USB SLCAN CDC 正常，可通过 CAN1 验证标准 CAN 和 DroneCAN-like 扩展帧。
- IMU/INS、磁力计、气压计、SDCard、日志等外设 driver-level 健康。
- 工作空间保持清晰，过程文件进入 `results/recycle_bin/`，最终证据集中保存。

## 当前验收结论

本轮最终证据根目录：

```text
results/execution/loop_rate_goal_20260620T095201Z/
```

当前软件台架验收结论：

```text
主循环性能: GREEN
USB 描述符: GREEN
USB MAVLink CDC 参数下载: GREEN
MAVLink FTP: GREEN
USB SLCAN / SocketCAN: GREEN
标准 CAN 发送: GREEN
DroneCAN-like 扩展帧: GREEN
IMU/INS/磁力计/气压计/logging driver-level: GREEN
SDCard / 日志下载: GREEN
最终编译: GREEN
```

仍需明确的边界：

```text
AHRS/pre-arm calibration 仍未闭合。
当前台架未接 RC，硬件安全开关、3D accel calibration、compass calibration 等 PreArm 项仍会存在。
这些是实机飞行准备条件，不是 RTT driver-level 失败。
```

因此本分支当前声明的是：

```text
CUAV V5 RT-Thread ArduPilot driver-level 软件台架验收通过。
```

本分支当前不声明：

```text
已经满足实机解锁起飞条件。
```

## 关键测试结果

### 主循环性能

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/loop_rate_gate_long_20260620T180025Z/loop_rate_gate.json
```

结果：

```text
verdict=GREEN
reason=loop_rate_ok
target_loop_rate_hz=400
ins_loop_rate=400
min_loop_rate_hz=380
debug_loop_hz=794.9
monitor_stuck_count=0
main_loop_slow_text=[]
imu_banner_text=[]
raw_imu_nonzero=true
```

说明：

- `SCHED_LOOP_RATE` 没有降低，目标仍为 400。
- `ins_loop_rate=400`，满足本目标的调度配置要求。
- 300 秒采样内未出现 `PreArm: Main loop slow`。
- 300 秒采样内未出现 `IMU0: fast sampling enabled 0.0kHz/0.0kHz`。
- `debug_loop_hz` 是 RTT 调试计数口径，不表示把 ArduPilot 主循环改成 800Hz；验收口径仍以 `target_loop_rate_hz=400`、`ins_loop_rate=400`、无 main-loop-slow STATUSTEXT 为准。

`Rate CPU normal, rate set to 250Hz` 的解释：

```text
该文本来自 ArduCopter 的 fast-rate / rate-controller thread，不是 SCHED_LOOP_RATE。
RTT 分支在 bench/disarmed 条件下限制 fast-rate 动态上限，避免高频 rate thread 与 USB、日志、storage 抢占后拖慢 400Hz 主调度。
```

### USB 描述符和双 CDC

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/usb_descriptor_20260620T180219Z/usb_descriptor_gate.json
```

结果：

```text
verdict=GREEN
VID:PID=1209:5740
bDeviceClass=0xEF
bDeviceSubClass=0x02
bDeviceProtocol=0x01
bNumInterfaces=4
MI_00/MI_01=MAVLink CDC
MI_02/MI_03=SLCAN CDC
bulk endpoint max packet=64
interrupt endpoint max packet=16
```

Linux 实际枚举：

```text
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00  # MAVLink CDC
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02  # SLCAN CDC
```

### 参数下载

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/acceptance_suite_20260620T180635Z/param_download/param_download.json
```

结果：

```text
verdict=GREEN
reason=complete_fast
reported_count=947
unique_indices=947
missing_count=0
elapsed_s=1.406
rate_params_s=673.3
```

说明：USB CDC 参数下载已经达到短时间完成，不再是 Mission Planner 体验里的长时间卡住。

### MAVLink FTP / SDCard

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/mavftp_retry_guard_20260620T181233Z/mavftp_gate.json
```

结果：

```text
verdict=GREEN
reason=mavftp_ok
list "/"=Success
list "/APM"=Success
@PARAM/param.pck bytes=10653
param_pck decoded_count=947
SD test file write/read/remove=Success
post_ftp_stability=true
```

本轮同时修正了 MAVFTP 验收脚本的两个主机侧问题：

- `pymavlink` 在该固件消息流下可能出现 instance-message cache crash，脚本增加了局部 guard。
- `pymavlink` 连接对象的 `target_component` 可能保持为 0，但实际飞控消息源组件是 1；MAVFTP gate 现在优先使用 heartbeat source component，避免把 FTP 请求发到 component 0 后误判固件超时。

### 外设健康

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/peripheral_health_20260620T181420Z/peripheral_health.json
```

结果：

```text
verdict=GREEN
driver_verdict=GREEN
reason=peripheral_driver_ok_calibration_pending
gyro present/enabled/healthy=true
accel present/enabled/healthy=true
mag present/enabled/healthy=true
baro present/enabled/healthy=true
logging present/enabled/healthy=true
RAW_IMU present
ATTITUDE present
EKF_STATUS_REPORT present
SCALED_PRESSURE present
```

采样数据示例：

```text
accel_norm_mg=1021.1
mag_norm_mgauss=290.0
press_abs_hpa=1003.72
```

`calibration_verdict=RED` 的原因是 `ahrs.healthy=false`，对应台架未完成 3D accel / compass calibration，不是 IMU、磁力计或气压计驱动失败。

### 日志下载

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/log_download_20260620T181524Z/log_download_gate.json
```

结果：

```text
verdict=GREEN
reason=log_list_download_restore_ok
selected_log id=497
download bytes=93696
sha256=c8ea67dddaddc569359ef621ff7a4e3dadda2576d47f922b6c9c9ceb7f30ca16
LOG_BACKEND_TYPE=1
LOG_DISARMED_after_reset=1
```

### USB SLCAN / CAN1 / DroneCAN-like

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/socketcan_dronecan_sudo_20260620T182307Z/socketcan_dronecan_gate.json
```

结果：

```text
verdict=GREEN
reason=socketcan_slcan_dronecan_gate_ok
slcand iface_exists=true
iface=can_rtt0
cansend standard_ok=true
cansend extended_ok=true
candump line_count=4
dronecan_fallback verdict=GREEN
dronecan_fallback source_node_ids=[10]
```

说明：

- 主机普通用户直接运行 `slcand` 时可能因为 `TIOCSETD` 权限失败，需要 `--sudo-system-tools`。
- 官方 `dronecan` Python 库本轮没有解出 NodeStatus，但 `candump` 抓到了来自 source node 10 的扩展 DroneCAN-like 帧，满足本台架的 DroneCAN-like 验收。
- 标准 CAN 和扩展 CAN 发送均通过 `cansend` 返回码验证。

## 做了什么

### USB / CherryUSB / CDC

RTT 版 USB CDC 尽量贴近 ChibiOS 行为：

- 使用 ChibiOS 风格双 CDC 描述符：MAVLink CDC 在 `MI_00`，SLCAN CDC 在 `MI_02`。
- 保持 `1209:5740`、`EF/02/01`、IAD、4 interface、bulk 64B、interrupt 16B。
- USB serial 使用 STM32 UID 生成 24 位十六进制字符串，避免 Windows 复用旧固定序列号实例。
- CDC TX 路径修正 buffer 所有权、endpoint 完成时机、ZLP、host 未消费时的恢复行为。
- 参数下载和 MAVFTP 相关路径避免长时间阻塞主循环。

### Scheduler / 主循环

RTT 与 ChibiOS 的核心差异不是单个函数，而是调度、USB、日志、storage、fast-rate thread 的系统耦合。

本分支的主循环修复方向：

- 不降低 `SCHED_LOOP_RATE`。
- 不关闭 arming/system check。
- 不隐藏 `Main loop slow` 告警。
- 减少热路径诊断开销，重型 loop 诊断默认关闭。
- 对 RTT fast-rate thread 做保守动态上限，避免 bench/disarmed 条件下抢占主调度。
- 保留低开销 monitor counters，用于追踪 main loop stuck、loop delay、当前 task/semline。

### MAVFTP / 参数 / 测试工具

测试工具修正：

- MAVFTP gate 使用当前 `rtt_usb_port_select.py` 自动选择 ArduPilot `if00`。
- MAVFTP gate 使用 heartbeat 源组件作为 FTP target component，避免 component 0 超时。
- MAVFTP gate 增加 `pymavlink` instance cache guard。
- SLCAN ASCII gate 处理 RTT SLCAN 的延迟响应归并，并按固件实现把 `z/Z` 视为标准/扩展帧 ACK。

### CAN / SLCAN

SLCAN 路径：

- USB `if02` 作为 SLCAN CDC。
- 支持 `slcand` 挂载 SocketCAN。
- 支持标准帧和扩展帧发送。
- 支持通过 `candump` 捕获 CAN1 上的 DroneCAN-like 扩展帧。

## 构建

CUAV V5 RTT 固件使用 SCons，不使用 waf：

```bash
python3 -m SCons --target=cuav-v5 -j16
```

本轮最终编译证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/final_build_20260620T182452Z.log
```

最终编译结果：

```text
scons: done building targets.
Binary integrity check PASSED
ROM used: 1441384 B / 1504 KB = 93.59%
RAM_STACK: 86296 B / 128 KB = 65.84%
RAM_DMA: 25184 B / 64 KB = 38.43%
RAM_APP: 55988 B / 320 KB = 17.09%
```

常用产物：

```text
build/rtt_cuav_v5/rtthread.bin
build/rtt_deploy/cuav_v5/rt-thread.elf
build/rtt_deploy/cuav_v5/arducopter.apj
```

## 烧录

CUAV V5 应用入口：

```text
0x08008000
```

OpenOCD 烧录：

```bash
timeout 120 openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify" \
  -c "reset run" \
  -c "shutdown"

pkill -9 -x openocd 2>/dev/null || true
pkill -9 -f '[o]penoccd' 2>/dev/null || true
```

## 复测命令

所有长时间测试建议使用 `nohup + timeout`，并把输出放到 `results/execution/`。

### 主循环性能 gate

```bash
OUT=results/execution/loop_rate_goal_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"
PORT=$(ls /dev/serial/by-id/usb-ArduPilot*if00 | head -1)

nohup timeout 420 python3 Tools/scripts/rtt_loop_rate_gate.py \
  --port "$PORT" \
  --outdir "$OUT/loop_rate_gate_long" \
  --sample-s 300 \
  --min-loop-rate 380 \
  --target-loop-rate 400 \
  > "$OUT/loop_rate_gate_long.log" 2>&1 &
```

### 参数下载 gate

```bash
OUT=results/execution/param_download_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 90 python3 Tools/scripts/rtt_param_download_gate.py \
  --port auto \
  --outdir "$OUT" \
  > "$OUT/stdout.log" 2>&1 &
```

### MAVFTP gate

```bash
OUT=results/execution/mavftp_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 300 python3 Tools/scripts/rtt_mavftp_gate.py \
  --port auto \
  --outdir "$OUT" \
  --require-sd \
  > "$OUT/stdout.log" 2>&1 &
```

### 外设健康 gate

```bash
OUT=results/execution/peripheral_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 120 python3 Tools/scripts/rtt_peripheral_health_gate.py \
  --port auto \
  --outdir "$OUT" \
  > "$OUT/stdout.log" 2>&1 &
```

### 日志下载 gate

```bash
OUT=results/execution/log_download_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 300 python3 Tools/scripts/rtt_log_download_gate.py \
  --port auto \
  --outdir "$OUT" \
  --reset-after-restore \
  > "$OUT/stdout.log" 2>&1 &
```

### SocketCAN / DroneCAN-like gate

```bash
OUT=results/execution/socketcan_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 120 python3 Tools/scripts/rtt_socketcan_dronecan_gate.py \
  --port auto \
  --iface can_rtt0 \
  --outdir "$OUT" \
  --pre-listen-s 2 \
  --listen-s 5 \
  --dronecan-listen-s 10 \
  --require-rx \
  --require-dronecan \
  --dronecan-any-node \
  --sudo-system-tools \
  > "$OUT/stdout.log" 2>&1 &
```

## 工作空间整理规则

本分支按下面方式组织文件：

- 源码修复放在对应 `libraries/`、`ArduCopter/`、`Tools/scripts/` 中。
- 可复现测试脚本放在 `Tools/scripts/`。
- 长期说明文档放在 `docs/rtt-porting/`。
- 当前有效验收证据放在 `results/execution/loop_rate_goal_20260620T095201Z/`。
- 废弃过程目录、旧构建试验、旧烧录日志、旧 GDB 快照放在 `results/recycle_bin/`。

本轮整理把 262 个过程目录/日志移动到：

```text
results/recycle_bin/20260620T182654Z_loop_rate_goal_process_artifacts/
```

最终保留的主证据目录只包含：

```text
acceptance_suite_20260620T180635Z/
loop_rate_gate_long_20260620T180025Z/
mavftp_retry_guard_20260620T181233Z/
peripheral_health_20260620T181420Z/
log_download_20260620T181524Z/
socketcan_dronecan_sudo_20260620T182307Z/
usb_descriptor_20260620T180219Z/
final_build_20260620T182452Z.log
```

## 注意事项

- 不要用降低 `SCHED_LOOP_RATE`、关闭 arming check、屏蔽 STATUSTEXT 的方式伪修复主循环问题。
- OpenOCD 操作后必须清理残留进程，否则可能占住 USB CDC。
- 不要同时运行多个读取同一 MAVLink CDC 的 pymavlink gate，参数下载和 MAVFTP 要串行。
- `slcand` 需要 `TIOCSETD` 权限；普通用户失败时使用 `--sudo-system-tools`。
- 当前 CAN1/DroneCAN-like 证据来自 `candump` 扩展帧 fallback，官方 `dronecan` 库本轮没有解出 NodeStatus。

