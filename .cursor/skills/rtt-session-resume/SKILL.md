---
name: rtt-session-resume
description: 恢复 AP_HAL_RTT 项目上下文，适用于新会话开始、同会话切换到新问题、长会话后重新收敛。使用 project 记忆文件和 agent trace 尾部恢复当前基线，避免全文重读长聊天历史。
---

# RTT Session Resume

## 何时使用

- 新会话开始
- 用户提出新的目标或新的问题域
- 长会话后需要重新收敛
- 需要判断“当前项目到底在哪个阶段”

## 恢复顺序

1. 读 `.cursor/project/current-focus.md`
2. 读 `.cursor/project/status.md`
3. 读 `.cursor/project/open-issues.md`
4. 若任务是 `AP_HAL_RTT` 对齐 `ChibiOS` 的专项任务，再读 `.cursor/alignment-status.md` 与 `.cursor/alignment-issues.md`
5. 若任务涉及命令或调试，再读 `.cursor/project/command-catalog.md`
6. 若任务属于驱动、总线、子系统验证，再读 `.cursor/project/driver-validation-matrix.md` 与 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md`
7. 若任务涉及结构整理、取舍、里程碑或 Git 流程，再读 `.cursor/project/decision-log.md` 与 `.cursor/project/milestone-plan.md`
8. 若任务是续接某个具体排障，只读 `.cursor/agent-trace.md` 最近 20-50 行

## 输出要求

恢复后先用 3-6 句说明：
- 当前阶段
- 当前主线目标
- 当前问题属于哪一层
- 本次读取了哪些文档
- 下一步先做什么

## 禁止

- 默认全文重读长 trace 或长聊天导出
- 不经过恢复流程就直接进入大范围搜索或修改
- 把上个问题的假设直接带进当前新问题
- 首轮恢复默认只读 `current-focus`、`status`、`open-issues` 三个短文档；其余按任务类型补读
