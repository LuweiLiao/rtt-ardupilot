# CUAV V5 RTT USB CDC / SLCAN 当前状态

本文档记录当前 CUAV V5 RT-Thread ArduPilot 分支的 USB MAVLink CDC、USB SLCAN、SocketCAN 和 CAN1/DroneCAN-like 验收状态。总体验收索引见：

```text
docs/rtt-porting/CUAV_V5_RTT_ACCEPTANCE_CURRENT.md
```

当前证据根目录：

```text
results/execution/loop_rate_goal_20260620T095201Z/
```

## USB 复合设备

静态描述符证据：

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
bcdUSB=0x0200
bcdDevice=0x0200
```

接口布局：

```text
MI_00 / MI_01: MAVLink CDC
MI_02 / MI_03: SLCAN CDC
EP1 IN interrupt: 16 bytes
EP2 OUT/IN bulk: 64 bytes
EP3 IN interrupt: 16 bytes
EP4 OUT/IN bulk: 64 bytes
```

实际 Linux 枚举：

```text
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02
```

说明：

- `if00` 是 MAVLink CDC。
- `if02` 是 SLCAN CDC。
- USB serial 使用 STM32 UID 生成 24 位十六进制字符串。

## MAVLink CDC

参数下载证据：

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

MAVFTP 证据：

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
@PARAM/param.pck decoded_count=947
SD test file write/read/remove=Success
post_ftp_stability=true
```

本轮修正的测试工具注意点：

- `pymavlink` 可能在 RAW_IMU 等 instance-bearing 消息上触发 `_instances=None` crash，MAVFTP gate 已加入局部 guard。
- `conn.target_component` 可能为 0；MAVFTP gate 现在使用 heartbeat source component，当前为 component 1。

## USB SLCAN

SLCAN CDC 端口：

```text
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02
```

SLCAN ASCII gate 已按 RTT 固件行为修正：

- `F/V/N` 响应可能延迟到下一次 host read，脚本会归并判断。
- `z` 是标准帧入队成功 ACK。
- `Z` 是扩展帧入队成功 ACK。
- `\a` 才是错误响应。

## SocketCAN / CAN1

SocketCAN/DroneCAN-like 证据：

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
candump_before line_count=2
candump_after line_count=2
```

标准 CAN 发送：

```text
cansend can_rtt0 123#1122334455667788
rc=0
```

扩展 CAN 发送：

```text
cansend can_rtt0 1F015508#1122334455667788
rc=0
```

DroneCAN-like fallback：

```text
extended_frame_count=4
source_node_ids=[10]
first can_id=104E2D0A
```

官方 `dronecan` Python 库本轮没有解出 NodeStatus：

```text
dronecan.verdict=RED
dronecan.reason=node_status_missing
```

但本台架验收使用 `candump` 扩展帧 fallback，通过 source node id 10 的 DroneCAN-like 帧证明 CAN1 上有扩展 DroneCAN 类流量。

## 复测命令

### MAVLink 参数下载

```bash
OUT=results/execution/param_download_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 90 python3 Tools/scripts/rtt_param_download_gate.py \
  --port auto \
  --outdir "$OUT" \
  > "$OUT/stdout.log" 2>&1 &
```

### MAVFTP

```bash
OUT=results/execution/mavftp_$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"

nohup timeout 300 python3 Tools/scripts/rtt_mavftp_gate.py \
  --port auto \
  --outdir "$OUT" \
  --require-sd \
  > "$OUT/stdout.log" 2>&1 &
```

### SocketCAN / DroneCAN-like

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

说明：

- 普通用户直接运行 `slcand` 可能失败：`ioctl TIOCSETD: Operation not permitted`。
- 使用 `--sudo-system-tools` 让 `slcand`、`ip`、`kill` 走 sudo，同时 Python 仍使用当前 venv。
- gate 结束后会清理 `slcand` 和 `can_rtt0`。

