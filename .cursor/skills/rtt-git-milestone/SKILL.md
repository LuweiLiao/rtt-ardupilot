---
name: rtt-git-milestone
description: 管理 AP_HAL_RTT 的 checkpoint、fix、milestone、分支说明和 PR 说明，适用于用户提到提交、里程碑、基线、tag、分支、PR 或“把当前阶段记录到 Git”时。
---

# RTT Git Milestone

长文权威说明见 `.cursor/project/git-process.md`；本 Skill 只保留入口与检查清单。

## 提交层级

- `checkpoint`：中间保存，防止长任务丢进度
- `fix`：单个问题或局部能力修复
- `milestone`：阶段性稳定基线

## milestone 前检查

先同步：
- `.cursor/project/status.md`
- `.cursor/project/open-issues.md`
- `.cursor/project/decision-log.md`
- `.cursor/project/command-catalog.md`
- `.cursor/project/milestone-plan.md`
- `.cursor/project/driver-validation-matrix.md`

## commit 说明重点

- 为什么这是一个阶段基线
- 现在什么已成立
- 已知限制是什么
- 如何验证
- 下一阶段是什么
- 若适用，写明通过了哪些 driver examples / tests，哪些能力仍只有整机层验证

## 分支 / PR 说明

至少交代：
- 当前目标
- 不做什么
- 完成判据
- 回退点

## 注意

- 未经用户明确要求，不主动提交、amend 或 push
- milestone 不等于“很多文件改了”，而是“边界清晰、可作为回退点”
