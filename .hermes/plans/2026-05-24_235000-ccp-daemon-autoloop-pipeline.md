# 方案：RTT 移植 24/7 自治自循环流水线

## 一、GitHub 项目调研结论

搜到以下最能参考的自治循环项目：

| 项目 | ⭐ | 核心思想 | 对我们最可借鉴的点 |
|------|-----|---------|------------------|
| **ralphy** (michaelshimeles) | 2873 | Bash 脚本循环：读任务清单 → 跑 AI Agent 执行 → 标记完成 → 循环，支持 Claude Code/Codex/OpenCode/Cursor 多引擎 | 1️⃣ 核心循环模式最简单实用 2️⃣ 失败重试(3次) + 延迟机制 3️⃣ 进度文件驱动 |
| **bmad-ralph** (qianxiaofeng) | 11 | Rust CLI daemon，编排并行 Claude Code worker，三层自愈(retry→restart→diagnose) | 1️⃣ 守护进程 24/7 架构 2️⃣ SQLite 持久化+WAL 3️⃣ Unix Socket IPC |
| **ARIS** (wanshuiyin) | 10501 | 五步循环：plan→draft→adversarial review→iterate→persist | 1️⃣ 对抗审(adversarial review) 2️⃣ Skill 驱动方法论 3️⃣ 兼容多引擎 |
| **superpowers** (obra) | 204646 | 技能框架+方法论+子 Agent 驱动开发 | 1️⃣ Subagent-driven-development 2️⃣ YAGNI/DRY/TDD 铁律 |
| **MetaBot** (xvirobotics) | 795 | 飞书/Telegram 控制 Claude Code/Kimi Code，多 Agent 团队 | 1️⃣ IM 集成模式 2️⃣ PM2 自动管理 |

**最关键的两个模式**：

```
ralphy 模式 (最简单):                           bmad-ralph 模式 (最完备):
  启动 → 读任务清单 → 运行 Agent                   daemon start → 读计划
    → 标记完成 → 循环                            → 并行 workers
    → 全部完成 → 退出                             → 自愈(3层)
    → 失败 → 重试(3次)                          → 终端 dashboard
                                                  → 24/7 不停
```

---

## 二、我们的自循环流水线设计（融合 ralphy + bmad-ralph）

### 整体架构

```
┌────────────────────────────────────────────────────────────────┐
│                  CCP Daemon (Continuous Closed Probe)           │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   │
│  │ 探针模块  │ → │ 诊断模块  │ → │ 修复模块  │ → │ 验证模块  │   │
│  │ (Probe)  │   │ (Diag)   │   │ (Fix)    │   │ (Verify) │   │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └────┬─────┘   │
│       │              │              │              │          │
│       └──────────────┴──────────────┴──────────────┘          │
│                         ← 循环 ←                              │
└────────────────────────────────────────────────────────────────┘
        │                          ↑
        │ 检测状态变化               │ 自我反馈
        ↓                          │
┌──────────────────┐               │
│ 飞书通知用户      │───────────────┘
│ (状态变更才发)    │
└──────────────────┘
```

### 模块详细设计

#### 1️⃣ 探针模块 (Probe) — 每 5 分钟 tick

```python
def probe():
    # 1. OpenOCD 连接检测
    if not can_connect_openocd():
        restart_openocd()  # 自愈
    
    # 2. 读关键调试变量
    setup_stage = read_memory(0x2001b2c8)      # 初始化进度
    main_loop_iterations = read_memory(0x20018fb4)  # 主循环次数
    fast_loop_count = read_memory(0x20018fa8)       # 快速循环
    had_hardfault = check_hardfault_status()
    
    # 3. 读 MAVLink 心跳
    heartbeat_rx = check_mavlink_heartbeat("/dev/ttyACM1")
    
    # 4. 判断当前状态
    return Status(
        main_loop_iterations > 0,     # 主循环在跑?
        heartbeat_rx > 0,             # 心跳在收?
        setup_stage,                  # 初始化到了哪步
        had_hardfault,                # 有没有硬错误
        fast_loop_count > 0           # 快速循环在跑?
    )
```

#### 2️⃣ 诊断模块 (Diag) — 发现问题时自动触发

```python
def diagnose(status):
    if status.had_hardfault:
        # 分析 HardFault → 定位代码行 → 出修复方案
        return HardFaultDiagnosis(...)
    
    if not status.main_loop_running:
        # 查 setup_stage 卡在哪
        # 如果是 wait_for_sample() → SPI 问题
        # 如果是其他 → 查具体模块
        return BlockedDiagnosis(blocked_module="SPI_IMU", ...)
    
    if status.main_loop_running and not status.heartbeat_ok:
        # 主循环在跑但心跳不活 → USB CDC 或 MAVLink 问题
        return HeartbeatDiagnosis(...)
    
    # 一切正常 → 不需要行动
    return NoAction()
```

#### 3️⃣ 修复模块 (Fix) — 自动创 kanban 任务/派 worker

```python
def fix(diagnosis):
    if diagnosis.type == "hardfault":
        # 分析 ESR → 定位代码 → 创建 ce-* 修复任务
        create_kanban_task("ce-diag", "分析 HardFault ESR={esr}")
    
    elif diagnosis.type == "blocked_init":
        # 知道卡在哪个模块，直接派对应 ce-* worker
        create_kanban_task(worker_for_module(diagnosis.module), 
                          f"修复 {diagnosis.module} 初始化阻塞")
    
    elif diagnosis.type == "no_action":
        return  # 什么都不做
```

#### 4️⃣ 验证模块 (Verify) — 修复后验证

```python
def verify(task):
    # 编译检查
    if not task.has_compile_succeeded:
        # 已自动创建 ce-scons 编译任务
        return "waiting_for_compile"
    
    # 烧录检查
    if not task.has_burn_succeeded:
        create_kanban_task("ce-burn", "烧录固件")
        return "waiting_for_burn"
    
    # 验证
    status = probe()
    if status.main_loop_running:
        return "VERIFIED"  # ✅
    else:
        # 没通过 → 诊断 → 下一轮
        return "FAILED"
```

### 自愈机制（3 层，从 bmad-ralph）

| 层级 | 触发条件 | 处理 |
|------|---------|------|
| L1: Retry | 编译/烧录/验证失败 | 重试 3 次，每次间隔 30 秒 |
| L2: Restart | 重试 3 次仍失败 | 重启 OpenOCD，重烧，读新状态 |
| L3: Diagnose | L2 后还不行 | 彻底诊断 → 换修复方案 → 通知用户 |

### 与现有基础设施的结合

**已有**：
- Hermes cronjob（`kanban-autopilot`，job_id `9b6f5ba924b5`）
- kanban DB（`/home/llw/.hermes/kanban.db`）
- ce-* 员工编队（ce-diag, ce-scons, ce-burn, ce-mavros, ce-spidevice 等）

**新增**：
- **CCP daemon**: 一个 Python 脚本（类似 ralphy 的单文件脚本），代替现有的 `kanban-autopilot` cronjob
- **工作流**：Probe → Diagnose → Fix → Verify → Loop，全自动无人干预
- **飞书通知**：只在状态变更时推送（不重复刷屏）

### 为什么不直接用 ralphy？

ralphy 是通用 coding agent 循环，适合"写代码 → 测试 → 写下一个"的场景。我们的场景是**嵌入式硬件调试**，需要：
1. OpenOCD/GDB 读硬件状态（不只是 git diff）
2. 编译嵌入式固件（不是 npm build）
3. 通过 ST-Link 烧录（不是 git merge）
4. 通过 MAVLink 收心跳验证（不是 npm test）

所以需要自己写一个专用 daemon，但**循环架构和自愈思想完全借鉴 ralphy/bmad-ralph**。

---

## 三、执行计划（确认后自动执行，不再弹窗）

### Step 1：先验证 FRXTH 修复版能否跑起来
这是当前的阻断点 — 上一次烧录被 OpenOCD 杀死导致 Flash 为空。需要：
```
1. OpenOCD 后台启动 → telnet program 烧录 FRXTH+DEVID5 固件
2. reset run → 等 30 秒
3. OpenOCD 读 main_loop_iterations 和 fast_loop_count
4. 如 > 0 → 验证通过，建 CCP daemon
5. 如仍为 0 → 诊断 SPI 问题
```

### Step 2：写 CCP daemon 脚本
约 200-300 行 Python，单文件，放在 `.hermes/scripts/ccp_daemon.py`。

### Step 3：cronjob 注册
```
cronjob(
    action='create',
    schedule='every 5 min',
    script='.hermes/scripts/ccp_daemon.py',
    name='rtt-ccp-daemon'
)
```

### Step 4：清理 kanban 垃圾任务
归档所有重复/陈旧的 task，只保留活跃链。

### Step 5：水到渠成
CCP 每 5 分钟检查 → 发现问题 → 自动修 → 自动验证 → 飞书通知。

---

## 四、风险与缓解

| 风险 | 缓解 |
|------|------|
| 无限循环（修不好反复重试） | 3 次 cap → 报告用户暂停链 |
| 飞书刷屏通知 | 只在状态变更时推送（当前状态 ≠ 上次状态） |
| OpenOCD 端口占用 | 启动前 `killall -9 openocd`，确认端口释放 |
| 编译失败后卡死 | 读编译错误 → 自动创建新修复 kanban 任务 |
| 烧录过程中 OpenOCD 被杀 | L2 Restart → 重启 OpenOCD → 重新 program |
| daemon 本身挂了 | cronjob 保证 5 分钟后重新拉起（自修复） |

---

## 五、CCP daemon 伪代码框架

```python
#!/usr/bin/env python3
"""CCP Daemon — Continuous Closed Probe for RTT ArduPilot"""
import subprocess, time, json, os

STATE_FILE = "/tmp/ccp_state.json"  # 记录上次状态

def probe():
    """读取板子状态"""
    # OpenOCD memory reads
    stages = {}

    # 检查死活状态
    return {"main_loop_ok": ..., "heartbeat_ok": ..., ...}

def has_state_changed(current, last):
    """判断状态是否真正变化"""
    return current["main_loop_ok"] != last.get("main_loop_ok")

def diag(status):
    """诊断阻塞点"""
    ...

def fix(diagnosis):
    """修复：创建 kanban 任务或直接操作"""
    ...

def main():
    last_state = load_state(STATE_FILE)
    current = probe()
    
    if has_state_changed(current, last_state):
        notify_user(current)
        save_state(STATE_FILE, current)
    
    if not current["main_loop_ok"]:
        d = diag(current)
        fix(d)
    
    # 一切正常 → 什么也不做

if __name__ == "__main__":
    main()
```
