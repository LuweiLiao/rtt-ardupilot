---
name: rtt-driver-validation
description: 对 AP_HAL_RTT 做逐驱动、逐总线、逐子系统验证，适用于用户提到 examples、tests、驱动验证、smoke、UART、USB CDC、SPI、IMU、Baro、Storage、PWM、ADC 或“不要总靠整机 bring-up 验证”时。
---

# RTT Driver Validation

正式方法论以 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 为准；本 Skill 只负责入口、分层顺序和使用约束。当前登记状态以 `.cursor/project/driver-validation-matrix.md` 为准。

> **何时优先回到本分层验证（反固着）**：当在某个上层（如 CDC 背靠背/整机）反复修不好、越改越糟时，按 `rtt-systemic-escalation` 的规则**自底向上重验**——先确认 flash/SD/SPI/I2C 地基模块单独 `RESULT: PASS`，再 CDC，再 CDC+MAVLink，最后才碰压力项；地基未绿不碰上层。

## 作用

把复杂项目从“整机是否能跑”拆成“驱动、总线、子系统是否逐层成立”。

## 验证层级

六层模型（执行顺序）见 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md`：

1. **Host/static (`H*`)** — hwdef、设备表等
2. **STM32 internal (`L*`)** — 寄存器 bring-up / USB 栈
3. **HAL abstract (`D*`)** — `UARTDriver`、`SPIDevice` 等
4. **External module (`E*`)** — IMU、Baro、FRAM、SD 卡等
5. **Subsystem smoke (`S*`)** — 多驱动链
6. **Full vehicle** — ArduCopter L0

旧四段金字塔仍适用；`L*`/`D*`/`E*` 是对 “Board examples” 的拆分。

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
