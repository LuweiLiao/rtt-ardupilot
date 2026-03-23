# Milestone Plan

## 当前里程碑视图

| ID | 里程碑 | 状态 | 完成判据 |
|---|---|---|---|
| `M0` | 构建与烧录链建立 | 已完成 | 可生成固件、可烧录、可连 OpenOCD/GDB |
| `M1` | `boot -> scheduler -> main` 主链跑通 | 已完成 | `bootloader -> app -> scheduler -> hal.run()` 稳定成立 |
| `M2` | USB CDC / MAVLink 基础通信打通 | 已完成 | Windows 侧出现 `ArduPilot` 串口并可见 `HEARTBEAT` |
| `M3` | 传感器链打通 | 已完成 | 至少 `IMU` 与 `Baro` 数据有效 |
| `M4` | 参数与状态消息形成可用基线 | 已完成 | 944/943 参数全量下载，关键消息类型齐备 |
| `M5` | `CUAV v5` 稳定开发基线 | 已完成 | 主循环 ~410Hz 稳定，MAVLink 参数全量，setup ~10s |
| `M6` | Cursor 治理系统 + 架构清理 Phase 1 | 进行中 | 规则、Skill、项目记忆层、Git 机制落地；建立 HAL/BSP/hwdef 分层边界 |
| `M7` | 数据驱动化与第二块板模板 | 待开始 | `hwdef.dat` 生成路径更完整，出现第二块板模板或迁移样板 |

## 当前所处阶段

- 当前主线位于 `M6`
- `M4`/`M5` 在 2026-03-23 随主循环 400Hz 修复和 MAVLink 参数全量验证一并达成
- 目标不是重新证明 `CUAV v5` 能运行，而是把现有成果变成可持续治理、可多板演进的工程系统

## M6 关注点

- 建立 bootstrap 规则和项目记忆协议 ✓
- 固化 `status / open-issues / decision-log / command-catalog / board-matrix` ✓
- 建立 RTT 专项 Skill ✓
- 建立 `checkpoint / fix / milestone` 与分支 / PR 模板 ✓
- 把 `CUAV v5` 当前基线写成后续工作的事实起点 ✓
- 建立 `driver-validation` 文档层与矩阵 ✓
- **待完成**：实际 Git commit 里程碑记录，项目记忆文件与代码一同入库

## 升级到下一里程碑前需要确认

- `AP_HAL_RTT` 的 HAL / BSP / hwdef 边界明确
- 关键命令、验证路径、设计决策不再依赖长聊天历史
- 至少有一个可以复用到新板 bring-up 的结构模板
- 逐驱动验证的文档、矩阵、Skill 与 examples/tests 规划已经接入当前治理系统
