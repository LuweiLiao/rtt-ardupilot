# RTT 移植工程 — 全方案总览

> 2026-05-25 汇总
> 4 份 Plan，覆盖驱动重写 → 构建系统 → 自动闭环 → 多Agent编排
> 规划阶段，尚无代码执行

---

## Plan ①：全驱动 CMSIS 寄存器重写
📄 `.hermes/plans/2026-05-25_005600-rtt-driver-cmsis-rewrite.md`

**一句话**：所有外设驱动去掉 STM32 HAL 和 CherryUSB，改为 CMSIS 寄存器直写，风格贴 ChibiOS LLD。

```
Phase 0 ─ 启动链路对齐 ChibiOS（链接脚本+双栈+FPU寄存器+看门狗）
Phase 1 ─ SPI 驱动：drv_spi.c 4 个函数体 CMSIS 化
Phase 2 ─ USB CDC：替换 CherryUSB，接入已有 hal_usb_lld_rtt.c
Phase 3 ─ UART 寄存器化
Phase 4 ─ I2C 硬件寄存器化
Phase 5 ─ PWM/RCInput/GPIO/CAN/Flash 确认
```

**改动量**：~1200 行 CMSIS 寄存器代码
**验收**：scons 通过 → 烧录 → MAVLink HEARTBEAT + RAW_IMU

---

## Plan ②：scons `--board=xxx` 自动发现
📄 `.hermes/plans/2026-05-25_005600-rtt-scons-board-architecture.md`

**一句话**：`scons --board=cuav_v5` 像 `waf configure --board=CUAVv5` 一样自动发现 hwdef 配置。

```
Step 1 ─ 新建 board_config.py（cuav_v5/pixhawk6c_mini/fmuv2）
Step 2 ─ 改 SConstruct：discover_boards() + --board 选项 + 删旧字典
Step 3 ─ 改 rtt_bsp_deploy.py：文件系统发现取代字典
Step 4 ─ hwdef.dat 加 define BOARD_TYPE
Step 5 ─ 主流程整合 + --target 兼容
Step 6 ─ 双路编译二进制对比验证
```

**改动量**：+234 行 / -105 行 → +129 行净增
**验收**：`scons --board=cuav_v5` 编译结果 md5sum 与 `--target=cuav_v5` 一致

---

## Plan ③：CCP Daemon 24/7 闭环
📄 `.hermes/plans/2026-05-25_005600-ccp-daemon-implementation.md`

**一句话**：从 Phase 1 PoC 升级为完整自治闭环——daemon 5 分钟探针→诊断→kanban 派工→验证。

```
Phase 1（1周）── 单次闭环
  Step 1.1: L1-L4 多层次验证（+200行）
  Step 1.2: 集成 kanban 派工（+80行）
  Step 1.3: done marker + build→flash→verify（+150行）
  Step 1.4: 飞书通知（+20行）
  ── 共计 +450 行，改 1 个文件

Phase 2（2周）── 隔离加固
  git worktree + 三用户隔离 + 自动回归 + L4退化回退

Phase 3（1月）── 长期稳定
  多模式诊断(寄存器活体探针) + 24/7 systemd + 崩溃自愈
```

**验收**: main_loop=0 → daemon 自动创建 kanban task → ce-* 完成 → daemon build+flash+verify

---

## Plan ④：多 Agent 并行推进编排
📄 `.hermes/plans/2026-05-25_005600-multi-agent-orchestration.md`

**一句话**：28 个 ce-* 员工如何高效调度、避免死锁、批量推进。

```
分组（4组并行）：
  组A ─ 核心驱动（10个parallel）：SPI/I2C/UART/GPIO/ADC/调度器/系统/Semaphore/Storage/Util
  组B ─ 传感器驱动（8个parallel）：IMUx2/气压计/磁力计/GPS/IOMCU/CAN/摄像头
  组C ─ 输出驱动（6个parallel）：RCInput/RCOutput 系列
  组D ─ 基础设施（4个parallel）：MAVROS/SDCard/Stdio/DSP

标准链：FIX → BUILD → VERIFY 三链自动创建
巡检：每 3 分钟 CEO 自动扫 board → 解阻塞 → 清理 zombie → 报告
超时保护：15 min FIX / 20 min BUILD → 自动拆分/清理
```

**关键反模式**: CEO 不动手（不跑 GDB/OpenOCD/scons/pymavlink）

---

## 项目总览

```
┌─────────────────────────────────────────────────────────────────┐
│                  RTT 移植工程 — 4 份 Plan                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Plan ① 驱动 CMSIS 化 ─────────── Plan ② --board 自动发现         │
│  （寄存器级，去 HAL、去 CherryUSB）      （scons 构建系统改造）         │
│          │                                    │                  │
│          └──────────┬──────────┬──────────────┘                  │
│                     │          │                                 │
│                     ▼          ▼                                 │
│  Plan ③ CCP Daemon 24/7 闭环      Plan ④ 多Agent 并行编排           │
│  （探针→诊断→派工→验证→循环）      （28员工调度+标准化链+巡检）        │
│                     │          │                                 │
│                     └──────────┴──────────────┐                  │
│                                                ▼                  │
│                RTT ArduPilot 自主运维                            │
│          一键 `scons --board=cuav_v5` → MAVLink 心跳            │
│          daemon 24/7 自动检测 → 自动修 → 自动验证                 │
│          CEO 只做战略决策，不碰 terminal                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 改动量汇总

| Plan | 文件数 | 净改动（行） | 所需时间 |
|------|--------|------------|---------|
| ① 驱动CMSIS化 | ~15 | ~1200 | 2-3 周 |
| ② --board自动发现 | ~8 | +129 | 1-2 天 |
| ③ CCP Daemon闭环 | 1(+额外) | +450 | 1 周 |
| ④ 多Agent编排 | 0(仅巡检脚本) | +150 | 3 天 |
| **总计** | **~24** | **~1929** | **3-4 周** |

---

---

## 无人值守差距分析：为什么还不能100%自治？

### 三个维度量化（当前 15% → 目标 90%）

```
自治度 = 无人介入的情况下，系统能自动维持运行多久
当前: 约 30 分钟（一个PoC探针周期）
目标: 24 小时（无人介入，固件死了自己修、员工死了自己重派、通知自己推）
```

### 7 个差距项（从大到小排列）

| # | 差距 | 当前状态 | 为什么没做到 | 难度 | 解决后贡献 |
|---|------|---------|------------|------|----------|
| 1 | **CCP daemon 不完整** | 只有 probe，没有 `diagnose→dispatch→build→flash→verify` | Plan ③ 没落地。PoC 写完就停了。 | 中 | +25% |
| 2 | **cron 全部暂停** | 12 个任务全部 paused | 之前几个 cron 互相冲突/死循环/无限创建任务，全部手动暂停后没恢复 | 低 | +10% |
| 3 | **CEO 自动巡检没开** | 没有 3 分钟的 kanban 自动清扫 | 写了脚本 `kanban_autopilot.py` 但没设成定时 | 低 | +10% |
| 4 | **编译→烧录→验证没固化** | scons/OpenOCD/telnet 每次手动发 | 试过固化脚本但执行时环境不一致（workdir 不对、路由码不对） | 低 | +10% |
| 5 | **部分 ce-* profile 不存在** | dispatcher 派单但 worker 不干活 | 加到 config.yaml 但忘了建 profile 目录，dispatcher 静默跳过 | 低 | +10% |
| 6 | **ST-Link/USB 断开不会自愈** | 断开了就死在那 | xhci reset 命令已知但没集成到巡检脚本 | 低 | +5% |
| 7 | **飞书通知不完备** | 只在对话中推，没有统一通知通道 | kanban notify-subscribe 配了但没全链覆盖 | 低 | +5% |

### 为什么是 85% 不是 100% — 硬边界

```
░ 可自动化（85%）    ██ 硬边界（15%）

██████████████████████████████████░░░░░░░░░░░░░
85% automatable                 15% not

硬边界（永远无法无人值守）：
  ① 新硬件接入（换飞控板、换传感器型号）→ 需要人定义 hwdef.dat
  ② ST-Link 物理脱落 → 人必须去插线
  ③ 架构级决策（换 RTOS、换构建系统）→ 需要人拍板
  ④ daemon 自身挂了（CPU 100%、OOM、内核崩溃）→ 人必须重启系统
  ⑤ 编译环境变化（SDK 升级、工具链换版本）→ 需要人修环境
```

### 最短路径：从 15% → 85%（7 天）

```
Day 1 ─ 恢复 cron（30min）
       ─ CCP daemon 启动 --tick（30min）
       ─ 飞书通知打通（30min）
       → 每5分钟自动探针，坏了自动通知 ✅

Day 2 ─ 固化 build→flash→verify 脚本（2h）
       ─ L1-L4 多层次验证（2h）
       → 死了自动重烧、自动验证 ✅

Day 3 ─ 诊断→kanban 派工（2h）
       ─ done marker + 自动触发 build→flash→verify（2h）
       → 固件死了：自动诊断→自动派工→自动修复→自动验证 ✅

Day 4 ─ CEO 自动巡检 cron（1h）
       ─ ce-* profile 补齐（1h）
       ─ ST-Link 自动恢复脚本（1h）
       → 员工崩溃自动重派、ST-Link 断开自动重连 ✅

Day 5-7 ─ 稳定运行测试
        ─ 连续 72h 无人值守运行
        ─ 观察异常、补漏
        → 彻底放手 ✅
```

### 执行计划（精确到动作）

| 天 | 动作 | 涉及文件 | 预期产出 |
|----|------|---------|---------|
| 1 | `cronjob resume` 全部 12 个 cron | - | cron 恢复 |
| 1 | `ccp_daemon.py --tick` 注册 cron | `ccp_daemon.py` | 每5分钟探针 |
| 1 | 飞书 webhook 配到 daemon | `ccp_daemon.py` | 状态变更推飞书 |
| 2 | `_do_build()` `_do_flash()` 固化 | `ccp_daemon.py` | 自动编译→烧录 |
| 2 | `FunctionalVerifier` 集成 | `ccp_daemon.py` | L1-L4 自动验证 |
| 3 | `_diagnose_and_dispatch()` | `ccp_daemon.py` | 自动诊断→派工 |
| 3 | done marker + build trigger | `ccp_daemon.py` | 自动完工→编译→烧录→验证 |
| 4 | `ceo_patrol` cron 注册 | 新建巡检脚本 | 每3分钟自动清扫 |
| 4 | 创建缺失 ce-* profile 目录 | - | dispatcher 能派单 |
| 4 | xhci reset 脚本集成巡检 | 巡检脚本 | ST-Link 断开自动恢复 |

### 验收标准：24h 无人值守

```
┌─────────────────────────────────────────────┐
│  ✅ 24 小时无人值守验收清单                    │
├─────────────────────────────────────────────┤
│  ☐ 1. cron 全部恢复，每 5 分钟正常 tick       │
│  ☐ 2. daemon probe 不报错（OpenOCD 稳定）     │
│  ☐ 3. 固件正常时 daemon 输出 L1-PASS 不停     │
│  ☐ 4. 固件死了 → 自动创建 kanban task         │
│  ☐ 5. kanban task 被 ce-* worker 自动 claim   │
│  ☐ 6. worker 完成后 → 自动 build→flash→verify │
│  ☐ 7. 验证通过 → daemon 回到 L1-PASS 循环      │
│  ☐ 8. ST-Link 断开 → 自动 xhci reset 恢复     │
│  ☐ 9. 所有状态变更 → 飞书自动通知               │
│  ☐ 10. 员工崩溃 → CEO 巡检自动解阻塞重派       │
│  ☐ 11. 连续 24h 无人介入                       │
└─────────────────────────────────────────────┘
```

### 为什么之前做不到？

不是技术原因，是**执行原因**：

1. **每次做到一半就被打断** — 编译出问题 → CEO 亲自修 → 修完忘了恢复 cron
2. **写完不启动** — PoC 脚本写了、自动巡检脚本写了、但没设成定时跑
3. **出问题就停掉** — cron 死循环 → 全部暂停 → 再也没恢复
4. **每次开启新的就不管旧的** — 新问题来了 dispatch 新员工，旧 cron 没人管

**解决方法**：这次一次性全部配上，配完后 7 天不动任何 cron。除非系统崩溃不介入。如果 cron 出问题 → daemon 自动检测 → 自动恢复 → 飞书通知。不再手动暂停。

---

## 自循环跟踪（2026-05-25 部署，自动维护）

### ✅ 已完成的修复
- [2026-05-25] SPI1 GPIO 引脚修复（三处+board init）
- [2026-05-25] SPI CR2 FRXTH 修复
- [2026-05-25] ADC DMA CMSIS 重写
- [2026-05-25] 启动顺序 5 步对齐 ChibiOS
- [2026-05-25] 自循环基础设施（daemon + heartbeat + extracted/ + master skill）

### 📋 当前待办
1. SPI1 GPIO board init 被 gpio->init() 覆盖
2. main_loop == 0（wait_for_sample 卡住）
3. hwdef 传感器定义不对齐

### 📚 经验教训
- lesson-2026-05-25-spi-gpio-wrong-pin — SPI1 GPIO 三处同源 bug

### 🚧 风险点
- GPIO MODER 被 gpio->init() 覆盖
- 引脚定义三处不一致
- cron 手动暂停→watchdog 兜底