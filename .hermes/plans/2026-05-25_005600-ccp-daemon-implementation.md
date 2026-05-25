# CCP Daemon 实现方案 — 24/7 自治闭环流水线

> **目标**：从 Phase 1 PoC（cron tick 手动探针）升级为完整的自动闭环。
> daemon 自主探针→诊断→派工（kanban）→等待→验证→循环。
>
> **架构参考**：`embedded/ccp-daemon-architecture` skill (v5, 1594行)
> **PoC 基础**：`/home/llw/.hermes/scripts/ccp_daemon.py` (Phase 1, ~900行)

---

## 当前状态

已实现的 PoC 功能：
- `--probe` 探针：OpenOCD 读 main_loop、setup_stage、HFSR/CFSR
- `--tick` 定时：每 5 分钟 cron 触发
- `--run-once` 单次执行：probe → diagnose → build → flash → verify
- ManagedProcess 进程管理
- 资源监护（磁盘/内存/uptime）

缺失的核心功能：
- **L1-L4 多层次验证**（L1调度器/L2心跳/L3传感器/L4稳定性）
- **CC 派工**（诊断出问题后自动创建 kanban task dispatch 给 ce-*）
- **自动回归**（快照对比、退化检测）
- **done marker**（CC 改完代码后写 marker，daemon 检测后才 build）
- **git worktree 隔离**（独立 worktree，失败丢弃）

---

## Phase 1 — 单次闭环（1 周）

### Step 1.1: 启用 L1-L4 验证

**改 `ccp_daemon.py`**，把 SKILL.md 中的 `FunctionalVerifier` 类集成进 daemon。

```python
# 替换 tick() 中简单的 probe→日志
# 改为 L1-L4 层次验证:

def tick(self):
    # 1) L1: OpenOCD 读 main_loop_iterations
    iterations = self._probe_main_loop()     # OpenOCD mdw 0x20018fb4

    if iterations > 0:
        # L2: pymavlink wait_heartbeat
        hb = self._check_heartbeat()          # ttyACM1, 921600, timeout=13
        # L3: RAW_IMU 消息 ≥ 3 条
        imu = self._check_raw_imu()
        # L4: 连续 4 轮 L1-L3 全通过无退化
        self._check_stability(iterations, hb, imu)
    else:
        # main_loop=0 → 诊断 → 创建 kanban task
        self._diagnose_and_dispatch()
```

**验收**：`--tick` 输出 `L1_PASS` / `L2_PASS` / `L3_PASS` / `L4_OK`

**10 个文件？** 不，只有 1 个文件：`ccp_daemon.py`
**改动量**：+200 行（FunctionalVerifier + _probe_main_loop + _check_heartbeat + _check_raw_imu + _check_stability）

---

### Step 1.2: 集成 kanban 派工

diagnose 发现 main_loop=0 后，自动创建 kanban task dispatch 给对应 ce-* 员工。

```python
def _diagnose_and_dispatch(self):
    """诊断挂死根因 → 创建 kanban task → dispatch"""
    # 1. 读调试变量确定挂死阶段
    setup_stage = self._read32(SETUP_STAGE_ADDR)

    # 2. 映射到问题类型
    if setup_stage < 0x262:          # I2C probe 阶段
        problem = "I2C probe hang"
        assignee = "ce-i2cdevice"
    elif setup_stage < 0x280:        # SPI IMU probe
        problem = "SPI IMU probe hang"
        assignee = "ce-spidevice"
    else:                            # wait_for_sample 或其他
        problem = "init_ardupilot hang (SPI/IMU)"
        assignee = "ce-spidevice"

    # 3. 创建 kanban task（避开 deadlock 的 task）
    import sqlite3
    db = sqlite3.connect(os.path.expanduser("~/.hermes/kanban.db"))
    task_id = f"t_ccp_{int(time.time())}"
    db.execute("""
        INSERT INTO tasks (id, title, assignee, status, created_at, body)
        VALUES (?, ?, ?, 'ready', strftime('%s','now'), ?)
    """, (task_id, f"[CCP] {problem}", assignee, f"""
## CCP 自动诊断报告
- 现象: main_loop={self._read32(MAIN_LOOP_ADDR)}, setup_stage={setup_stage}
- 诊断: {problem}
- 上次成功固件: {self._last_successful_bin}

修复方向:
1. 读 ChibiOS 参考对比
2. 修复后 kanban_complete()
"""))
    db.commit()
    db.close()
```

**改动量**：+80 行（_diagnose_and_dispatch + 问题分类逻辑）

---

### Step 1.3: 添加 done marker + build → flash → verify 流水线

当 kanban task 被 worker 完成后，daemon 检测到 → 自动 build → flash → verify。

```python
def _check_done_markers(self):
    """检测是否有 ce-* worker 完成的 task → build → flash → verify"""
    import sqlite3
    db = sqlite3.connect(os.path.expanduser("~/.hermes/kanban.db"))
    # 找最近 1 小时内完成的 CCP 任务
    cur = db.execute("""
        SELECT id, assignee, title, completed_at
        FROM tasks WHERE title LIKE '[CCP]%' AND status='done'
        AND completed_at > strftime('%s','now') - 3600
        AND id NOT IN (SELECT task_id FROM ccp_built)
    """)
    for row in cur.fetchall():
        task_id = row[0]
        # scons build
        result = self._do_build()
        if result["status"] == "OK":
            # OpenOCD flash
            flash = self._do_flash(result["bin"])
            # 等 30s 后 L1-L4 verify
            time.sleep(30)
            if self._verify_all():
                self._mark_built(task_id)  # 记录已 build 过
                self._notify_feishu(f"✅ CCP: {row[2]} build+flash OK")
```

**改动量**：+150 行（_check_done_markers + _do_build + _do_flash + _mark_built）

---

### Step 1.4: 集成飞书通知

当发生状态变化时推送。

```python
def _notify_feishu(self, message):
    """通过飞书 webhook 或 Hermes send 发送通知"""
    # 方式 A: 通过 Hermes 的 kanban notify-subscribe（如果 task 已订阅）
    # 方式 B: 直接飞书 API
    requests.post(FEISHU_WEBHOOK_URL, json={"msg_type": "text", "content": {"text": message}})
```

**改动量**：+20 行

---

### Phase 1 总改动

| 文件 | 改动 |
|------|------|
| `ccp_daemon.py` | +450 行（L1-L4 + kanban派工 + done marker + build+flash + notify） |
| cron 任务 | 更新为每 5 分钟 --tick |

**Phase 1 验收**：
1. main_loop=0 → daemon 自动创建 kanban task → kanban 有 [CCP] 开头的新任务
2. ce-* worker 完成后 → daemon 自动 build → flash → verify
3. L1-L4 全通过后 → 通知

---

## Phase 2 — 隔离加固与退化处理（2 周）

### Step 2.1: git worktree 隔离

### Step 2.2: 自动回归（快照对比）

### Step 2.3: L4 退化检测 + 自动回退

### Step 2.4: 三用户隔离（hermes/ccpd/ccrunner）

---

## Phase 3 — 长期稳定性（1 个月）

### Step 3.1: 多模式诊断（STM32 寄存器活体探针）
### Step 3.2: 24/7 无人值守（ccpd systemd）
### Step 3.3: daemon 自身监控（心跳 + 崩溃自动重启）

---

## 关键路径选择

| 决策 | 选择 | 原因 |
|------|------|------|
| 诊断→派工方式 | 创建 kanban task（非 delegate_task） | 持久化、可审计、可重试 |
| done marker 检测 | 轮询 kanban.db task_events | 无额外文件，与现有系统集成 |
| build 位置 | project 目录（/data/firmare/pogo-apm） | scons 正常工作的唯一位置 |
| 通知方式 | 飞书 webhook + Hermes notify-subscribe | 双保险 |