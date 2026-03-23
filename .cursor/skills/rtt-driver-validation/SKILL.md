---
name: rtt-driver-validation
description: 对 AP_HAL_RTT 做逐驱动、逐总线、逐子系统验证，适用于用户提到 examples、tests、驱动验证、smoke、UART、USB CDC、SPI、IMU、Baro、Storage、PWM、ADC 或“不要总靠整机 bring-up 验证”时。
---

# RTT Driver Validation

正式方法论以 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 为准；本 Skill 只负责入口、分层顺序和使用约束。当前登记状态以 `.cursor/project/driver-validation-matrix.md` 为准。

## 作用

把复杂项目从“整机是否能跑”拆成“驱动、总线、子系统是否逐层成立”。

## 验证层级

1. Host tests：纯逻辑、无需真实硬件
2. Board examples：单驱动或单总线最小验证
3. Subsystem smoke：多驱动协同验证
4. Full vehicle milestone：整机里程碑

## 当前首批对象

- `scheduler-smoke`
- `uart-smoke`
- `usb-cdc-smoke`
- `spi-ms5611-smoke`
- `imu-whoami-smoke`
- `storage-smoke`
- `pwm-output-smoke`
- `analogin-smoke`

## 使用顺序

1. 先确认问题属于哪一层
2. 若已有 driver-validation 门禁，先在该层验证
3. 该层通过后，再继续上卷到 subsystem 或整机
4. 若该层还没有门禁，先在 `driver-validation-matrix` 中登记

## 事实源

- 设计与原则：`docs/AP_HAL_RTT_DRIVER_VALIDATION.md`
- 当前矩阵：`.cursor/project/driver-validation-matrix.md`
- 当前系统事实：`.cursor/project/status.md`
- 当前问题：`.cursor/project/open-issues.md`

## 禁止

- 默认直接依赖整机现象来判断底层驱动是否正确
- 将 driver-validation 文档写成当前事实或 TODO 列表
- 在没有分层验证的情况下同时深挖多个子系统
