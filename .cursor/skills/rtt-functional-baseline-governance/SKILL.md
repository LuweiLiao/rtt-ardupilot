---
name: rtt-functional-baseline-governance
description: >-
  AP_HAL_RTT 功能基线与性能优化分轨治理：锁定 honest functional baseline、归档 known performance gap、
  ChibiOS 对照与 SPI LLD 激活证据链、param/MAVLink 瓶颈归因、T4/MAVFTP 与 SD 挂载边界、
  HAL 以上改动门禁、Planner/composer 分工与 clean branch 纪律。适用于用户提到功能基线、
  clean baseline、性能优化、ChibiOS 对照、SPI LLD、param 速度、T4/MAVFTP、
  避免未测量先修、避免把优化当 correctness blocker 时。
---

# RTT 功能基线治理（Functional Baseline Governance）

## Quick Trigger

在以下任一情况**先读本 skill**，再决定改代码还是归档：

- 功能门禁已通过，但有人提议「再优化一下 SPI / param / USB」
- 文档或口头宣称「LLD 已激活」「接近 ChibiOS」「SPI 吃满 CPU」
- T4/MAVFTP/参数下载慢，怀疑 HAL 以下或 stat shim
- 准备改 `GCS_*` / `AP_Param` / `AP_InertialSensor` / `AP_Filesystem`
- 准备激活 `drv_spi_lld`、生成 `BSP_USING_SPIn`、或动 CMSIS bypass
- 父代理/composer 分工、clean 分支、里程碑前文档校正

**与其它 skill 关系**

| Skill | 本 skill 补什么 |
|-------|----------------|
| `rtt-systemic-escalation` | 单点固着 ≥3 次 → 升维；本 skill 定「该不该动」与分轨 |
| `rtt-agent-orchestration` | 父规划 / composer 执行；本 skill 定补丁队列与 HAL 边界 |
| `rtt-driver-validation` | 怎么跑分层 test；本 skill 定证据等级与禁止宣称 |
| `rtt-build-flash-debug` | 命令手册；本 skill 不重复 |

---

## Core Rules

### R1 — 功能正确性 vs 性能优化分轨

| 轨道 | 定义 | 通过后的动作 |
|------|------|--------------|
| **功能基线** | IMU/EKF/ATTITUDE、MAVFTP 6/6、param 全量可读、CFSR/HFSR=0、CDC 稳 | **锁定** honest baseline；文档如实描述实际路径 |
| **性能 gap** | 同功能下更慢、cpu_idle 低、吞吐差 | **归档**为 known gap；**不阻塞**功能里程碑 |

**硬规则**：功能门禁已通过时，不得因 perf gap 阻断 milestone，除非用户明确把 perf 升为 blocker。

### R2 — 证据先于宣称

没有下列之一，**不得**宣称某 LLD/驱动/优化路径「已激活」或「已生效」：

1. **GDB/符号**：如 `spi_lld_lookup(SPI1)` 非 NULL、`init_count`/`xfer_count` 增长
2. **计数器/探针**：如 `spi1_xfer_calls`、LLD stats、USB fail_streak
3. **实机 gate 脚本输出**：带时间戳、可复现命令

推测只进 `agent-trace` / `open-issues`，**不进** `status.md`。

### R3 — 数据可以否决优化

测量结果**优先于**设计意图。若数据证明瓶颈不在拟修层，**停止该优化线**，写入 decision-log / open-issues 归档。

### R4 — 上层默认禁止改

`GCS_*`、`AP_Param`、`MAVLink`、`AP_InertialSensor`、`AP_Filesystem` 等 **HAL 以上**默认 **禁止**修改。

若要改，必须先交付（父代理或简报中）：

1. **Portability justification** — 为何 RTT 特有问题、ChibiOS 是否同路径
2. **ChibiOS 对照** — 同语义下的参考行为
3. **最小 diff** — 单假设、可 revert
4. **独立测试** — 不依赖整机 soak 才能判 PASS/FAIL

### R5 — Git / 基线纪律

- **禁止** force push、reset 已 push 历史（除非用户书面授权）
- **clean branch** 不得含 `.cursor/` 日志、`build/` 产物、临时测量 json（除非用户明确要求保留）
- **submodule 指针**须可复现；文档引用的 commit 须存在
- 文档必须如实标注 **休眠 / 未激活** 代码，不得写「gate-passed」而实机未注册

---

## Known Facts From CUAV V5 Baseline（2026-05-30）

> 以下为本仓已验证事实；后人勿重推无效结论。

### SPI LLD 休眠，生产走 CMSIS bypass

| 项 | 事实 |
|----|------|
| 注册守卫 | `rt_board_init.c`：`#if defined(BSP_USING_SPI1) && …` — **`BSP_USING_SPIn` 从未生成** |
| 生成链 | `.config` 中 `CONFIG_BSP_USING_SPI*` 注释禁用；`rtt_hwdef.py` 只出 `RT_USING_SPIn` + DMA 宏 |
| 实机 | `spi_lld_lookup(SPI1)=NULL`，`init_count=0`；SPI1/2/4 的 `spi_lld_register` **全未执行** |
| 生产路径 | `SPIDevice.cpp` CMSIS bypass（`_dev=nullptr`），register poll + 大传输 DMA 忙等 |
| 功能 | RAW_IMU、EKF、MAVFTP 6/6、param 912、CFSR=0 — **在 CMSIS 路径上通过** |

**无效历史结论（禁止再写）**：

- 「full SPI LLD 完成 / Floor C/D gate-passed」
- 「cpu_idle 99% **来自** LLD」
- 「已接近 ChibiOS SPI 语义」（无 A/B 测量不得写）

**激活 LLD 的前置风险**（未测量不得 toggle）：

- 生成 `BSP_USING_SPIn` → RT-Thread `drv_spi.c` 实例化总线 → 可能与 CMSIS bypass **双初始化冲突**
- B1 尝试（71b7cb40c2）已 revert（8cc2a1ef0a）

### Param 慢 — 瓶颈不在 SPI

| 测量 | 结果 |
|------|------|
| 空载 | `cpu_idle≈99%`，`spi1_xfer_calls≈1.8k/s`（IMU 轮询在跑，CPU 几乎空闲） |
| param | 912 个一轮 85s vs 30s（同量方差大） |
| 裁决 | 优先级：**(1) GCS_Param/MAVLink 调度** > **(2) USB CDC TX 背压** >> SPI |
| B1 | **数据否决** — SPI1 LLD 激活对当前无收益，已归档 |

**诚实局限**：负载期 cpu_idle 曾受 OpenOCD/CDC 互斥未能在线采；结论基于空载 idle + 耗时方差，非负载期直接读数。

### T4 / MAVFTP 与 SD 挂载

| 项 | 事实 |
|----|------|
| SD 已挂载 | T4 Create/Write/Read/Delete **通过** |
| 历史失败 | SD **未挂载** → `/APM` 缺失；**不是** stat shim 缺陷 |
| 禁止 | 为 T4 **盲改** `safe_stat` / `GCS_FTP` / FRAM 路径 |

### Clean 功能基线锚点

- 分支：`clean/rtt-spi-full-lld`（诚实 SPI LLD 状态文档）
- 设计文档：`docs/rtt-porting/SPI_LLD_DESIGN.md` 顶部 **STATUS CORRECTION (2026-05-30)** 为准（doc 校正 commit `301431c332`）
- 本 skill：功能/性能分轨治理与禁止宣称清单；**不替代** `status.md` / `open-issues.md`

---

## Decision Workflow

每个补丁提议前，**必须**过四问（见 § 决策模板）。流程：

```
用户/简报目标
  → Q1: correctness blocker 还是 performance gap?
       ├─ blocker → 功能轨道：最小 diff、单模块、双重验证
       └─ gap     → 需测量 + 用户批准；默认归档，不挡 milestone
  → Q2: 有没有测量证明瓶颈在拟修层?
       ├─ 无 → 只读调查 / 加探针；禁止上板大改
       └─ 有 → 记录命令与 raw 输出到 trace
  → Q3: 补丁失败如何回退?（单 commit / git checkout 路径）
  → Q4: 对全局里程碑收益?（L0/L1/L2 哪一层）
  → 若触 HAL 以上 → R4 四件套；否则 BLOCKED 待批准
  → 派发 composer（硬件串行）或并行只读调查
  → 验收：证据等级 ≥ 宣称等级
  → 稳定事实 → status；gap → open-issues；取舍 → decision-log
```

### 决策模板（四问）

1. **这是 correctness blocker 还是 performance gap？**
2. **有没有测量证明瓶颈在这一层？**（GDB/计数器/脚本，非 grep 推断）
3. **如果补丁失败，如何一步回退？**
4. **对当前全局里程碑（bring-up / L0 / L1）收益是什么？**

四问无书面答案 → **不实施补丁**。

---

## Patch Queue Rules

1. **单假设单 commit 意图**：一次子任务只验证一条因果链（与仓库「单次修复可检验」一致）
2. **先只读后上板**：SPI LLD 激活、BSP 宏、HAL 以上 — 必须先只读查清冲突面
3. **数据否决即停**：如 B1 SPI1 LLD — revert 后归档，不排队「再试一次 DMA 宏」
4. **perf 补丁排队条件**：功能 baseline 已锁 **且** 用户批准 **或** M7 ChibiOS A/B 证明该层为瓶颈
5. **文档与代码同步**：改激活状态必须同步 `SPI_LLD_DESIGN.md` / `open-issues` / `status`

---

## Hardware Gate Rules

| 层级 | 判据 | 与 perf 关系 |
|------|------|--------------|
| L0 | CDC 心跳、STANDBY、CFSR=0、30s 流 | 功能 blocker |
| L1 | RAW_IMU、ATTITUDE、SYS_STATUS | 功能 blocker |
| MAVFTP | 单轮 6/6（milestone 基线） | 功能 blocker |
| param 全量 | 904/912 可读 | 功能 blocker |
| param 耗时 / cpu_idle | 方差、idle% | **perf gap**，非 blocker |
| SPI LLD stats | lookup/init/xfer | **激活证据**，非「编译过」 |

**互斥**：OpenOCD 与 CDC 验证串行；测量瓶颈时记录「未能采负载 idle」等局限。

---

## Prohibited Claims

| 禁止宣称 | 所需证据 |
|----------|----------|
| SPI LLD 已激活 / IMU 走 drv_spi_lld | `spi_lld_lookup` 非 NULL + xfer_count 增 |
| cpu_idle 改善来自 LLD | 同场景 A/B，LLD on/off 对照 |
| 接近 ChibiOS 性能/语义 | ChibiOS 同板同脚本 A/B 数据 |
| param 慢因 SPI 吃 CPU | 负载/空载 idle + xfer 率 + 排除 GCS 方差 |
| T4 失败因 stat shim | SD 挂载状态 + `/APM` 存在性 |
| 「gate-passed」仅编译/link | 实机 probe 或分层 test PASS |

---

## Prohibited Edits（默认）

| 区域 | 默认 | 例外 |
|------|------|------|
| Bootloader / IO firmware | 禁止 | 用户明确 |
| `GCS_*` / `AP_Param` / MAVLink 调度 | 禁止 | R4 四件套 + 用户批准（ISSUE-02） |
| `AP_InertialSensor` 事务形态 | 禁止 | Fix#4 规则：SPI 问题不得改 INS 事务 |
| `safe_stat` / `GCS_FTP` 为 T4 | 禁止盲改 | 证明非 SD 挂载问题 |
| 无测量激活 `BSP_USING_SPIn` | 禁止 | 只读冲突分析 + 单变量 gate |
| 为 perf 同时改多模块 | 禁止 | — |

**本 skill 执行者**：若任务仅为写 skill/文档，**禁止**改固件源码、构建脚本、子模块。

---

## Planner / Composer 分工

| 角色 | 职责 |
|------|------|
| **父代理（Planner）** | bootstrap、四问裁决、补丁队列、写简报、只读验收、更新 project 记忆 |
| **composer-2.5（行动者）** | 按简报改代码/跑构建烧录/记 trace（若简报允许） |

**并行**：只读调查、文档、矩阵核对、符号检索 — 可多 agent。  
**串行**：ST-Link、OpenOCD、CDC、MAVFTP、param soak — **独占硬件**。

详见 `rtt-agent-orchestration`。

---

## Handoff Template

父代理 → composer 简报应包含：

```markdown
## 子任务：[标题]

### 轨道
- [ ] 功能 correctness  /  [ ] 性能 gap（已用户批准：是/否）

### 四问（必填）
1. Blocker or gap? →
2. 测量证据（命令+输出路径）→
3. 回退：`git checkout <rev> -- <paths>` 或 revert commit →
4. 里程碑收益 →

### 范围
- 允许修改：
- 禁止修改：

### 完成判据
1. …

### 回报
- STATUS: DONE | BLOCKED
- 是否触碰 HAL 以上：是/否 + 四件套链接
- Verification: …
- 若仅归档 gap：更新 open-issues 条目
```

行动者回报父代理时，**必须**标明：宣称等级（1–5 证据）、是否 contradict Known Facts 节。

---

## 验收清单（功能基线锁定）

- [ ] `status.md` 与实机路径一致（CMSIS vs LLD 无夸大）
- [ ] `SPI_LLD_DESIGN.md` STATUS CORRECTION 仍在且准确
- [ ] open-issues 中 perf 项标为 **known gap**，非 open blocker
- [ ] ISSUE-02（param/GCS）未擅自实施
- [ ] clean branch 无 build/log 垃圾
- [ ] 无禁止宣称出现在 commit message / PR 描述

---

## 禁止误用

- 用本 skill 代替 `rtt-build-flash-debug` 跑具体命令
- 以「治理」为名跳过四问直接改 HAL 以上
- 把 archived perf gap 重新标成 blocker 而无新测量
