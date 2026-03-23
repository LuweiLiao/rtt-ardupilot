# Agent 排障 / 实现轨迹（追加式）

本文件由 Cursor 按 `.cursor/rules/autonomous-debugging.mdc` 约定**追加**记录；每条对应一小步，便于回溯。勿写入密钥与隐私。

---

### 2026-03-23 AP_HAL_RTT 飞行相关完整性审计（对比 ChibiOS）

- **动作**：枚举 `libraries/AP_HAL_RTT/*.cpp|*.h`；精读 RCOutput/RCInput/AnalogIn/GPIO/Storage/system/Util/HAL_RTT_Class/Scheduler；`wc -l` 对比 `AP_HAL_ChibiOS` 同名文件；核对 `rtt_bsp_cuav_v5/.config` 的 `RT_USING_PWM`/`RT_USING_ADC` 与 `hwdef/cuav_v5/hwdef.dat` 的 `HAL_WITH_RAMTRON`。
- **依据**：代码与配置事实；ChibiOS `Scheduler.cpp` 调用 `((AnalogIn*)hal.analogin)->_timer_tick()`，RTT Scheduler 无对应调用。
- **结果**：RCOutput `_write_hw` 为空壳；CUAV V5 配置下 ADC/PWM 均未启用；Storage 为 RAM Stub（`HAL_WITH_RAMTRON 0`），重启丢参数；AnalogIn `_timer_tick` 未被调度。
- **下一步**：若需实体飞行，优先接 PWM（RT 设备 + `_write_hw`）与参数持久化（RAMTRON 或 Flash backend）；Scheduler 增加 `hal.analogin` 采样节拍（与 ChibiOS 对齐）。

### 2026-03-23 Cursor Skill：RTT CUAV V5 编译/烧录/调试

- **动作**：新增 `.cursor/skills/rtt-build-flash-debug/SKILL.md`（frontmatter `name`/`description`，中文正文；编译→刷写→调试→USB/MAVLink 工作流；路径对齐根目录 `SConstruct` 的 `build/rtt_deploy/cuav_v5/` 与 `RT_TICK_PER_SECOND` 取自 `rtt_bsp_cuav_v5/rtconfig.h`）。
- **依据**：用户要求汇总 scons/OpenOCD/GDB/usbipd/0x08008000 等命令；交叉核对 `SConstruct`、`rtt.py`（`RTT_ROOT`）、`hwdef.dat`、`cursor_ardupilot_at32_gd32.md` 片段。
- **结果**：Skill 已写入，正文少于 500 行。
- **下一步**：无（交付物为 Skill 文件）。

---

### 2026-03-19 OpenOCD + ST-Link 复位飞控

- **动作**：新增 `scripts/openocd_stlink_f7_reset.cfg` + `scripts/openocd_reset_cuav_v5.sh`（`-c "init" -c "reset run" -c "exit"`）；`README_MAVLINK_WSL2.md` §3.1 / §2 写明：**有 ST-Link 时优先用 OpenOCD 硬件复位**，不必依赖 MAVLink；并说明须先结束其它占用适配器的 openocd。
- **依据**：用户指出可通过 OpenOCD 重启飞控（SWD），与「仅软重启脚本」互补。
- **结果**：文档与一键脚本已对齐 CUAV V5 / STM32F767。

---

### 2026-03-19 ArduPilot 名称端口 + 软重启脚本

- **动作**：`check_ardupilot_com_win.ps1` 单独列出 **FriendlyName 含 ardupilot（不区分大小写）** 的端口，并解析 **建议 MAVLINK_COM**；新增 `-RequireArduPilotName`（无则退出码 4）。新增 `scripts/reboot_fc_mavlink.py`（MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN）。README §3.1 说明：**无法由远程 AI 执行硬件复位**，软重启需链路已通。
- **依据**：用户要求「重启后检测 ArduPilot 名称端口」。
- **结果**：检测逻辑与可选软重启已入库；硬件操作仍由用户现场完成。
- **下一步**：Windows 运行 `check_ardupilot_com_win.ps1`；若需软复位且桥已开，WSL 运行 `reboot_fc_mavlink.py`。

---

### 2026-03-19 先检 COM 再桥接（用户建议）

- **动作**：新增 `scripts/check_ardupilot_com_win.ps1`（`GetPortNames` + `Get-PnpDevice -Class Ports` 列表）；`start_mavlink_bridge_win.ps1` 默认子进程预检 COM，不存在则退出并提示 **复位飞控/拔插 USB**；`README_MAVLINK_WSL2.md` §3.1 写明顺序「先端口 → 无则复位再检 → 再起桥 → WSL」。
- **依据**：无 COM 枚举时开桥无意义；应先硬件侧恢复枚举再测 MAVLink。
- **结果**：文档与脚本与用户流程对齐。
- **下一步**：用户在 Windows 实机 `-Com` 验证。

---

### 2026-03-19 Windows 串口桥 + 参数探测闭环

- **动作**：修正 `scripts/start_mavlink_bridge_win.ps1` 中 `$RepoRoot`（原误用双重 `Split-Path`，导致脚本路径错）；`mavlink_serial_tcp_bridge.py` 在 `tcp_to_serial` 写串口后增加 `ser.flush()`，减轻 CDC 缓冲未及时刷出。
- **依据**：桥必须从仓库根解析到 `scripts/mavlink_serial_tcp_bridge.py`；USB CDC 有时需 flush 才能让对方及时看到 MAVLink。
- **结果**：在 **未在 Windows 前台成功启动桥** 的情况下，WSL 执行 `wsl2_param_probe.py --master tcp:$WIN:5760` 得到 **`[Errno 111] Connection refused`** —— 属预期：无进程监听 `WIN:5760`（或防火墙拒绝）。**客观阻塞**：当前环境无法代替用户在 Windows 设备管理器确认 COM、并保持桥窗口运行。
- **下一步**：用户在 Windows 用 **设备管理器中的 COMx** 启动 §3 桥至出现 `Waiting for TCP client ...` 后，WSL 执行  
  `./scripts/wsl2_mavlink_bridge_smoke.sh`  
  或  
  `python3 scripts/wsl2_param_probe.py --master tcp:$WIN:5760 --duration 45`  
  以 **PARAM_VALUE count > 0** 为通过判据；仍为 0 则按 `README_MAVLINK_WSL2.md` §10 与 `README_DEBUG.md`（`initialised_params`、UART 带宽、`usb_connected`）逐项查。

---

### 2026-03-21 OpenOCD 调试时 MAVLink 参数读不出

- **动作**：确认 `rtt_debug_cuav_v5_verify.gdb` 在 `_main_loop_entry` 断点处 **`quit` 前未 resume**，CPU 保持 **halt** → 主循环/MAVLink 不处理 `PARAM_REQUEST_LIST`。在脚本末尾增加 **`monitor resume`**；`README_MAVLINK_WSL2.md` §10 增加「OpenOCD/GDB CPU 须运行」条目；`README_DEBUG.md` 补充说明；`wsl2_param_probe.py` 失败提示加入 GDB/halt 排查。
- **依据**：历史 trace 与 README 写明 verify 不做第 4 次 `continue`，等价于调参前飞控停住；与「OpenOCD 调试时拉不出参」现象一致。
- **结果**：仓库侧默认 verify 结束后恢复运行；仍 halt 时需用户自行 `continue`。
- **下一步**：实机先 Windows 起桥，再跑 `wsl2_param_probe.py`；若仍 0 条再查 `initialised_params`/带宽/独占 COM。

---

### 2026-03-21 RTT USB MAVLink 无参数 — 计划落地（代码 + 探测）

- **动作**：（1）本机执行 `python3 scripts/wsl2_param_probe.py --master tcp:127.0.0.1:5760 --duration 2`，无桥时 **退出码 3**（连接拒绝），脚本可用。（2）复核 `scripts/rtt_debug_cuav_v5_verify.gdb` 末尾已有 **`monitor resume`**（约第 40 行），避免 verify 后 CPU 停住。（3）新增 `scripts/gdb_rtt_check_params_ready.gdb`，打印 **`copter.ap.initialised_params`** / **`copter.ap.initialised`**，区分「boot 未完成丢弃 PARAM_REQUEST_LIST」与其它问题。（4）`UARTDriver::_begin()` 中 `rt_device_open` 改为 **`RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX`**（DMA 回退加 RDWR），与 `_timer_tick` 延期打开路径一致。
- **依据**：计划「RTT MAVLink 参数诊断」待办；CherryUSB CDC 字符设备上仅 `INT_RX` 可能导致写路径与延期路径不一致。
- **结果**：UART 修改已入库；实机 **PARAM_VALUE count > 0** 仍需 Windows 桥 + 飞控验证。
- **下一步**：实机拉参仍失败时：`source scripts/gdb_rtt_check_params_ready.gdb` 确认 `initialised_params`；并确认未在无 `resume` 下调试。

---

### 2026-03-23 GCS 参数发送路径调度频率（源码阅读）

- **动作**：阅读 `GCS_Param.cpp::queued_param_send`、`GCS_Common.cpp::update_send` / `deferred_message_to_send_index` / `set_ap_message_interval(MSG_NEXT_PARAM)`、`Copter.cpp` 中 `SCHED_TASK_CLASS(GCS::update_send)`；核对 `AP_Scheduler::run` 的 `interval_ticks` 计算。
- **依据**：用户要求分析 count/bytes_allowed/调用频率与 ~25 params/s 成因。
- **结果**：交付用户中文分析：30% 带宽公式 `link_bw * dt / 3333`、无流控每批 ≤5、`MSG_NEXT_PARAM` 间隔强制 100–1000ms、调度器 400Hz 任务在 181Hz 主循环下每 tick 可运行。
- **下一步**：无。

---

### 2026-03-23 — 主循环 170Hz→400Hz 根因分析与修复

**问题**：ArduPilot RTT 移植主循环只跑到 ~170Hz，目标 400Hz。

**根因分析（三层叠加）**：

1. **DeviceBus `_periodic_thread_entry` 架构错误**（最关键）
   - 旧实现：每个 `register_periodic_callback` 创建独立线程，循环 `cb(); rt_thread_mdelay(ms);`
   - 缺陷 a：实际周期 = callback执行时间 + mdelay → 漂移累积，1kHz SPI读 FIFO 实际变 500-770Hz
   - 缺陷 b：`adjust_timer()` 写 `ctx->period_usec` 但线程用的是开头算好的局部变量 `ms`，动态调速无效
   - 缺陷 c：CUAV V5 有 5 个 IMU，SPI1 上 5+ 个独立线程竞争，无共享总线调度

2. **`delay_microseconds_boost` 精度不足**
   - `wait_for_sample()` 的 check_sample 轮询每次调 `delay_microseconds_boost(100)`
   - 旧实现用 `rt_thread_delay(1)`（100µs tick），实际睡 100-200µs（tick对齐+上下文切换）
   - 400Hz 周期 = 2500µs，几轮多睡就拉到 5000-6000µs（170-200Hz）

3. **ChibiOS 对比**
   - ChibiOS `CH_CFG_ST_FREQUENCY = 1000000`（1µs 精度），chThdSleep 精确到微秒
   - ChibiOS DeviceBus 用绝对时间戳 `next_usec` 调度，一个总线一个线程

**修复（仅修改 RTT HAL 层，零改动 ArduPilot 核心代码）**：

1. **DeviceBus.cpp/h 重写** — 镜像 ChibiOS Device.cpp 的 bus_thread 架构：
   - 每个 DeviceBus 实例一个线程，管理 callback 链表
   - 绝对时间戳调度 `next_usec += period_usec`，callback 执行时间不影响周期
   - `adjust_timer()` 直接更新 callback 结构体的 `period_usec` + `next_usec`
   - 通过 `hal.scheduler->delay_microseconds()` 精确等待到下一次触发
   - 最小间隔 100µs 防 CPU 饥饿

2. **Scheduler.cpp `delay_microseconds_boost`** — 短延迟 yield+busy-wait：
   - ≤200µs：`rt_thread_yield()` 让出一次调度（DeviceBus 线程可运行），然后 busy-wait 剩余
   - >200µs：继续用 `rt_thread_delay(ticks)`

**预期效果**：
- DeviceBus IMU 回调真正 1kHz（不再受 SPI 读时间漂移）
- wait_for_sample 100µs 轮询精确到 ~100µs（不再被 tick 对齐膨胀到 200µs）
- 主循环应能达到 ~400Hz

---

### 2026-03-23 Cursor 持续化治理系统（规则 + Skill + 项目记忆 + Git）

- **动作**：新增 `.cursor/rules/session-bootstrap.mdc`、`project-memory-protocol.mdc`、`ap-hal-rtt-delivery-goals.mdc`、`git-milestone-policy.mdc` 与 HAL/BSP/hwdef 文件域规则；新增 `.cursor/project/` 下 `current-focus.md`、`status.md`、`open-issues.md`、`decision-log.md`、`command-catalog.md`、`board-matrix.md`、`milestone-plan.md`、`git-process.md`；新增 `.cursor/skills/` 下 `rtt-session-resume`、`rtt-mavlink-verification`、`rtt-bringup-checklist`、`rtt-root-cause-playbook`、`rtt-arch-refactor-playbook`、`rtt-git-milestone`。
- **依据**：当前仓库已有通用规则与单个 RTT 构建调试 Skill，但缺“会话入口协议、项目状态快照、专项 Skill、Git 里程碑模板”，容易导致上下文膨胀、命令失忆与换题跑偏。
- **结果**：已形成“规则负责流程、Skill 负责操作手册、`project/*.md` 负责稳定事实、`agent-trace` 负责时间线、Git 负责版本基线”的完整首版治理框架；当前 `CUAV v5` 作为稳定开发基线已被固化到项目记忆层。
- **下一步**：后续每个 milestone 提交前，先同步 `status/open-issues/decision-log/command-catalog/milestone-plan`；继续把新增稳定结论从 trace 升级到 `project/*.md`，避免再次依赖长聊天历史恢复上下文。

### 2026-03-23 AP_HAL_RTT 逐驱动验证文档层（driver-validation）

- **动作**：新增 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 与 `.cursor/project/driver-validation-matrix.md`；更新 `status.md`、`open-issues.md`、`board-matrix.md`、`milestone-plan.md`、`command-catalog.md`、`git-process.md`；扩展 `session-bootstrap.mdc`、`rtt-session-resume`、`rtt-bringup-checklist`、`rtt-root-cause-playbook`、`rtt-git-milestone`；新增 `rtt-driver-validation` Skill；在 `docs/AP_HAL_RTT_ARCHITECTURE.md` 中补入逐驱动验证层说明。
- **依据**：当前项目虽然已有 `CUAV v5` 整机可运行基线，但若继续只依赖整机 bring-up 来验证，会把驱动、总线、子系统、环境问题混在一起，不利于多板迁移。
- **结果**：形成了“host tests -> board examples -> subsystem smoke -> full vehicle milestone”的文档化验证金字塔，并把首批 `scheduler/uart/usb-cdc/spi-ms5611/imu-whoami/storage/pwm/analogin` 验证对象纳入矩阵与里程碑视图。
- **下一步**：后续若开始实现 examples/tests，优先从 `scheduler-smoke`、`uart-smoke`、`spi-ms5611-smoke` 起步，再把通过项逐步从矩阵状态升级为 milestone 判据的一部分。

### 2026-03-23 Bootstrap 成功率补强（统一入口 + 少读 + 单一事实源）

- **动作**：更新 `session-bootstrap.mdc` 与 `rtt-session-resume`，明确主 bootstrap 仅以 `current-focus -> status -> open-issues` 为第一入口，并加入 `AP_HAL_RTT` 对齐任务的专项补读、`decision-log + milestone-plan` 的并列读取、`agent-trace` 仅补读最近 20-50 行，以及 bootstrap 首轮默认不超过 3 个 project 文档；扩展 `project-memory-protocol.mdc` 加入“何种证据才可升级进 `status/open-issues/decision-log/command-catalog`”规则；更新 `rtt-chibios-alignment.md`，把 `.cursor/alignment-status.md` / `.cursor/alignment-issues.md` 明确降为专项档案而非主入口；为 `git-process.md`、`rtt-git-milestone`、`driver-validation-matrix.md`、`rtt-driver-validation` 加入权威文件说明；在 `decision-log.md` 固化“主入口唯一化”和“单一事实源”决策。
- **依据**：审计发现当前系统虽已有 bootstrap，但仍存在双入口、Skill 与规则轻微漂移、trace 读取边界不够硬、Git/driver-validation 多处重复说明等问题。
- **结果**：新 agent 进入仓库后更容易先读对的短文档，再按任务类型补读专项档案；`RTT-ChibiOS` 对齐不再和主治理系统形成双轨；稳定事实、命令、Git 流程、driver-validation 的职责边界更清晰。
- **下一步**：若后续继续强化，可考虑把 `.cursor/alignment-status.md` / `.cursor/alignment-issues.md` 的核心稳定结论进一步并入 `project/*.md`，但当前先保持“主入口 + 专项档案”的过渡形态。

### 2026-03-23 主循环卡死根因确认 + 修复（delay_microseconds busy-wait → rt_thread_delay）

- **动作**：仅修改 `Scheduler.cpp` 一个文件的 `delay_microseconds()` 函数：
  - 移除对 ≤100µs 短延迟的 busy-wait 分支
  - 移除对 sub-ms 余数的 busy-wait 分支
  - 统一使用 `rt_thread_delay(ticks)` 实现所有延迟
- **根因链**：
  1. RT-Thread main 线程优先级 `RT_MAIN_THREAD_PRIORITY=10`（rtconfig.h 配置）
  2. DeviceBus 回调线程的 fallback 优先级 `RT_THREAD_PRIORITY_MAX/3=10`（DeviceBus.cpp）
  3. 两者优先级相同（10），round-robin 调度，timeslice=20 ticks
  4. `wait_for_sample()` 调用 `delay_microseconds(100)` → busy-wait 100µs
  5. busy-wait 不会让出 CPU，主线程占满整个 timeslice（2ms）
  6. DeviceBus 回调线程在 timeslice 内得不到 CPU → SPI 读 IMU 无法完成
  7. `_have_sample()` 永不返回 true → 主循环在 ~195 次后永久卡死
  8. 修复后 `rt_thread_delay(1)` 主动挂起主线程 100µs，DeviceBus 立即获得 CPU
- **之前的误判**：反复调整 DeviceBus/主线程/boost/SPI_BUS 等优先级定义，均无效。根因不在优先级差异，而在同优先级下 busy-wait 阻塞 round-robin 的基本问题。
- **验证结果**：
  - 主循环频率：~410Hz（4094 iters/10s，连续 60s 稳定），超过 400Hz 目标
  - Setup 时间：~10s
  - USB CDC MAVLink：944/943 参数全部下载成功（25s 内完成）
  - MAVLink 消息类型确认：HEARTBEAT(0) + PARAM_VALUE(22) + STATUSTEXT(253) + TIMESYNC(111)
- **改动范围**：仅 `Scheduler.cpp::delay_microseconds()` 的 10 行代码
- **下一步**：将此修复 stage 到 git，更新 status.md 和 open-issues.md
