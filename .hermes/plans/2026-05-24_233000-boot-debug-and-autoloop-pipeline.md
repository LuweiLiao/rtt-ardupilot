# 方案：启动慢根因深挖 + 自治自循环流水线

## 一、启动慢问题——诊断结论

先明确一个关键判断：**启动慢很可能是 GDB/OpenOCD 单步调试的测量伪像，不是固件真实慢。**

### 时钟配置链（已确认没问题）
```
Reset_Handler
  → SystemInit()           [仅 FPU + VTOR, 几 us]
  → data copy + bss zero   [~200KB, 几 ms]
  → entry()
    → rtthread_startup()
      → rt_hw_board_init()
        → SystemClock_Config()  ← 16MHz HSI → 216MHz PLL
```

`SystemClock_Config()` 在 `stm32f7_clock_ll.c` 中正确配置：
- HSE 16MHz 晶振 → PLL (×216/8÷2) → 216MHz SYSCLK
- Flash Latency 7 wait states
- OverDrive 模式开启
- HSE fallback → HSE_BYPASS → HSI（3级冗余）

**结论：时钟配置本身没有 bug。** 之前的 18 秒/4 字节现象，是 OpenOCD halt/resume 单步测量导致的，不代表固件真实运行速度。

### 真正需要验证的（而非怀疑时钟）
1. **烧录后直接 `reset run`，等 30 秒再 halt** — 不要单步看 PC
2. 直接读 `main_loop_iterations` — 如果 > 0 说明主循环在跑
3. 读 MAVLink 心跳 — 这是最终验证

---

## 二、真正的阻塞链（当前问题）

```
main_loop_entry → setup_stage=651 ✅
  → AP_Scheduler::loop()
    → AP::ins().wait_for_sample()
      → SPIDevice._do_transfer()  ← FRXTH 修复前死循环
```

**FRXTH 修复 + DEVID5 修正** 已经编译烧录成功，但上次烧录因 OpenOCD 被杀导致 Flash 被擦空。**现在要做的不是更多调试，而是重新烧录并验证。**

---

## 三、自治自循环流水线方案

### 核心理念

**不再弹窗问"要不要做"**，而是用一个 kanban 驱动的闭环流水线：

```
               ┌──────────────────────────┐
               │   CCP (Continuous Closed  │
               │   Probe) 监视器           │
               │   — 5分钟 tick            │
               └───────┬──────────────────┘
                       │ poll: 最近任务完成？
                       │ 固件是否在跑？
                       ↓
          ┌────────────────────────────┐
          │  发现新问题 / 上一步完成    │
          └────────────┬───────────────┘
                       ↓
    ┌──────────────────────────────────────┐
    │  诊断 Agent (ce-diag)               │
    │  OpenOCD + GDB 读调试变量 → 定位    │
    └──────────────┬──────────────────────┘
                   ↓
    ┌──────────────────────────────────────┐
    │  修复 Agent (ce-spidevice / 对应人)  │
    │  分析根因 → 出代码修复               │
    └──────────────┬──────────────────────┘
                   ↓
    ┌──────────────────────────────────────┐
    │  编译 Agent (ce-scons)              │
    │  scons -j$(nproc)                   │
    └──────────────┬──────────────────────┘
                   ↓
    ┌──────────────────────────────────────┐
    │  烧录 Agent (ce-burn)               │
    │  OpenOCD program → verify           │
    └──────────────┬──────────────────────┘
                   ↓
    ┌──────────────────────────────────────┐
    │  验证 Agent (ce-mavros)             │
    │  pymavlink 收心跳 + OpenOCD 读变量  │
    └──────────────┬──────────────────────┘
                   ↓
              ←─── 回 CCP 监视器循环
```

### 各组件详细设计

#### 1. CCP 监视器（Continuous Closed Probe）

**类型**：cronjob，每 5 分钟 tick
**行为**：
- 读 kanban board：当前是否有 `ready` 状态的 ce-* 任务
- 如果有 → dispatch 给对应 worker（不等我亲自问）
- 如果没有 → OpenOCD 读 `main_loop_iterations` 和 `fast_loop_count`
  - 如果 `main_loop_iterations > 0` → 发状态报告到飞书
  - 如果仍为 0 → 触发诊断 pipeline

**挂载点**：`cronjob(action='create', schedule='every 5 min')`

#### 2. 链式触发（Chain Trigger v3）

之前的 kanban_chain_trigger.py v2 有问题（重复创建任务）。v3 改进：
- 只监听当前 worker 的 `ce-*` 任务完成事件
- 完成一个任务后，自动创建下一个阶段的 task
- 不用 inotify（不可靠），用轮询 + last_event_id

#### 3. 诊断 → 修复 → 编译 → 烧录 → 验证 五段链

| 阶段 | kanban profile | 行为 | 完成条件 |
|------|---------------|------|---------|
| 诊断 | ce-diag | OpenOCD halt, 读调试变量, 分析阻塞点 | 输出根因定位报告 |
| 修复 | ce-* (按模块) | 代码修改 | git diff 确认改动 |
| 编译 | ce-scons | `scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` | 编译通过 |
| 烧录 | ce-burn | OpenOCD program + verify | verify OK |
| 验证 | ce-mavros | CDC MAVLink 心跳 + OpenOCD 读变量 | `main_loop_iterations > 0` + 心跳持续 |

**关键规则**：
- **不弹窗问"行不行"** — 每阶段完成后自动创建下一阶段
- **卡住时**（编译失败、烧录失败、验证失败）→ ce-diag 再分析 → 修复 → 重试
- **重试 3 次仍失败** → 降级报告到飞书（不卡死）

### 异常处理

| 场景 | 处理 |
|------|------|
| 编译失败 | ce-diag 读编译错误 → 自动创建新修复任务 |
| 烧录中 OpenOCD 被杀 | 重启 OpenOCD → 重新 program |
| 验证失败（心跳无） | ce-diag 读 OpenOCD 调试变量 → 定位 → 修复 |
| 3 次重试全部失败 | 飞书汇报完整日志 + 暂停链（需人工介入） |

---

## 四、第一步执行计划（确认后执行）

**Step 1：烧录 FRXTH+DEVID5 修复版并验证**
```
scons --v=ArduCopter --target=cuav_v5 -j$(nproc)  # 已编译通过
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify" \
  -c "reset run" -c "exit"
# 等待 30 秒 → pymavlink 收心跳
# 如心跳 OK → main_loop_iterations > 0 → L0 通过
```

**Step 2：建立 CCP 监视器 cronjob**
```bash
cronjob(action='create', schedule='every 5 min', 
        prompt='检查 kanban board 和固件状态...')
```

**Step 3：清理垃圾 kanban 任务**
重复的、陈旧的任务归档，只保留活跃的 ce-* 任务链

**Step 4：正常流水线运行**
CCP 监视 → 发现问题 → 诊断 → 修复 → 编译 → 烧录 → 验证 → 循环

---

## 五、风险与开放问题

| 风险 | 缓解 |
|------|------|
| CCP cronjob 重复通知 | 状态变更才发飞书，不变不发 |
| 链式触发因 OpenOCD 失败而循环重试 | 3 次 cap，超过暂停 |
| 用户想改流水线配置 | 本计划作为文档，修改时更新 plan 文件 |
| Mem0 qdrant 冲突 | 当前不阻塞流水线运行，后续单独修复 |

**开放问题**：
1. CCP 的轮询间隔：5 分钟是否合适？太短浪费 token，太长延迟大
2. 是否每条中间结果都发飞书，还是只发关键状态变化？
3. 是否需要给用户一个"暂停/恢复流水线"的命令接口？

---

## 六、文件变更清单

| 文件 | 变更 |
|------|------|
| (无需变更) | 时钟配置已验证通过 |
| (无需变更) | SPIDevice.cpp FRXTH 修复已完成 |
| (无需变更) | hwdef ICM42688 DEVID5 修正已完成 |
| `.hermes/scripts/kanban_chain_trigger.py` | 后续改进 v3（如链式有 bug） |
| `.hermes/skills/` | 新增 `rtt-autonomous-pipeline` skill（可选） |
