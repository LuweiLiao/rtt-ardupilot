# CUAV V5 RTT 当前验收状态

本文档记录当前 CUAV V5 RT-Thread ArduPilot 分支的最新软件台架验收结果。详细背景、实现说明和复测命令见仓库根目录 `README.md`。

当前证据根目录：

```text
results/execution/loop_rate_goal_20260620T095201Z/
```

机器可读摘要：

```text
results/execution/loop_rate_goal_20260620T095201Z/final_acceptance_summary.json
```

## 总结论

```text
driver-level software bench acceptance: GREEN
```

通过项：

- 主循环性能：GREEN。
- USB 双 CDC 描述符：GREEN。
- USB MAVLink 参数下载：GREEN。
- MAVLink FTP：GREEN。
- IMU/INS、磁力计、气压计、logging driver-level：GREEN。
- SDCard / DataFlash 日志下载：GREEN。
- USB SLCAN / SocketCAN：GREEN。
- 标准 CAN 发送：GREEN。
- DroneCAN-like 扩展帧捕获：GREEN。
- 最终 SCons 编译：GREEN。

边界：

- `calibration_verdict=RED` 是台架未完成 AHRS/pre-arm 校准导致。
- `PreArm: RC not found`、`PreArm: Hardware safety switch`、`PreArm: 3D Accel calibration needed`、`PreArm: Compass not calibrated` 属于实机飞行准备条件，不作为 RTT driver-level 失败。
- 官方 `dronecan` Python 库本轮未解出 NodeStatus；当前 DroneCAN-like 验收使用 SocketCAN/candump 扩展帧 fallback，source node id 为 10。

## 主循环性能

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
```

解释：

- `SCHED_LOOP_RATE` 未降低，仍为 400。
- 未关闭 arming/system check。
- 未屏蔽 `PreArm: Main loop slow`。
- 300 秒 gate 内未出现 `Main loop slow`，也未出现 `IMU0: fast sampling enabled 0.0kHz/0.0kHz`。
- `Rate CPU normal, rate set to 250Hz` 来自 Copter fast-rate/rate-controller thread，不是主调度 `SCHED_LOOP_RATE`。

## USB / 参数 / MAVFTP

USB 描述符证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/usb_descriptor_20260620T180219Z/usb_descriptor_gate.json
```

参数下载证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/acceptance_suite_20260620T180635Z/param_download/param_download.json
```

结果：

```text
reported_count=947
unique_indices=947
missing_count=0
elapsed_s=1.406
rate_params_s=673.3
```

MAVFTP 证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/mavftp_retry_guard_20260620T181233Z/mavftp_gate.json
```

结果：

```text
verdict=GREEN
list "/"=Success
list "/APM"=Success
@PARAM/param.pck decoded_count=947
SD write/read/remove=Success
post_ftp_stability=true
```

## 外设和日志

外设健康证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/peripheral_health_20260620T181420Z/peripheral_health.json
```

结果：

```text
verdict=GREEN
driver_verdict=GREEN
gyro healthy=true
accel healthy=true
mag healthy=true
baro healthy=true
logging healthy=true
RAW_IMU/ATTITUDE/EKF_STATUS_REPORT/SCALED_PRESSURE present
```

日志下载证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/log_download_20260620T181524Z/log_download_gate.json
```

结果：

```text
verdict=GREEN
reason=log_list_download_restore_ok
download bytes=93696
sha256=c8ea67dddaddc569359ef621ff7a4e3dadda2576d47f922b6c9c9ceb7f30ca16
```

## USB SLCAN / CAN / DroneCAN-like

证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/socketcan_dronecan_sudo_20260620T182307Z/socketcan_dronecan_gate.json
```

结果：

```text
verdict=GREEN
reason=socketcan_slcan_dronecan_gate_ok
slcand iface_exists=true
cansend standard_ok=true
cansend extended_ok=true
candump line_count=4
dronecan_fallback verdict=GREEN
dronecan_fallback source_node_ids=[10]
```

说明：

- 当前用户接线按 CAN1 验收。
- `slcand` 需要 `TIOCSETD` 权限，本轮使用 `--sudo-system-tools`。
- `candump` 捕获到扩展 DroneCAN-like 帧 `104E2D0A`，source node id 为 10。

## 构建

最终编译证据：

```text
results/execution/loop_rate_goal_20260620T095201Z/final_build_20260620T182452Z.log
```

结果：

```text
scons: done building targets.
Binary integrity check PASSED
ROM used: 1441384 B / 1504 KB = 93.59%
RAM_STACK: 86296 B / 128 KB = 65.84%
RAM_DMA: 25184 B / 64 KB = 38.43%
RAM_APP: 55988 B / 320 KB = 17.09%
```

## 工作空间整理

过程目录和旧实验产物已经移动到：

```text
results/recycle_bin/20260620T182654Z_loop_rate_goal_process_artifacts/
```

最终保留证据集中在：

```text
results/execution/loop_rate_goal_20260620T095201Z/
```

