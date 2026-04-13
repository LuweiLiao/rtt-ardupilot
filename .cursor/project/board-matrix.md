# Board Matrix

| Board | SoC | BSP | hwdef | 启动 | MAVLink | 传感器 | PWM | SD 卡 | 参数 | 备注 |
|---|---|---|---|---|---|---|---|---|---|---|
| `cuav_v5` | `STM32F767` | `libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/` | `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` | 已跑通 | 已验证 (96.6 msgs/s) | BMI055/MS5611/IST8310 全通 | TIM1/4/12 8ch 已初始化，待实机验证 | 非阻塞挂载，SDMMC2 硬件层无响应 | 941 params 已验证 | 当前稳定开发基线 |
| `gd32_*` | TBD | TBD | TBD | 未开始 | 未开始 | 未开始 | 未开始 | 未开始 | 未开始 | 后续国产 MCU 扩展方向 |
| `at32_*` | TBD | TBD | TBD | 未开始 | 未开始 | 未开始 | 未开始 | 未开始 | 未开始 | 后续国产 MCU 扩展方向 |
