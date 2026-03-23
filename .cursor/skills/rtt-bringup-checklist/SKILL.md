---
name: rtt-bringup-checklist
description: 按固定顺序执行 AP_HAL_RTT 新板 bring-up，适用于用户提到新板移植、AT32、GD32、第二块板、从零带起、boot 到 main、USB、SPI 或传感器探测时。
---

# RTT Bring-up Checklist

## 固定顺序

1. Flash 布局与 bootloader 跳转
2. `VTOR` / `MSP` / `PSP` / 时钟 / `SystemInit`
3. RT-Thread scheduler 和 `main -> hal.run()`
4. UART / USB CDC 基础链路
5. SPI / I2C 设备表与 probe
6. MAVLink / 参数
7. 持久化 / PWM / RCInput / AnalogIn

## 原则

- 未过上一层，不提前深挖下一层
- 每过一个里程碑，就把结论提炼进 `status.md`
- 涉及外设、总线、子系统时，优先对应到 `driver-validation` 门禁，而不是直接跳进整机行为
- 若问题只影响环境观测，不要误判为固件根因

## 最小通过线

- 能进入 `hal.run()`
- 至少一条可靠通信链路可用
- 至少一组核心传感器数据可用

## 不要做

- 在 boot 还不稳定时就先重构架构
- 在无稳定通信链路前就判断上层行为
- 同时追多个底层假设

## 与逐驱动验证的关系

- `UART / USB CDC`：优先看 `uart-smoke` / `usb-cdc-smoke` 这类门禁
- `SPI / IMU / Baro`：优先看 `spi-ms5611-smoke` / `imu-whoami-smoke`
- `Storage / PWM / ADC`：在整机依赖它们之前，先建立独立 example 或 smoke
