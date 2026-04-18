# DShot/PWM Output Investigation — RTT CUAV V5

**Date:** 2026-04-19
**Board:** CUAV V5
**HAL:** AP_HAL_RTT

## 1. hwdef.dat PWM 配置对比

### RTT hwdef (cuav_v5)

```
PI0  TIM5_CH4  TIM5  PWM(1)  GPIO(50)  BIDIR
PH11 TIM5_CH2  TIM5  PWM(3)  GPIO(52)  BIDIR
PD13 TIM4_CH2  TIM4  PWM(5)  GPIO(54)
PD14 TIM4_CH3  TIM4  PWM(6)  GPIO(55)
PE11 TIM1_CH2  TIM1  PWM(7)  GPIO(56)
PI5  TIM8_CH1  TIM8  PWM(11) GPIO(60)
PE6  TIM15_CH2 TIM15 PWM(12) GPIO(61)
PH6  TIM12_CH1 TIM12 PWM(13) GPIO(62)  NODMA
PH9  TIM12_CH2 TIM12 PWM(14) GPIO(63)  NODMA
```

**问题：RTT hwdef 的引脚映射与 ChibiOS fmuv5 完全不同！**

### ChibiOS hwdef (fmuv5) — 标准参考

```
PE14 TIM1_CH4 TIM1 PWM(1) GPIO(50)
PA10 TIM1_CH3 TIM1 PWM(2) GPIO(51)
PE11 TIM1_CH2 TIM1 PWM(3) GPIO(52)
PE9  TIM1_CH1 TIM1 PWM(4) GPIO(53)
PD13 TIM4_CH2 TIM4 PWM(5) GPIO(54)
PD14 TIM4_CH3 TIM4 PWM(6) GPIO(55)
PH6  TIM12_CH1 TIM12 PWM(7) GPIO(56) NODMA
PH9  TIM12_CH2 TIM12 PWM(8) GPIO(57) NODMA
```

### 差异分析

| 项目 | ChibiOS fmuv5 | RTT cuav_v5 |
|------|--------------|-------------|
| CH1 | PE14 TIM1_CH4 | **PI0 TIM5_CH4** |
| CH2 | PA10 TIM1_CH3 | *(缺失)* |
| CH3 | PE11 TIM1_CH2 | **PH11 TIM5_CH2** |
| CH4 | PE9 TIM1_CH1 | *(缺失)* |
| CH5 | PD13 TIM4_CH2 | PD13 TIM4_CH2 ✅ |
| CH6 | PD14 TIM4_CH3 | PD14 TIM4_CH3 ✅ |
| CH7 | PH6 TIM12_CH1 | **PE11 TIM1_CH2** |
| CH8 | PH9 TIM12_CH2 | **PI5 TIM8_CH1** |

⚠️ **RTT hwdef 引脚分配完全错乱**，CH1-4 用了 TIM5（CUAV V5 上 TIM5 主要用于 RC 输入），且缺少 CH2/CH4。额外增加了 PWM(11-14) 的映射。

## 2. RCOutput 驱动实现

RCOutput 驱动存在：`libraries/AP_HAL_RTT/RCOutput.cpp`

**驱动中的映射是正确的（与 ChibiOS 一致）：**
```cpp
{ "pwm1", 4 },   // CH1 → TIM1_CH4 (PE14)
{ "pwm1", 3 },   // CH2 → TIM1_CH3 (PA10)
{ "pwm1", 2 },   // CH3 → TIM1_CH2 (PE11)
{ "pwm1", 1 },   // CH4 → TIM1_CH1 (PE9)
{ "pwm4", 2 },   // CH5 → TIM4_CH2 (PD13)
{ "pwm4", 3 },   // CH6 → TIM4_CH3 (PD14)
{ "pwm12", 1 },  // CH7 → TIM12_CH1 (PH6)
{ "pwm12", 2 },  // CH8 → TIM12_CH2 (PH9)
```

**问题：hwdef.dat 和 RCOutput.cpp 的映射不一致！** hwdef 被 ArduPilot 上层用于 GPIO 初始化和功能分配，RCOutput.cpp 用的是正确的映射，但 hwdef 如果错误会导致引脚被错误初始化。

## 3. DShot 支持

hwdef 中定义了：
```
ROMFS io_firmware_dshot.bin Tools/IO_Firmware/iofirmware_dshot_lowpolh.bin
define HAL_WITH_IO_MCU_DSHOT 1
```

RCOutput.cpp 中 **没有 DShot 实现**，只有纯 PWM (`rt_pwm_set`)。DShot 定义存在但未实际实现。

## 4. MAVLink SERVO_OUTPUT_RAW 实测

```
SERVO_OUTPUT_RAW: servo1-4=0,0,0,0
  servo5-8=0,0,0,0
  port=0
```

**全部输出为 0。** 可能原因：
1. Safety switch 未解除（默认 SAFETY_DISARMED，`write()` 会拒绝输出）
2. RT-Thread PWM 设备未正确注册（`rt_device_find` 返回 nullptr）
3. hwdef 引脚映射错误导致 PWM 外设未正确初始化

## 5. 结论与建议

### 关键问题

1. **hwdef.dat 引脚映射错误** — 必须修正为与 ChibiOS fmuv5 一致
2. **DShot 未实现** — `HAL_WITH_IO_MCU_DSHOT` 已定义但 RCOutput 中无 DShot 协议代码
3. **输出全 0** — 需进一步排查 safety 状态和 RT-Thread PWM 设备注册情况

### 修复优先级

- **P0:** 修正 hwdef.dat 引脚映射 → 对齐 ChibiOS fmuv5
- **P1:** 验证 RT-Thread PWM 设备 "pwm1", "pwm4", "pwm12" 是否注册成功
- **P2:** 实现 DShot 协议支持（如需要）
