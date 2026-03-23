# Current Focus

## 当前阶段
`CUAV v5` 的 RT-Thread ArduPilot 可运行基线已经建立，当前进入“治理系统搭建 + 架构清理”阶段。

## 当前主线目标
- 建立可持续运作的 Cursor 治理系统
- 在不破坏 `CUAV v5` 基线的前提下，推动 `AP_HAL_RTT` 向 `hwdef.dat` 驱动、多板可扩展的结构演进

## 当前优先级
1. 固化项目记忆文件、Skill、Git 里程碑机制
2. 清理 HAL / BSP / hwdef 的职责边界
3. 为后续 `GD32` / `AT32` 等新板迁移建立模板

## 暂不处理
- WSL2 `usbipd` 的兼容性细节
- 非关键调试变量的风格统一
- 在未建立结构基线前直接启动第二块板 bring-up

## 推荐起手动作
先读 `status.md` 与 `open-issues.md`，确认当前稳定基线与未关闭问题，再决定是做 bring-up、调试、架构整理还是 Git 里程碑。
