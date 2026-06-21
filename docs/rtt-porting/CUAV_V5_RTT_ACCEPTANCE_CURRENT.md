# CUAV V5 RTT 当前验收状态

本文档记录当前 CUAV V5 RT-Thread ArduPilot 分支的软件台架验收状态。详细背景、实现说明和复测命令见仓库根目录 `README.md`。

当前分支：

```text
issue/rtt-spi-lld-real-activation
```

当前提交基线：

```text
47e1145df7 AP_HAL_RTT: stabilize CUAV V5 loop rate
```

最终 GREEN 证据保留在：

```text
results/execution/
```

失败实验、被替代实验和旧过程材料已归档到：

```text
archive/recycle/20260622_main_loop_perf_rejected_runs/
```

## 总结论

```text
CUAV V5 RT-Thread ArduPilot driver-level software bench acceptance: GREEN
```

通过项：

- 主循环性能：GREEN。
- `SCHED_LOOP_RATE=400`：GREEN。
- `PreArm: Main loop slow`：未再出现。
- `IMU0: fast sampling enabled 0.0kHz/0.0kHz`：未再出现。
- USB MAVLink CDC：GREEN。
- USB 参数下载：GREEN。
- MAVLink FTP：GREEN。
- IMU/INS、磁力计、气压计、logging driver-level：GREEN。
- SDCard / DataFlash 日志下载：GREEN。
- USB SLCAN / SocketCAN：GREEN。
- 标准 CAN 发送：GREEN。
- DroneCAN / pydronecan NodeStatus：GREEN。
- CAN 测试后 MAVLink 回归：GREEN。
- 最终 SCons 编译：GREEN。

边界：

- 台架未接 RC，未做 3D accel / compass calibration，未解除硬件安全开关，因此 Mission Planner 仍可能显示对应 PreArm 项。
- 这些 PreArm 项属于实机飞行准备条件，不是 RTT driver-level 失败。
- 外接 `1a86:55d3 USB Single Serial` 设备当前不响应 Lawicel/SLCAN ASCII 命令；飞控自身 USB SLCAN 已通过 `slcand + cansend + candump + pydronecan` 验证。

## 主循环性能

诊断构建证据：

```text
results/execution/rtt_hrtimer_diag_gate_20260621T181359Z/loop_rate_gate.json
```

结果：

```text
verdict=GREEN
reason=loop_rate_ok
avg_loop_hz=403.66
ins_loop_rate=400
main_loop_slow_text=[]
imu_banner_text=[]
rtt_dbg_boost_hrtimer_count=5434
rtt_dbg_clock_time_irq_count=5434
```

Release 构建证据：

```text
results/execution/rtt_hrtimer_release_gate_20260621T181809Z/loop_rate_gate.json
```

结果：

```text
verdict=GREEN
reason=loop_rate_ok
avg_loop_hz=406.45
ins_loop_rate=400
main_loop_slow_text=[]
imu_banner_text=[]
rtt_dbg_boost_hrtimer_count=5209
rtt_dbg_clock_time_irq_count=5208
```

CAN 测试后回归证据：

```text
results/execution/rtt_post_can_loop_rate_20260621T183328Z/loop_rate_gate.json
```

结果：

```text
verdict=GREEN
reason=loop_rate_ok
SCHED_LOOP_RATE=400
ins_loop_rate=400
main_loop_slow_text=[]
imu_banner_text=[]
rtt_dbg_boost_hrtimer_count=224545
rtt_dbg_clock_time_irq_count=224521
```

说明：

- `SCHED_LOOP_RATE` 未降低，仍为 400。
- 未关闭 arming/system check。
- 未屏蔽 `PreArm: Main loop slow`。
- 主循环修复通过 STM32F7 DWT + TIM5 high-resolution clock-time 后端实现，RTT `delay_microseconds_boost()` 走 `rt_clock_hrtimer_udelay()`，接近 ChibiOS 的高精度 timer sleep 行为。

## USB / 参数 / MAVFTP

参数下载证据：

```text
results/execution/rtt_release_param_download_20260621T181930Z/param_download.json
```

结果：

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

MAVFTP 证据：

```text
results/execution/rtt_release_mavftp_20260621T181943Z/mavftp_gate.json
```

结果：

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

## 外设和日志

外设数据流证据：

```text
results/execution/rtt_release_mavlink_peripherals3_20260621T182253Z/peripherals.json
```

结果：

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

结果：

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

日志下载证据：

```text
results/execution/rtt_release_log_download_20260621T182521Z/log_download_gate.json
```

结果：

```text
verdict=GREEN
reason=log_list_download_restore_ok
selected_log.id=497
selected_log.size=93696
LOG_BACKEND_TYPE=1
```

## USB SLCAN / CAN / DroneCAN

SocketCAN / DroneCAN 证据：

```text
results/execution/rtt_release_socketcan_dronecan_sudo_20260621T183023Z/socketcan_dronecan_gate.json
```

结果：

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

结果：

```text
飞控 SLCAN 命令层: OK
外接 1a86 USB Single Serial 调试器: 未响应 Lawicel/SLCAN 命令
双向 ASCII 调试器验收: RED
```

## CAN 后 MAVLink 回归

证据：

```text
results/execution/rtt_post_can_heartbeat_20260621T183152Z/heartbeat_gate.json
results/execution/rtt_post_can_fast_green_20260621T183249Z/fast_green.json
```

结果：

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

## 构建

最终编译证据：

```text
results/execution/rtt_final_build_20260621T183842Z/build.log
```

结果：

```text
scons: done building targets.
Binary integrity check PASSED
ROM used: 1443268 B / 1504 KB = 93.71%
RAM_STACK: 86296 B / 128 KB = 65.84%
RAM_DMA: 13760 B / 64 KB = 21.00%
RAM_APP: 56348 B / 320 KB = 17.20%
```

## 替代 ChibiOS 的后续差距

当前验收是 CUAV V5 driver-level bench GREEN，不等于已经完全替代 ChibiOS。替代 ChibiOS 的差距台账见：

```text
docs/rtt-porting/RTT_CHIBIOS_REPLACEMENT_GAP_LEDGER.md
```
