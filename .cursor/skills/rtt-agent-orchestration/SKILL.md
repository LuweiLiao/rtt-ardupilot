---
name: rtt-agent-orchestration
description: >-
  AP_HAL_RTT 大任务中父代理作为规划监督者、composer-2.5 作为行动者的编排规范；适用于用户要求父代理禁止执行、
  只派发 composer-2.5、架构治理/驱动验证/构建烧录等场景。
---

# RTT Agent Orchestration（规划监督者 × composer-2.5 行动者）

## 何时使用

- 用户明确要求：**父代理只做规划监督，禁止亲自执行任务**；**composer-2.5 才是行动者**
- 跨多文件/多子系统的大任务：架构整理、驱动矩阵验证、bring-up、构建烧录与双重验证
- 需要并行探索但硬件资源必须串行时
- 父代理需要拆子任务、写简报、只读验收，而不占 ST-Link / CDC / 构建环境

## 角色分工（总览）

| 角色 | 定位 | 典型产出 |
|------|------|----------|
| **父代理（规划监督者）** | 读项目记忆、规划、拆任务、派发、只读验收、更新 status/open-issues | 子任务简报、验收结论、状态文档更新 |
| **composer-2.5（行动者）** | 按简报改代码、构建、烧录、验证、记证据 | diff、命令输出、实机/OpenOCD/MAVLink 结论 |

---

## 父代理职责

1. **Bootstrap**（新会话/新域/长会话）：按 `session-bootstrap` 与 **`rtt-session-resume`** 读 `current-focus`、`status`、`open-issues`；按任务类型补读 command-catalog、driver-validation、alignment 等，**不全文重读**长 trace。
2. **规划**：把用户目标收敛为可验证子目标；标风险与验证方式；难块优先排期。
3. **拆任务**：每个子任务范围清晰、可单轮闭环；能并行则并行（见硬件互斥）。
4. **派发 composer-2.5**：为每个子任务写**子任务简报**（见下文模板）；指定需读取的 skill、完成判据、回报格式。
5. **只读验收**：对照简报判据审阅行动者回报（diff、命令输出、日志要点）；**不亲自跑构建/烧录/调试**。
6. **更新状态文档**：稳定结论 → `status` / `decision-log`；未闭环 → `open-issues`；过程事实 → 要求行动者写入 `agent-trace` 后由父代理提炼。

---

## 父代理禁止（硬约束）

用户要求「严格按照…你是规划监督者…禁止执行任务」时，父代理**不得**：

- **直接修改源码**（含 HAL、BSP、hwdef、脚本、规则、skill 正文——除非用户单独授权父代理只改文档/skill）
- **运行** `scons`、`openocd`、`gdb`、`pymavlink` / MAVLink 验证脚本，或等价构建/烧录/调试命令
- **占用** ST-Link、CMSIS-DAP、`/dev/ttyACM*`、USB CDC 等硬件通道
- **烧录**固件或要求用户代跑本应行动者完成的验证
- **创建 git commit**（除非用户明确点名父代理提交）

父代理若发现判据未满足，应**退回简报**或派发新的 composer-2.5 子任务，而不是自己下场执行。

---

## composer-2.5 行动者职责

1. **按单任务简报**执行：只改简报范围内的文件；遵守简报中的禁止项。
2. **改代码**：最小必要 diff；一次子任务聚焦一个模块/一条假设（与仓库「单次修复可检验」原则一致）。
3. **构建**：`scons` 全量或增量；记录通过/失败与关键错误行。
4. **烧录**：OpenOCD 或 `--upload`；记录入口地址与产物路径。
5. **验证**：按任务要求做 CDC MAVLink、OpenOCD halt/resume、GDB 等；**证据优先**见下文。
6. **记录证据**：每步追加 `.cursor/agent-trace.md`（动作、依据、结果、下一步）。
7. **回传父代理**：diff 摘要、执行的命令、结论性日志片段、STATUS（DONE/BLOCKED）、未决风险。

行动者应主动读取简报指定的 skill（如 **`rtt-build-flash-debug`**、**`rtt-driver-validation`**），勿在无简报时扩大范围。

---

## 硬件与执行互斥

| 资源 | 规则 |
|------|------|
| ST-Link / CMSIS-DAP | 同一时刻仅一个行动者占用；烧录、GDB、OpenOCD 串行 |
| USB CDC / `ttyACM` | MAVLink 验证与烧录后等待窗口串行；勿与另一行动者争用 |
| 实机复位 / 拔插 | 能 `usb unbind/bind` 则优先脚本恢复；避免多代理同时操作 |
| **只读审计** | 源码检索、静态符号、读文档、读 trace — **可并行**，不占硬件 |

父代理派发时须在简报中写明：**是否占硬件**、**前置任务是否释放调试器**。

---

## 多执行者并行编排（提高效率）

用户要求「尽量多开执行者 agent 提高效率」时，父代理按**任务是否占硬件**分流，最大化并行、严守串行：

### 默认执行者：`composer-2.5-fast`

- 派发 `Task` 子代理时显式指定 `model: composer-2.5-fast`（用户点名 fast 变体时）。
- 每个执行者一份独立简报，范围互不重叠（文件/模块/硬件资源）。

### 可并行（同一批次多开）

只要满足「**不占硬件 + 文件不冲突**」即可并行，父代理在一条消息里发多个 `Task`：

- **只读调查**：根因分析、源码审计、符号检索、git status 归类、commit 拆分计划、矩阵核对
- **互不冲突的代码改动**：分属不同模块/目录，且不需要立刻上板（如 M7 的 `Util.cpp` 与测试树清理）
- **纯构建验证**：仅 `scons` 编译产物校验（不烧录/不连 CDC）——但需注意多个并行 `scons` 会争 CPU 与归档竞态（`.o` 归档曾出现 race），并行构建应限 2 个以内或拆增量

### 必须串行（同一时刻仅一个执行者）

凡触及以下资源，**绝不并行**，父代理排队派发，前一个回报释放后再发下一个：

- **ST-Link / OpenOCD / st-flash / GDB**（烧录、halt/resume、寄存器读）
- **USB CDC / `ttyACM*`**（MAVLink、MAVFTP、参数下载、soak、gate 脚本）
- **实机复位 / 拔插**

> 经验：硬件验证类（MAVFTP、L0 gate、FRAM 持久、参数下载）必须独占 CDC，且烧录后留冷启窗口、`param_request_list` 前 drain 5s；多进程争同一 `ttyACM` 会放大 USB 断连，伪装成固件回归。

### 编排节奏（典型）

```
批次1（并行，只读）：根因调查 A ‖ commit 拆分计划 B ‖ 矩阵/文档核对 C
   → 父代理只读汇总，消除矛盾
批次2（串行，占硬件）：单个执行者 build→flash→独占 CDC→验证
   → 验收
批次3（并行，非硬件）：代码卫生清理 ‖ 文档/skill 更新（如授权）
```

父代理在派发并行批次前，必须确认各执行者**不会同时碰 ST-Link/CDC**；若某"非硬件"子任务可能临时需要上板，应改为串行排队。

---

## 子任务简报模板（父代理 → composer-2.5）

```markdown
## 子任务 ID / 标题

### 目标
（一句可验证结果，例如：CUAV V5 编译通过且 CDC 15s 内心跳 + STANDBY）

### 范围
- 允许修改的路径/模块：
- 明确不在此次的范围：

### 禁止项
- 例：不得改 bootloader / IO firmware；不得一次改多模块；不得未验证就宣称修复完成

### 需读取的 Skill / 文档
- [ ] rtt-session-resume（若冷启动）
- [ ] rtt-build-flash-debug / rtt-driver-validation / rtt-arch-refactor-playbook / …
- [ ] `.cursor/project/status.md` 相关段落

### 完成判据
1. …
2. …

### 回报格式
- STATUS: DONE | BLOCKED
- Files changed: …
- Summary: …
- Verification: （命令 + 关键输出/现象）
- Notes/risks: …
```

---

## 与相关 Skill 的关系

| Skill | 父代理 | composer-2.5 |
|-------|--------|----------------|
| **`rtt-session-resume`** | 会话入口必读；恢复阶段与基线 | 子任务开始前若上下文不足则读 |
| **`rtt-arch-refactor-playbook`** | 拆架构子任务、定边界与里程碑 | 执行 hwdef/HAL 分层与数据驱动改动 |
| **`rtt-driver-validation`** | 排驱动矩阵与验证顺序 | 跑 examples/tests、填验证证据 |
| **`rtt-build-flash-debug`** | 不在简报外执行命令 | 编译、烧录、OpenOCD/GDB、WSL usbipd 的操作手册 |

编排 skill **不替代**上述专项 skill：父代理负责「派谁、做什么、何时验收」；行动者负责「按专项 skill 真做」。

---

## 证据优先级（验收与写 status 时）

从高到低，**不得用低层级证据推翻高层级已失败结论**：

1. **实机验证**：CDC MAVLink 心跳、状态机、STATUS_TEXT、长时间稳定运行
2. **OpenOCD / GDB**：halt/resume、PC 范围、HardFault/ESR、线程栈
3. **构建 / 静态符号**：scons 通过、链接 map、关键符号存在
4. **源码审计**：逻辑自洽、与 ChibiOS 语义对齐
5. **推测**：仅作假设，不得写入 `status.md`；只进 `agent-trace` 或 `open-issues`

L0 稳定性（见 `CLAUDE.md`）要求双重验证时，行动者回报须**同时**覆盖 OpenOCD 与 CDC，父代理无硬件执行权时依行动者证据验收。

---

## 状态记录（项目记忆协议）

| 内容类型 | 写入位置 |
|----------|----------|
| 本轮命令、现象、假设否定 | `.cursor/agent-trace.md`（行动者追加） |
| 已多次验证、可回归的稳定事实 | `.cursor/project/status.md` |
| 未关闭问题、下一验证动作 | `.cursor/project/open-issues.md` |
| 设计取舍、放弃方案 | `.cursor/project/decision-log.md` |
| 可复用稳定命令 | `.cursor/project/command-catalog.md` |

父代理在验收后：从 trace **提炼**到 status/open-issues；**不**把长推理堆进 status。

---

## 父代理工作流（简图）

```
用户目标
  → bootstrap（rtt-session-resume）
  → 规划 & 拆子任务
  → 写简报 → 派发 composer-2.5
  → 只读验收回报
  → 更新 status / open-issues / current-focus
  → 未闭环则再派发（不占硬件）
```

---

## 触发语（便于自动选用本 skill）

- 「父代理只规划不执行」「composer-2.5 行动者」「规划监督者」
- 「拆任务给子代理」「不要你自己跑 scons/OpenOCD」
- 大任务：架构治理、驱动验证矩阵、构建烧录验证、多板 bring-up

## 禁止误用

- 单文件小改、用户未要求双角色时 — 不必强行拆父/子；行动者可单代理完成
- 父代理以「先看一眼构建」为由运行 scons — **违反本 skill**
- 行动者未收到简报就扩大修改范围 — 应 BLOCKED 并请求父代理补简报
