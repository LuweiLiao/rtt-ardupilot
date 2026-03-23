# Open Issues

## Issue: EKF3 内存压力
- 级别：中
- 现象：实机链路出现 `EKF3 not enough memory`，当前允许回退到 `DCM active`
- 当前判断：`NavEKF3_core` 体积、堆裕量与 `HAL_WITH_EKF_DOUBLE` 相关
- 已排除：不是主线程未运行、不是 IMU/Baro 未初始化
- 下一步：统计 RAM 占用来源，确认是否需要继续裁剪或维持单精度 EKF 基线

## Issue: 持久化 / PWM / RC / ADC 仍不完整
- 级别：高
- 现象：`Storage` 为 RAM stub；`RCOutput`、`RCInput`、`AnalogIn` 尚不足以支持实体飞行
- 当前判断：bring-up 已完成，但飞行相关外围能力仍缺口明显
- 已排除：不是 `CUAV v5` 主链路启动问题
- 下一步：优先评估 `Storage` 持久化与 `PWM` 输出，决定后续飞行相关补齐顺序

## Issue: HAL / BSP / hwdef 仍有硬编码耦合
- 级别：高
- 现象：部分板级事实仍散落在 HAL、BSP、构建逻辑与文档中
- 当前判断：当前结构还未完全达到 `ChibiOS` 式"`hwdef.dat` 驱动 + HAL 板级无关"
- 已排除：不是功能 bring-up 阻塞问题，而是结构治理问题
- 下一步：优先建立治理规则、记忆文件与 Skill，再逐步推动数据驱动化

## Issue: 缺少逐驱动验证门禁
- 级别：中
- 现象：当前很多能力仍只能通过整机 bring-up 或 MAVLink 现象间接判断
- 当前判断：需要把 `driver-validation` 层变成 examples/tests 与文档并行的稳定门禁
- 已排除：不是当前 `CUAV v5` 基线不可运行，而是验证粒度不足
- 下一步：先落文档、矩阵与 Skill，再逐步把首批 `scheduler/uart/usb-cdc/spi/imu/storage` 变成可执行门禁

## Issue: WSL2 USB CDC 结果容易误导
- 级别：低
- 现象：WSL2 `usbipd` 下的双向 CDC 结果不稳定，容易被误判为固件问题
- 当前判断：环境层与固件层必须显式分离
- 已排除：Windows 侧 MAVLink 验证已形成更可信链路
- 下一步：把"Windows 优先验证、WSL2 仅作辅助"的策略固化进 Skill 与状态文档

## ~~Issue: 主循环 400Hz 相关优化仍需收敛~~ [已关闭 2026-03-23]
- **结论**：根因为 `delay_microseconds()` 的 busy-wait 在同优先级（RT_MAIN_THREAD_PRIORITY=10 = DeviceBus fallback=10）round-robin 调度下阻塞 timeslice。修复为统一使用 `rt_thread_delay()`。实测 ~410Hz，参数 944/943 全量下载。详见 `agent-trace.md`。
