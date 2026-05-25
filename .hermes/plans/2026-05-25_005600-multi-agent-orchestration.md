# 多 Agent 并行推进编排方案

> **目标**：28 个 ce-* 员工 + 自动流水线如何高效调度、避免死锁、
> 失败自动恢复、批量推进 RTT 移植。
>
> **参考**：`embedded/rtt-driver-agent-roster` — 完整编队表 + 快速诊断
> **参考**：`devops/kanban-orchestrator` — 派遣铁律 + 反模式

---

## 核心问题

当前 **CEO（我）** 面临的核心矛盾：

| 问题 | 表现 | 根因 |
|------|------|------|
| 员工卡在 ready | dispatcher 不派单 | profile 不存在 / consecutive_failures 满 / claim_lock 残留 |
| 员工跑了但无产出 | 15 分钟超时回收 | 任务 scope 太大 |
| CEO 等不及亲自干 | 自己打开 terminal 跑命令 | 「员工干不如自己干快」错觉 |
| 流水线卡在某一步 | 下个任务在等这个，但这个死了 | 链式依赖无超时保护 |

---

## 编排方案架构

### 层次

```
L0: CEO Orchestrator（我）
    ├─ 战略：看 roadmap，决定下一批做什么
    ├─ 战术：创建 task chain，dispatch，监控
    ├─ 兜底：诊断 blocked，清除 stale lock，处理 protocol_violation
    └─ 汇报：状态变更时主动推送到飞书/对话

L1: Kanban Dispatcher（嵌入式 gateway）
    ├─ 每 60s 扫描 ready 任务 → spawn ce-* worker
    ├─ 15 分钟超时回收
    ├─ 3 次崩溃后 gave_up（不改变 status 但内部放弃）
    └─ 清理 zombie workers

L2: ce-* Workers（28 个嵌入式驱动工程师）
    ├─ 每个 ce-* = 一个驱动专家
    ├─ 独立 git workspace
    ├─ 完成后 kanban_complete()
    └─ 失败自动标记 blocked

L3: CCP Daemon（24/7 硬件探针）
    ├─ 每 5 分钟 probe（OpenOCD + MAVLink）
    ├─ L1-L4 验证
    ├─ 异常 → 自动创建 kanban task
    └─ 成功 → 通知、更新里程碑
```

---

## Step 1 — 员工分组与并行流水线

### 分四组并行推进

```
Group A（核心驱动 — 10 个 parallel）：
  ce-spidevice, ce-i2cdevice, ce-uartdriver,
  ce-gpio, ce-analogin, ce-scheduler,
  ce-system, ce-semaphores, ce-storage, ce-util

Group B（传感器驱动 — 8 个 parallel）：
  ce-imu(icm20689), ce-imu(icm42688), ce-barometer,
  ce-magnetometer, ce-gps, ce-iomcu,
  ce-can, ce-hardware-cam

Group C（输出驱动 — 6 个 parallel）：
  ce-rcinput, ce-rcoutput, ce-rcoutput_serial,
  ce-rcoutput_bdshot, ce-rcoutput_iofirmware,
  ce-softsigreader

Group D（基础设施 — 4 个 parallel）：
  ce-mavros, ce-sdcard, ce-stdio, ce-dsp
```

每一组内任务互相独立 → 可以同时 dispatch；组之间可能有依赖 → 用 `parents[]` 控制。

---

## Step 2 — 任务链标准模板

### 标准三链（FIX → BUILD → VERIFY）

```python
def create_fix_chain(assignee, title, body):
    """创建 FIX → BUILD → VERIFY 三条链任务"""
    import time, sqlite3
    db = sqlite3.connect(os.path.expanduser("~/.hermes/kanban.db"))
    ts = int(time.time())

    fix_id = f"t_chain_fix_{assignee}_{ts}"
    build_id = f"t_chain_build_{assignee}_{ts}"
    verify_id = f"t_chain_verify_{assignee}_{ts}"

    # FIX: 修复代码
    db.execute("""
        INSERT INTO tasks (id, title, assignee, status, created_at, body, workspace_kind)
        VALUES (?, ?, ?, 'ready', strftime('%s','now'), ?, 'worktree')
    """, (fix_id, f"[FIX] {assignee}: {title}", assignee, body))

    # BUILD: 编译验证
    db.execute("""
        INSERT INTO tasks (id, title, assignee, status, created_at, body, workspace_kind)
        VALUES (?, ?, ?, 'todo', strftime('%s','now'), ?, 'dir')
    """, (build_id, f"[BUILD] {assignee}: {title}", "ce-scons", f"""
编译 {assignee} 的修改
工作目录: /data/firmare/pogo-apm
cd /data/firmare/pogo-apm && scons --v=ArduCopter --target=cuav_v5 -j$(nproc)
完成后 kanban_complete()
"""))

    # VERIFY: 烧录验证（L1: main_loop > 0）
    db.execute("""
        INSERT INTO tasks (id, title, assignee, status, created_at, body, workspace_kind)
        VALUES (?, ?, ?, 'todo', strftime('%s','now'), ?, 'scratch')
    """, (verify_id, f"[VERIFY] {assignee}: {title}", "ce-mavros", f"""
烧录 + 验证 {assignee} 的修改
1. OpenOCD 烧录
2. 检查 main_loop > 0
3. 检查 MAVLink 心跳
完成后 kanban_complete()
"""))

    # 链接依赖
    db.execute("INSERT INTO task_links (parent_id, child_id) VALUES (?, ?)", (fix_id, build_id))
    db.execute("INSERT INTO task_links (parent_id, child_id) VALUES (?, ?)", (build_id, verify_id))

    db.commit()
    db.close()
    return fix_id, build_id, verify_id
```

---

## Step 3 — CEO 自动巡检基础设施

### 巡检清单（每 3 分钟 cron）

```python
def ceo_patrol():
    """CEO 自动巡检——每 3 分钟执行"""
    db = sqlite3.connect(os.path.expanduser("~/.hermes/kanban.db"))
    cur = db.cursor()
    report = []

    # 1. 统计看板全景
    cur.execute("SELECT status, COUNT(*) FROM tasks GROUP BY status")
    stats = dict(cur.fetchall())
    report.append(f"📊 board: {stats}")

    # 2. 发现 ready 但 dispatcher 没管的
    cur.execute("SELECT id, assignee, title FROM tasks WHERE status='ready' AND consecutive_failures >= 2")
    stale = cur.fetchall()
    for tid, assignee, title in stale:
        cur.execute("UPDATE tasks SET consecutive_failures=0 WHERE id=?", (tid,))
        report.append(f"🔄 解阻塞: {assignee}/{title[:40]}")

    # 3. 发现 running 但进程死了的（zombie）
    cur.execute("SELECT id, assignee, worker_pid FROM tasks WHERE status='running'")
    for tid, assignee, pid in cur.fetchall():
        if pid:
            import os
            try:
                os.kill(pid, 0)  # 检查进程死活
            except OSError:
                cur.execute("UPDATE tasks SET status='ready', claim_lock=NULL WHERE id=?", (tid,))
                report.append(f"🧟 清理 zombie: {assignee}/{tid} (pid {pid} dead)")

    # 4. 发现 done 但 protocol_violation 的（git log 有改动）
    cur.execute("""
        SELECT id, assignee FROM tasks WHERE status='blocked'
        AND title LIKE '[FIX]%'
    """)
    for tid, assignee in cur.fetchall():
        report.append(f"⏳ blocked FIX task: {assignee}/{tid} — 需手动确认")

    db.commit()
    db.close()
    return report
```

---

## Step 4 — 超时保护与死锁预防

| 模式 | 检测 | 动作 |
|------|------|------|
| FIX worker 跑 > 15 min | dispatcher 回收 | 标记 blocked，创建拆分版新任务 |
| BUILD worker 跑 > 20 min | CEO 巡检杀死 | scons 单独调大超时 |
| 链阻塞 > 30 min（上游 done 但下游不 ready） | 检查 parents 链 | 手动执行 sqlite UPDATE task_links 或创建替代任务 |
| 同一 task 连续 3 次 gave_up | 标记 done（不重试） | 换 profile 或委托不同员工 |
| ce-* profile 不存在 | dispatcher 跳过 | CEO 巡检自动检测并报告 |

---

## Step 5 — 批量推进策略

### 第一波（Phase 1 — 核心驱动组 A，10 个 parallel）

```python
group_a = [
    ("ce-spidevice", "SPI 完整 CMSIS 寄存器重写", 9),
    ("ce-i2cdevice", "I2C 硬件寄存器化", 8),
    ("ce-uartdriver", "UART CMSIS + CDC 集成", 8),
    ("ce-gpio", "GPIO 寄存器直写 + OSPEEDR", 7),
    ("ce-analogin", "ADC DMA 确认 + D-Cache", 7),
    ("ce-scheduler", "调度器稳定 + 栈大小", 6),
    ("ce-system", "启动链路 + 时钟 + 看门狗", 6),
    ("ce-semaphores", "Semaphore 确认 + 死锁", 5),
    ("ce-storage", "Storage Flash 磨损均衡", 5),
    ("ce-util", "Util watchdog 寄存器化", 5),
]

for assignee, title, pri in group_a:
    create_fix_chain(assignee, title, f"## 任务\n{title}\n## 工作目录\n/data/firmare/pogo-apm")
```

### 第二波（依赖第一波结果 — 传感器组 B）
### 第三波（输出组 C + 基础设施 D）

---

## 反模式速查

| 症状 | 我在做什么（错的） | 应该做什么 |
|------|-------------------|-----------|
| 问题出在 SPI，CEO 自己打开 GDB | "我查一下寄存器" | 创建 kanban task → dispatch ce-spidevice |
| 员工跑了 5 分钟没结果，CEO 自己跑 pymavlink | "开一下串口确认心跳" | 再等 5 分钟，或用巡检脚本检查进程 |
| 编译失败，CEO 自己看编译日志改代码 | "就改一行" | 创建 [FIX] task → dispatch ce-scons |
| 看板上全是 ready，CEO 一个一个点 dispatch | "Dispatcher 太慢了" | 用巡检脚本批量解阻塞 + 自动 dispatch |