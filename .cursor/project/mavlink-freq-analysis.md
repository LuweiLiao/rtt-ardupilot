# MAVLink 频率分析与修复

> 日期：2026-03-31
> 状态：**已解决**

## 问题描述

地面站（Mission Planner / QGC）连接 RTT 版 Copter 后，MAVLink 数据频率极低，影响飞行监控体验。

## 诊断数据

### 修复前（三层 bug 全在时）
| 指标 | 值 |
|------|-----|
| 总消息量 | 5.6 msgs/s |
| ATTITUDE | 0.2 Hz |
| RAW_IMU | 0.33 Hz |
| SYS_STATUS | 0.33 Hz |
| 主循环 | ~400Hz |
| CPU 空闲 | ~99% |

### 修复后（默认流率）
| 指标 | 值 |
|------|-----|
| 总消息量 | **96.6 msgs/s** |
| ATTITUDE | **13.3 Hz** |
| RAW_IMU | 5.4 Hz |
| SYS_STATUS | 4.1 Hz |
| VFR_HUD | 5.4 Hz |
| SCALED_IMU2/3 | 5.4 Hz |
| SCALED_PRESSURE | 5.4 Hz |

### 已知限制
- `SET_MESSAGE_INTERVAL` 设置特定频率后反而退化（96.6 → 16.2 msgs/s），原因待查
- 启动前 5s 可能有缓冲清空（clears），稳态后 clears=0

## 根因分析（三层）

### 第 1 层：`call_delay_cb()` 未被调用
- **路径**：ChibiOS 的 `delay()` → `call_delay_cb()` → `scheduler_delay_callback()` → `gcs().update_send()`
- **RTT 问题**：`wait_for_sample()` 使用 `delay_microseconds_boost()`，后者调用 `delay_microseconds()` 而非 `delay()`。`delay_microseconds()` 使用 `rt_thread_delay()` / DWT spin，不触发 `call_delay_cb()`
- **结果**：`scheduler_delay_callback()` 从未被调用，所有 GCS 消息只能通过 scheduler task 路径发送

### 第 2 层：Scheduler task 中 GCS 被跳过
- `time_available` 在 `run()` 循环中被递减（每个 task 运行后减去实际耗时）
- GCS `update_send` 的 `_task_time_allowed = 550µs`，是所有 scheduler task 中最大的
- 前面的 20+ 个 task 消耗大部分 `time_available`（2500µs），到 GCS 时剩余 < 550µs
- `if (_task_time_allowed > time_available) continue;` → GCS task 被跳过
- 即使 `call_delay_cb()` 能执行，`should_send_message_in_delay_callback()` 白名单只允许 HEARTBEAT/PARAM/AUTOPILOT_VERSION，ATTITUDE 等关键消息被跳过

### 第 3 层：USB CDC TX ring buffer 溢出 + fail count 逻辑错误
- `CONFIG_USBDEV_SERIAL_TX_BUFSIZE` 默认 2048B，96 msgs/s × ~50B/msg ≈ 4.8KB/s
- `_usb_write_fail_count` 每 1ms timer tick 无条件 `++`，100 ticks 后清空写缓冲（丢数据）
- 实际上 `rt_device_write` 成功时也应重置计数器

## 修复内容

| 修复 | 文件 | 变更 |
|------|------|------|
| 主循环显式调 `call_delay_cb()` | `HAL_RTT_Class.cpp` | `loop()` 后调用 `sched->call_delay_cb()` |
| RTT 允许所有消息在 delay callback 中发送 | `GCS_Common.cpp` | `should_send_message_in_delay_callback()` 对 RTT 返回 true |
| USB CDC TX ring buffer 增大 | `rtconfig.h` | `CONFIG_USBDEV_SERIAL_TX_BUFSIZE = 8192` |
| `_usb_write_fail_count` 逻辑修正 | `UARTDriver.cpp` | 仅在 buffer 非空时递增，buffer 清空时重置 |
| 默认流率提高 | `GCS_Common.cpp` | `initialise_message_intervals_from_streamrates()` fallback 值提高 |

## 验证方法

```bash
# UART7 msh 诊断
ap_rate                    # CPU/loop 统计
# [USB0] wb_avail=0 fails=0 clears=0  # 每 5s 自动打印

# USB CDC MAVLink 频率测试
python3 -c "
from pymavlink import mavutil; import time, collections
m = mavutil.mavlink_connection('/dev/ttyACM1', baud=115200)
m.wait_heartbeat(timeout=8)
time.sleep(3)
counts = collections.Counter()
start = time.time()
while time.time() - start < 10:
    msg = m.recv_match(timeout=1)
    if msg: counts[msg.get_type()] += 1
for t, c in sorted(counts.items(), key=lambda x:-x[1])[:8]:
    print(f'{t}: {c/10:.1f}Hz')
"
```
