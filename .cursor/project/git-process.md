# Git Process

本文件是 Git 流程的长文权威说明；`.cursor/skills/rtt-git-milestone/SKILL.md` 只保留入口与检查清单。

## 提交层级

### `checkpoint`
- 用途：中间保存，防止长任务丢失进度
- 适用：已定位问题、已完成部分工作，但还不是稳定基线
- 说明重点：当前在做什么、为什么先保存

### `fix`
- 用途：单个问题修复或局部能力完成
- 适用：修复同步、驱动、脚本、文档或构建链中的明确问题
- 说明重点：根因、修复点、验证结果

### `milestone`
- 用途：阶段性稳定基线
- 适用：达到新的可运行层级，适合作为后续工作的回退点
- 说明重点：现在什么成立、边界是什么、还剩什么

## 分支命名建议

- `rtt/cuavv5-<topic>`
- `rtt/arch-<topic>`
- `rtt/hwdef-<topic>`
- `rtt/gd32-<topic>`
- `rtt/at32-<topic>`

## milestone 前同步清单

- 更新 `status.md`
- 更新 `open-issues.md`
- 更新 `decision-log.md`
- 更新 `command-catalog.md`
- 更新 `milestone-plan.md`
- 更新 `driver-validation-matrix.md`
- 明确本次 milestone 的验证边界

## PR 说明模板

```md
## Goal
本次要达到什么阶段目标。

## What Is Now True
- 现在已经稳定成立的能力
- 具体到链路、模块或板卡

## Key Changes
- 主要改动点
- 为什么这么做

## Known Limits
- 当前还没解决的问题
- 哪些结论仍是阶段性

## Validation
- 构建
- 调试
- 实机 / 主机侧验证
- 若适用，列出通过了哪些 driver examples / tests，哪些能力仍只有整机层验证

## Next Step
- 下一阶段最值得继续推进的方向
```

## 分支说明模板

```md
目标：<本分支只解决什么>
不做：<本分支明确不处理什么>
完成判据：<什么现象出现后算完成>
回退点：<若失败，回退到哪个 milestone>
```
