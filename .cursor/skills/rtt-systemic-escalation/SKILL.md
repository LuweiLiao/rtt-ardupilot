---
name: rtt-systemic-escalation
description: >-
  AP_HAL_RTT 调试反固着与升维规则：同一局部点修复 ≥3 次未闭环时，强制转「自底向上模块验证 +
  系统级跨层对照 + 大批并行 agent」。适用于用户提到 别死磕一个点、越改越糟、回到分步验证、
  先验 SD/flash/SPI、派几十个 agent、从全局/系统层面分析，或 CDC/USB 背靠背稳定性等场景。
  内含 CDC/USB 系统级根因知识库，避免后人重推。
---

# RTT 系统级升维与自底向上验证（反固着）

## 何时使用

- 同一个**局部点**已尝试修复 **≥3 次仍未闭环**（典型：CDC 背靠背、某外设时序）
- 出现「单点旋钮来回试、越改越糟、把板子改进更坏状态」的迹象
- 用户说：别死磕一个点 / 你太局限 / 回到分步验证 / 先把各模块验正常 / 派几十上百个 agent / 从系统全局分析
- 怀疑是**多问题耦合**或**地基模块本身没验过**，而非单一表层 bug

## 核心规则 1：反固着升维（硬约束）

**对同一局部点的修复尝试达到 3 次仍未闭环，立即停手**，不得再试第 4 个"单点旋钮"。强制切换到：

1. **自底向上模块验证**：先确认地基模块单独正常（见规则 2），再往上叠。
2. **系统级跨层对照**：把症状当作可能的**内存/缓存/中断/时钟/调度/总线耦合**，逐层对照参考实现（ChibiOS / native），而不是只盯一个文件。
3. **大批并行 agent**：把"不同层 / 不同模块 / 不同假设"拆成**许多并行只读 agent**同时查（构建/分析/对照并行）；硬件验证串行。

> 反模式（本项目 2026-05-29 实证）：CDC 背靠背连续试 裸poll→背压清队列→ring深度→FIFO→多包→守卫…，单点反复，还把板子搞进 CDC/BL 乒乓，越改越差。教训：**地基不稳就先验地基**。

## 核心规则 2：自底向上验证顺序

任一层未验绿（上板 `RESULT: PASS` + fault=0），**不碰上层**：

```
L1 地基模块  : flash / SD / SPI / I2C （单芯片/单总线 smoke）
L2 总线组合  : 外设组合、存储持久化
L3 通信单独  : CDC 物理链路（echo）
L4 通信+协议 : CDC + MAVLink（L0 gate：STANDBY/参数/30s 流）
L5 压力/长稳 : 背靠背 MAVFTP、≥10min soak、重连
```

- 用既有分层测试：`libraries/AP_HAL_RTT/test/` 的 `D_*/E_*/S_*/L7`，`scons --test=<name>`。
- 判据：UART7 见 `[<TEST>] RESULT: PASS`；OpenOCD `CFSR/HFSR=0`、`VTOR=0x08008000`、`IWDGRSTF=0`。
- 失败就**停在该层定位**，不跳层；该层卡 3 次再次触发规则 1 升维。

## 核心规则 3：硬件互斥下的"50 个 agent"

- **可并行（不占硬件）**：所有 `--test=` 的**构建**、源码/日志/寄存器分析、跨实现对照、结果归整 → 一次多 agent 并行（这是"派几十个 agent"的正确用法）。
- **严格串行（独占 ST-Link/CDC/ttyACM）**：**上板烧录 + 运行验证**一次只能一个 agent，按 L1→L5 排队。
- 父代理只规划/只读验收，不下场跑硬件（见 `rtt-agent-orchestration`）。

## CDC / USB 系统级根因知识库（2026-05-29 实证，避免重推）

排查 CherryUSB CDC 背靠背 MAVFTP 不稳时，**先读本节**，不要从零再推：

### 为什么 ChibiOS / 本仓 native 稳
- **单笔多包 transaction**：一次 `usb_lld_start_in` 用 `DIEPTSIZ.PKTCNT` 声明整块多包（如 512B=8 包），TXFE ISR **跨包连续填 FIFO**，DWC2 核自主发完 → **对 ISR 延迟天然免疫**。
- **EPENA 守卫 + XFRC 超时→EPDIS 恢复**：`hal_usb_lld_rtt.c:2231-2266` 发送前查 `DIEPCTL.EPENA`，超时未完成则 `EPDIS` 恢复 → 不会死锁。
- **128B(32 words) per-EP TX FIFO**（in_multiplier=2）+ 深 obqueue。

### CherryUSB shim 缺什么（真因，多因耦合）
1. **逐包 64B**：`hal_usb_cherryusb_shim.c` 每次 `usbd_ep_start_write` 只发 ≤64B 一包 → 每包都依赖及时 OTG ISR（CherryUSB `usbd_ep_start_write` **本就支持多包** PKTCNT，瓶颈在 shim 切 64B）。
2. **无 EPENA 守卫 / 无 XFRC 超时恢复**：`cherry_tx_busy` 一旦与硬件失步（XFRC 在抢占/竞态下丢失）→ **永久卡 1 → 端点死锁 → 之后全 FTP 死、ResetSessions 救不回**（"R1后全死"的真机制）。
3. **TX1 FIFO 仅 64B(16 words)**；**注意：单独把 FIFO 改 128B 实测更差，勿重复该死路**。
4. **耦合放大器**：OTG_FS IRQ 优先级 **5** < SDMMC1=**2** / SD-DMA Stream3/6=**3**，SD 活动抢占 OTG ISR，加剧欠载与 XFRC 竞态。

### 已用证据排除的方向（别再纠结）
- **fd 泄漏 / DFS 耗尽**：GDB `_fdtab.used=0` 全程为空，`DFS_FD_MAX=16` 未打满。
- **D-cache / 内存放置 / DMA 一致性**：生产 **D-cache 全局关闭** + **slave-FIFO（不走 AHB DMA）**，DTCM 放置对 USB 无影响；`1330926404≈0x4F4F45xx` 是 MAVLink/FTP **帧错乱**，非 cache 残留。
- **USB 48MHz 时钟**：与 ChibiOS 同为 PLLQ÷9=48MHz，精确一致。
- **stat shim / FRAM / GCS_FTP 协议**：共享代码、相对 milestone 无 diff；`comp=0`/`CRITICAL(5)` 是背压下帧损坏与下游 failsafe/internalerror 的**症状**，非独立 bug。

### 修复方向（已设计，照抄 ChibiOS/native）
- **Phase 1（停摆守卫）**：把 EPENA 守卫 + busy 看门狗(>100ms 无 XFRC → SNAK|EPDIS + TXFIFO flush + 清 busy + kick) + ring 满背压（不静默丢包）搬进 shim（只改 `hal_usb_cherryusb_shim.c`，易回退）。
- **Phase 2（多包）**：shim `cherry_tx_kick` 从 ring 合并最多 ~512B 一次 `usbd_ep_start_write`（`CDC_TX_CHUNK_MAX=512` + `cdc_tx_buf[512]`，4B 对齐普通 RAM），让 TXFE ISR 跨包填；不动 FIFO。
- **Phase 3（抢占）**：OTG_FS 优先级抬到 SD 之上 或 填 FIFO 用 BASEPRI。

## 与其它 skill 的关系
- `rtt-driver-validation`：分层测试矩阵与验证证据（本 skill 给"顺序与升维"，那个给"怎么跑每个 test"）。
- `rtt-agent-orchestration`：父/子代理分工与硬件互斥（本 skill 强调"卡住就多开并行 + 自底向上"）。
- `rtt-root-cause-playbook`：症状→根因判别树（本 skill 补"何时停止单点、升维到系统级"）。
- `rtt-build-flash-debug`：具体构建/烧录/GDB 命令。

## 禁止
- 同一局部点第 4 次单点旋钮试错（必须先升维）。
- 地基层未验绿就调上层（如 CDC 没单独验就调 CDC+MAVLink 背靠背）。
- 多个 agent 同时占 ST-Link/CDC。
- 重复已证伪的死路（如 CDC TX1 FIFO 单独 128B、native 默认化而不先解 BL 乒乓）。
