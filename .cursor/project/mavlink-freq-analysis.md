# MAVLink 频率分析

## 日期：2026-03-31

## 问题
地面站反馈 MAVLink 数据频率偏低。

## 测试数据

### 默认流率（不设 SET_MESSAGE_INTERVAL）
| 消息 | 频率 |
|------|------|
| ATTITUDE | 9.0 Hz |
| RAW_IMU | 12.3 Hz |
| SYS_STATUS | 12.3 Hz |
| SCALED_PRESSURE | 12.3 Hz |
| HEARTBEAT | 1.3 Hz |

### SET_MESSAGE_INTERVAL ATTITUDE=10Hz (100ms)
- 实际收到：4.5 Hz
- max_gap: 1171ms（超过1秒无数据）
- COMMAND_ACK: ACCEPTED ✅

### SET_MESSAGE_INTERVAL ATTITUDE=50Hz (20ms)
- 实际收到：0.6 Hz（严重偏离）

### SET_MESSAGE_INTERVAL ATTITUDE=20Hz (50ms)
- 实际收到：13.6 Hz
- max_gap: 219ms

## 已排除的原因
1. USB CDC 吞吐 — 不是瓶颈
2. SCHED_LOOP_RATE=400Hz — 正确
3. cap_message_interval — 不限制
4. SET_MESSAGE_INTERVAL 命令 — 被 ACCEPTED

## 待排查
1. `update_send()` 时间预算是否足够（GCS task 550µs）
2. USB CDC tx_rb 满时写入行为
3. `_usb_write_fail_count` 丢包情况
4. 主循环 yield/delay 对 GCS 调度的影响
5. 消息序列化+发送的 CPU 耗时

## SD 卡状态
- USB CDC 枚举正常，无 SD 卡相关 STATUSTEXT
- 需确认是否有 SD 卡物理插入
