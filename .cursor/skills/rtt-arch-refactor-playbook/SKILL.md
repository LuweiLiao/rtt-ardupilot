---
name: rtt-arch-refactor-playbook
description: 整理 AP_HAL_RTT 架构并向 ChibiOS 风格靠拢，适用于用户提到架构清理、hwdef 数据驱动、多板支持、HAL/BSP 分层、SPIDeviceManager 去硬编码或新板模板时。
---

# RTT Architecture Refactor Playbook

## 目标边界

- `hwdef.dat`：描述板级硬件事实
- `rtt_hwdef.py`：生成设备表、probe 列表和编译宏
- `rtt_bsp_<board>`：承担时钟、DMA、cache、IRQ、底层驱动注册
- `AP_HAL_RTT`：保持板级无关，尽量对齐 `AP_HAL_ChibiOS` 语义

## 重构顺序

1. 先画清当前边界，再定义目标边界
2. 先去除最明显的板级硬编码
3. 再把可生成的配置迁到 `hwdef.dat` / `rtt_hwdef.py`
4. 每次只改一层，并验证 `CUAV v5` 基线不回退

## 高优先级关注

- SPI 设备表
- probe 列表
- UART / SERIAL 顺序
- 板级 define
- BSP 设备 attach

## 禁止

- 为了整理结构破坏当前可运行基线
- 把本应位于 BSP 的特例继续塞进 HAL
- 在没有明确迁移收益时重写整个子系统
