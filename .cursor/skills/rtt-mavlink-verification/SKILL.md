---
name: rtt-mavlink-verification
description: 验证 AP_HAL_RTT 的 USB CDC 与 MAVLink 链路，适用于用户提到 CDC、MAVLink、MissionPlanner、MAVProxy、参数下载、WSL2 串口桥接、Windows COM 或 usbipd 时。
---

# RTT MAVLink Verification

## 验证原则

- Windows 主机侧的结果，优先级高于 WSL2 `usbipd` 下的 CDC 结果
- “枚举成功”不等于“MAVLink 双向链路可用”
- 先确认最可靠链路，再排环境问题

## 推荐顺序

1. 先确认 Windows 是否出现 `ArduPilot` 的 `COMx`
2. 用 Windows 侧 MissionPlanner、MAVProxy 或 pymavlink 验证：
- `HEARTBEAT`
- 关键消息类型
- 参数是否可读
3. 仅在需要代码与工具都在 WSL2 时，再走 TCP 桥接
4. 不把 WSL2 `/dev/ttyACM*` 是否稳定当成固件唯一判据

## 成功判据

- 有 `HEARTBEAT`
- 有 `ATTITUDE`、`RAW_IMU`、`SCALED_PRESSURE` 等关键消息
- 参数读取至少形成可用链路
- `STATUSTEXT` 能反映系统状态

## 常见误判

- 看到 USB 枚举就判定 MAVLink 已完全正常
- 把 WSL2 串口异常直接归因于固件
- 忽略飞控可能还停在 GDB halt 状态
