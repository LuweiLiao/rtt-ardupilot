# Decision Log

## 2026-03-23: 将项目记忆拆成多文件，而非继续堆到单一 trace
- 原因：长对话和单一 `agent-trace` 容易造成上下文膨胀，也容易把“中途现象”误当成“稳定事实”
- 决定：把当前状态、未关闭问题、设计决策、命令速查、里程碑计划分别拆到 `project/` 下
- 放弃方案：继续依赖长聊天导出或单文件 trace 做全部恢复

## 2026-03-23: Windows 主机验证优先于 WSL2 usbipd 结果
- 原因：WSL2 `usbipd` 下的 USB CDC 双向通信不稳定，容易把环境问题误判为固件问题
- 决定：USB CDC / MAVLink 的正确性以 Windows 主机侧验证为主，WSL2 结果只作辅助观察
- 放弃方案：把 WSL2 `/dev/ttyACM*` 作为唯一地面真相源

## 2026-03-23: 当前基线保持 `HAL_WITH_RAMTRON 0`
- 原因：FRAM 后端尚未形成可靠初始化与持久化闭环，过早开启会让 `Storage` 路径不稳定
- 决定：在当前稳定基线中继续把 RAMTRON 关闭，待后端成熟后再升级
- 放弃方案：在未验证后端完整性的情况下直接把 FRAM 当成默认持久化

## 2026-03-23: 当前基线允许 `HAL_WITH_EKF_DOUBLE 0`
- 原因：当前 RAM 裕量下，双精度 EKF 容易导致 `EKF3 not enough memory`
- 决定：先把“系统稳定运行 + 传感器/MAVLink 可用”作为更高优先级，允许单精度 EKF 作为当前基线
- 放弃方案：在尚未解决内存问题时强行坚持双精度 EKF

## 2026-03-23: 结构目标对齐 ChibiOS，但不复制板级硬编码
- 原因：未来目标是支持更多国产芯片，而不是仅把 `CUAV v5` 特解写得更深
- 决定：把 `hwdef.dat` 作为板级真相源，推动 `rtt_hwdef.py`、设备表、probe 列表、SPI attach 等走数据驱动路径
- 放弃方案：继续在 HAL 层和 `SPIDeviceManager` 中累积板级字符串和硬编码映射

## 2026-03-23: bootstrap 以 `project/*.md` 为唯一主入口
- 原因：若 `project/*.md` 与专项档案并列为两个入口，新 agent 会在新会话和对齐任务中出现“先读哪套状态”的歧义
- 决定：统一采用 `current-focus -> status -> open-issues` 作为主 bootstrap；`.cursor/alignment-status.md` 与 `.cursor/alignment-issues.md` 仅作为 `AP_HAL_RTT` 对齐任务的专项补充档案
- 放弃方案：继续让 `rtt-chibios-alignment` 维持独立于主治理系统之外的第二套入口

## 2026-03-23: Git 与 driver-validation 采用单一事实源
- 原因：同一主题若同时在 project 文档、Skill、命令目录中展开完整说明，容易产生漂移和重复阅读
- 决定：Git 流程以 `project/git-process.md` 为长文权威，`rtt-git-milestone` 只保留入口与检查清单；driver-validation 以 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 为正式方法论，矩阵管状态，Skill 管入口
- 放弃方案：让多个文件分别维护同一主题的完整长文版本

## 2026-03-23: delay_microseconds 必须使用 rt_thread_delay，禁止 busy-wait
- 原因：RT-Thread main 线程（prio=10）与 DeviceBus 回调线程（prio=10）同优先级，busy-wait 不会让出 timeslice。主线程在 `wait_for_sample()` 中 busy-wait 100µs 导致 DeviceBus 回调无法完成 SPI 读 IMU → `_have_sample()` 永不返回 → 主循环在 ~195 次后卡死
- 决定：`delay_microseconds()` 统一使用 `rt_thread_delay(ticks)` 实现所有延迟；不使用任何分支的 busy-wait
- 放弃方案：(1) 为短延迟（≤100µs）保留 busy-wait 分支 — 会在同优先级下导致 round-robin 饥饿；(2) 通过调整 DeviceBus 或主线程优先级绕过 — 反复尝试 BOOST/MAIN/SPI_BUS 等多种组合均未解决根因；(3) DWT busy-wait + yield — 无法保证 yield 后 bus 线程得到足够时间
- 实测验证：修复后主循环 ~410Hz（4094 iters/10s），参数 944/943 全量下载
