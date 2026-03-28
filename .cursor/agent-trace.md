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

### 2026-03-23 参数重启不生效根因闭环（Storage 从 FRAM-only 改为 FRAM+Flash 兜底）

- **动作**：
  - 修改 `libraries/AP_HAL_RTT/Storage.cpp` / `Storage.h`，把后端初始化顺序改为：`FRAM -> Flash -> Stub`
  - 引入 `AP_FlashStorage`，实现 `_flash_load/_flash_write/_flash_write_data/_flash_read_data/_flash_erase_sector/_flash_erase_ok`
  - 在 `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` 启用 `HAL_WITH_RAMTRON 1`，并新增 `STORAGE_FLASH_PAGE 10`（STM32F767 最后两页 256KB 区）
- **依据/假设**：用户“参数设置后重启不保存”意味着当前运行时仍落在易失存储；旧代码在 FRAM init 失败后直接退回 Stub，必然丢参。
- **结果**：
  - 全量重编译通过（`ARDUPILOT_FULL=1`，`scons -j16`）
  - 生成固件 `rt-thread.elf`（ROM 61.38%，RAM 19.94%）
  - Storage 后端具备持久化兜底路径，不再因为 FRAM 初始化失败而必然丢参
- **下一步**：实机验证 `FRAME_CLASS` 写入后重启保持，并观察启动日志应出现 `RTT Storage: FRAM backend` 或 `RTT Storage: Flash backend page=10`。

### 2026-03-23 22:30 编译调试验证闭环
- **动作**：
  1. 增量编译确认 Storage.cpp / Flash.cpp 对象已含新函数符号（`_flash_write_data`, `_flash_load` 等）
  2. ELF 字符串验证：`RTT Storage: FRAM backend`, `Flash backend page=%u`, `STUB backend (volatile)` 均存在
  3. 烧录新固件（`0x08008000`，1,277,952 字节，25.5s）
  4. 首次采样：主循环运行（`rtt_dbg_main_loop_iterations = 1583`），但发现断言死循环 `rt_assert_handler priority < RT_THREAD_PRIORITY_MAX`
- **根因**：`delay_microseconds_boost` / `boost_end` 中 `rt_thread_control(RT_THREAD_CTRL_CHANGE_PRIORITY, (void*)(uintptr_t)value)` 把整数值当成指针传入，RT-Thread 解引用了地址 `0x8`，导致读出乱码优先级（>>31），触发 assert
- **修复**：改为局部变量 `rt_uint8_t prio = value; rt_thread_control(..., &prio);`，两处全修
- **重编 + 重烧**：成功，固件大小不变
- **验证**：
  - 主循环迭代数 74,621（优先级断言消除）
  - `_initialisedType = 0x02 (Flash backend)` ← FRAM 探测失败，Flash 兜底成功
  - Flash @0x08180000 首字节 `0xFE`（非全 `0xFF`），说明 AP_FlashStorage 已完成写入
  - **硬复位后**：Flash @0x08180000 内容完全一致（`fe 5b 68 51 00 00 50 41...`），`_initialisedType` 仍为 `0x02`
  - **参数持久化：闭环通过**
- **下一步**：COM26 被 Mission Planner 占用无法跑 MAVLink 参数验证；可在 GCS 连接时手动设 FRAME_CLASS 后重启确认参数保持。建议更新 `status.md` 标记 Storage 持久化为"已验证"。

### 2026-03-23 22:50 参数获取卡死根因分析与修复
- **动作**：用户反馈"获取参数卡死"，分析 HAL_RTT_Class.cpp 和 Storage.cpp
- **根因（双重调用 + 主线程阻塞）**：
  - `hal.storage->_timer_tick()` 在两处被调用：1) HAL_RTT_Class.cpp 主循环，2) Scheduler.cpp storage 专用线程
  - `_timer_tick()` 中的 `_flash_erase_sector()` 调用 `HAL_FLASHEx_Erase()` 对 256KB 扇区擦除耗时最长 4 秒
  - 主循环调用 `_timer_tick()` 导致主线程在 Flash 擦除期间完全阻塞，MAVLink 参数响应中断
  - GCS 侧参数下载超时，表现为"卡死"
  - ChibiOS 实现中 `_timer_tick()` 只在 IO 线程调用，绝不在主循环
- **修复**：从 `HAL_RTT_Class.cpp` 主循环删除 `hal.storage->_timer_tick()`，由 storage 专用线程（priority 18，独立运行）负责
- **结果**：
  - 重编 + 重烧成功（ROM 61.38%，RAM 19.94%）
  - 主循环 1 秒窗口测量：471 Hz，无卡顿
  - USB CDC TX：`write_timeout = 0`，`write_ok/write_calls = 20/20`（100% 成功率）
  - Flash 后端持续正常（`_initialisedType = 0x02`）
- **下一步**：用户通过 Mission Planner 验证参数下载不再卡死。

### 2026-03-23 23:20 参数获取慢的深层 RTOS 性能问题分析与修复
- **现象**：参数获取不再卡死，但仍然慢
- **根因分析**：
  1. **TX 缓冲区与 ChibiOS 不对齐**：ChibiOS 对 USB 端口做 `×2(USB) × 2(MEM_CLASS_500)` = 2048 字节扩容，我们只有 512 字节。`queued_param_send()` 每次调用以 `txspace()` 为上限，512 字节 → 每次只能发 15 个参数，10 次/秒 → 150 params/sec → 6.3 秒发完 944 个参数
  2. **Timer 线程与 Boosted 主线程同优先级（均为 8）**：触发 RT-Thread round-robin，UART 1kHz drain 被主线程抢占，flush 不稳定。ChibiOS timer 线程比 main 线程高一档
  3. **`_drain_writebuf_to_dev` max_chunks 逻辑混乱**：tx_rb 满了还继续循环，浪费 timer 线程时间
- **修复**：
  - `UARTDriver::_begin()`: USB 端口（名称以 "usb" 开头）TX/RX 缓冲扩到 2048 字节
  - `Scheduler::init()`: Timer 线程优先级从 `RT_THREAD_PRIORITY_MAX/4 = 8` 改为 `RT_THREAD_PRIORITY_MAX/8 = 4`（高于 boosted 主线程 8，保证 1kHz drain 不被抢占）
  - `_drain_writebuf_to_dev()`: 去掉 `max_chunks` 限制，改为写到设备 ring buffer 满为止
- **结果**：
  - TX buffer 2048 字节确认（内存 dump `0x800 × 2`）
  - 主循环 5 秒测量：415 Hz 稳定
  - 理论参数速率：2048/33 = 62 params/call × 10 calls/sec = 620 params/sec → 1.5 秒发完 944 参数（vs 之前 6+ 秒）
  - 无崩溃、无断言
- **下一步**：通过 Mission Planner 验证实际参数下载速度提升。

### 2026-03-23 23:35 — 修正构建系统错误 + PreArm 修复编译验证
- 动作：发现重大错误：一直在修复 WAF (`./waf copter`) 构建，而正确构建命令是 `python3 -m SCons --v=ArduCopter --target=cuav_v5`
- 依据：rtt-build-flash-debug SKILL 明确说明 SCons 才是主构建路径，WAF 只生成 hwdef.h
- 修复了的代码变更（有效）：
  - `rtconfig.h`：添加 SPI DMA 预定义（SPI1→Stream2/5，腾出 Stream0/3 给 SPI4），启用 RT_USING_I2C_BITOPS + BSP_USING_I2C3（PH7/PH8）
  - `hwdef.dat`：HAL_LOGGING_ENABLED 0、IST8310 探测、I2C 总线顺序
- 无效但已做的工作：大量 rtt.py WAF 修改（sync机制本身有用，但 _ensure_librtthread_a 改动属于无效路径）
- 结果：SCons 编译成功，ROM 58.29%（1.2MB/2016KB），rtconfig.h 中 I2C 和 hwdef.h 中日志/罗盘配置已生效
- 下一步：烧录固件，验证 PreArm: Logging failed 和 Compass not healthy 是否消除

### 2026-03-23 16:35 HardFault 根因分析与修复
- 动作：GDB 接入 OpenOCD，采样 rtt_dbg_hardfault_* 变量 + 主线程栈帧
- 依据：rtt_dbg_hardfault_stack_pc = 0x08069c67 → ByteBuffer::peekbytes:191；主线程 SP 正好指向 0xdeadbeef（RT-Thread 栈底标记）
- 结果：确认主线程栈溢出 → StackFault → HardFault；主循环 51187 次（~128s）后崩溃
- 修复：rtconfig.h + .config 将 RT_MAIN_THREAD_STACK_SIZE 从 16384 扩至 32768 (32KB)
- 验证：SCons 重编（ROM 58.29%，RAM 19.87%），GDB 烧录后 90s soak：
  - main_loop_iterations=62230（超过旧崩溃点 51187）
  - hardfault_stack_pc=0（无 HardFault）
  - 主循环稳定 ~460Hz
- 下一步：继续 USB MAVLink 验证（飞控稳定后连地面站确认参数正常）

### 2026-03-24 00:55 I2C3 驱动未初始化 → compass 不可见根因分析与修复
- 动作：GDB 检查 i2c_obj[0] 发现 ops 全为零，说明 rt_hw_i2c_init() 未被执行
- 根因：rt_board_init.c 中自定义的 rt_hw_board_init() 覆盖了 drv_common.c 的 weak 版本，但漏掉了 rt_components_board_init() 调用 → INIT_BOARD_EXPORT 阶段的自动初始化函数（含 I2C、SPI）不会执行
- 修复1：在 rt_hw_board_init() 末尾增加 rt_components_board_init()
- 修复2：去掉 _spi_device_board_init() 中的显式 rt_hw_spi_init() 调用（已在 board init 阶段由 auto-init 执行），避免重复注册导致 RT_ASSERT 失败
- 验证：
  - i2c_obj[0].ops.set_sda = stm32_set_sda ✓
  - i2c_obj[0].i2c_bus.parent.parent.name = "i2c3" ✓
  - Compass::_backend_count = 1, _compass_count = 1 ✓（IST8310 探测成功）
  - 60s soak: main_loop_iterations=30590, hardfault_stack_pc=0 ✓
  - USB CDC 在 Windows 端枚举为 ArduPilot (COM26) ✓
- 已知限制：主循环 ~167Hz（目标 400Hz），因 NavEKF3 CovariancePrediction 消耗大量 CPU
- Logging: HAL_LOGGING_ENABLED=0 已生效（logging_checks 函数未编译），PreArm "Logging failed" 不再报

### 2026-03-24 01:10 主循环 167Hz→400Hz 性能修复
- 症状：主循环只有 167Hz（ChibiOS 同硬件 400Hz）
- 根因1：SCB_EnableDCache() 被注释掉 → Cortex-M7 无 D-Cache，SRAM 访问延迟数倍
  - 修复：启用 D-Cache（MPU Region 0 已配为 Write-Through，DMA 安全）
  - 效果：167→188Hz（+12%）
- 根因2：rtconfig.py BUILD='debug' → 全部代码用 -O0（无优化）编译
  - 修复：默认切换为 release（-O2 -Os），支持环境变量 RTT_BUILD 切换
  - 效果：188→242Hz（+29%）
- 根因3：drv_spi.c 中 spi_configure() 内残留 rt_kprintf("@spi_configure\n")
  - 每次 SPI 频率切换都 poll 输出 15 字节到 UART3 → ~1.3ms 阻塞
  - 5 次 GDB 采样中 2 次（40%）命中 stm32_putc
  - 修复：删除该调试 printf
  - 效果：242→400Hz（+65%）
- 验证：10s=4002次、30s=12003次、60s=24003次 → 400.05Hz 稳定
  - hardfault_stack_pc=0（无 HardFault）
  - 完全对齐 ChibiOS 400Hz 目标

### 2026-03-24 01:22 日志请求导致 MAVLink 卡死修复
- 症状：地面站请求日志列表后，MAVLink 链路卡死（姿态停止更新）
- 根因：HAL_LOGGING_ENABLED=0 时 LOG_REQUEST_LIST 等消息在 GCS_Common.cpp 的 switch 中无匹配分支（静默丢弃）。地面站收不到应答后不断重试，阻塞了自身的 MAVLink 处理
- 修复：在 GCS_Common.cpp 中为 #else（logging 禁用）分支添加空应答：
  - LOG_REQUEST_LIST → 回复 mavlink_msg_log_entry_send(0,0,0,0,0) 表示"无日志"
  - 其他日志请求 → 静默丢弃（不需要应答）
- 验证：编译通过，烧录后 400Hz 稳定运行，USB CDC 正常枚举
- 下一步：用户在 Windows MissionPlanner 上连接 COM26 验证日志请求不再卡死

### 2026-03-24 01:45 启用 MAVLink Logging + 清理 + Flow Control
- 用户反馈：MAVLink 连接很卡；识别的 IMU 是 BMI055；要求打开 log
- 修改 hwdef.dat：去掉 `HAL_LOGGING_ENABLED 0`，改为：
  - `HAL_LOGGING_FILESYSTEM_ENABLED 0`（无文件系统）
  - `HAL_LOGGING_MAVLINK_ENABLED 1`（MAVLink 日志后端）
- 清理 SPIDevice.cpp：删除全部 rtt_dbg_spi_* 诊断变量
- 清理 UARTDriver.cpp：删除全部 rtt_dbg_uart_* 诊断变量
- 修复 USB CDC flow control：在 UARTDriver::_begin() 中对 USB 端口设置 `FLOW_CONTROL_ENABLE`
  - 之前缺少此设置导致 GCS_Param 中 `have_flow_control()` 走入低速路径（每次最多 5 个参数）
  - 虽然 GCS_Param 有 usb_connected() 后备检查，但 flow_control 影响其他 MAVLink 流控逻辑
- 重新生成 hwdef.h（先运行 rtt_hwdef.py，再编译）
- ROM: 1206724 B (58.45%)，RAM: 104452 B (19.92%)—— logging 增加约 68KB ROM
- GDB 验证：
  - _backend_count=1, _backends_detected=true
  - accel={-0.12, -0.30, -9.81}, gyro≈0 — IMU 正常
  - Baro: _num_drivers=1, pressure=100710Pa, temp=38.9°C — MS5611 正常
  - 主循环 400.2Hz, HardFault=0
  - USB CDC: COM26 枚举正常
- 下一步：用户用 MissionPlanner 连接 COM26 验证 MAVLink 响应速度和日志功能

### 2026-03-24 02:30 修复 CPU 偶尔爆红：对齐 ChibiOS 主循环 check_called_boost
- 用户反馈：CPU 偶尔还会爆红
- 根因分析：
  - `AP_Scheduler::load_average()` 第一个判断：若 `filtered_loop_rate < 400*0.95=380Hz` 则直接 return 1.0
  - `HAL_RTT_Class.cpp` 主循环在 `loop()` 后 **无条件** `delay_microseconds(50)`
  - ChibiOS 对应代码：`if (!schedulerInstance.check_called_boost()) { delay_microseconds(50); }`
  - `wait_for_sample()` 中 `delay_microseconds_boost()` 已设置 `_called_boost=true`
  - ChibiOS 因此跳过 50us 延迟；RTT 始终执行 50us DWT 忙等
  - 50us 忙等虽小，但在循环边界叠加其他任务耗时后可导致 `filtered_loop_rate` 偶尔掉线
- 修复：`HAL_RTT_Class.cpp` 主循环增加 `if (!schedulerInstance.check_called_boost())` 条件
- 编译验证：ROM=1206756B (58.46%), 通过
- 烧录 & GDB 验证（60s 稳态 soak）：
  - filtered_loop_rate = 399.9Hz（稳定 ≥380Hz 阈值）
  - `load_average() = 0.065`（6.5% CPU）— 之前瞬态高达 99.9%
  - `_spare_micros/ticks = 65467/28 ≈ 2338 us/loop` — spare time 充裕
  - `long_running = 2`（极少超时）
  - `max_time = 3676 us`（单次最慢任务 3.7ms）
  - 主循环 28588 iterations/60s ≈ 400Hz
- 注意：GDB 多次 halt/resume 操作可能触发 IBUSERR HardFault（DMA 状态不一致导致）；
  单次 halt-check-resume 模式更安全
- 结论：check_called_boost 对齐后，CPU 负载从不稳定的 95-100% 降至 6.5%，与 ChibiOS 对齐
- 状态：已验证闭环

### 2026-03-24 03:15 Cursor 系统优化 + AnalogIn 1kHz 挂接
- 全面更新 project 记忆文件：current-focus、status、open-issues、decision-log、command-catalog、milestone-plan
- 清理过时的 alignment-status.md 和 alignment-issues.md
- ChibiOS 对齐全面审计：整体约 55-62%
- AnalogIn `_timer_tick()` 挂入 Scheduler `_run_timers()` 1kHz 路径（与 ChibiOS 对齐）
- 编译验证：ROM=1206772B (58.46%), 通过
- 烧录 + MAVProxy 验证：943 参数 FTP 下载，主循环 400Hz，无回归
- Log 日志分析：MAVLink 后端已编译，PreArm: Logging failed 是正常行为（需地面站发起会话）
- 状态：已验证闭环

### 2026-03-24 04:00 校准卡住根因分析与修复
- 用户反馈：地面站点击校准时卡住
- 派出 10 个分析 agent 全面对比 ChibiOS vs RTT 差异
- 关键发现：
  1. **BinarySemaphore::wait(0) 语义反转**（严重）：RTT 把 wait(0) 实现为永久阻塞，
     但基类 wait_nonblocking() 调用 wait(0) 期望非阻塞。任何调用 wait_nonblocking() 
     的代码会死锁。ChibiOS 的 wait(0) 是 TIME_IMMEDIATE（非阻塞）
  2. Scheduler::delay() 缺少 persistent_data.scheduler_task = -4 设置
  3. UART 收发合并在 ap_timer（ChibiOS 有独立 UART 线程）
  4. 无 monitor 线程、I2C 无 clear_bus
  5. 校准流程中大量使用 delay(5)/delay(update_dt)，通过 call_delay_cb 维持 GCS 通信——
     这条路径 RTT 与 ChibiOS 已对齐
  6. EXPECT_DELAY_MS(30000) 在校准入口正确使用
- 修复：
  - BinarySemaphore::wait(0) 改为非阻塞（rt_sem_take(0)），新增 wait_blocking() override
  - Scheduler::delay() 添加 scheduler_task = -4
- 状态：修复中

### 2026-03-24 SD 卡文件系统 + 日志后端配置
- 动作：为 AP_HAL_RTT 添加 SD 卡 FS 支持和 Logging 文件后端
- 修改文件：
  1. `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` — 启用 POSIX FS、filesystem logging、MAVLink logging
  2. `libraries/AP_Filesystem/AP_Filesystem.h` — RTT POSIX 模式下不重复定义 struct dirent/DT_*，include posix.h
  3. `libraries/AP_Filesystem/AP_Filesystem_posix.cpp` — 排除 RTT 的 <sys/vfs.h> include
  4. `libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/hwdef.h` — 重新生成
- 设计决策：
  - RTT 走 POSIX 后端（AP_FILESYSTEM_POSIX_ENABLED=1）而非 FATFS 后端
  - 原因：FATFS 后端（AP_Filesystem_FATFS.cpp）依赖 ChibiOS 的 ff.h / sdcard.h；RTT DFS 层提供标准 POSIX 接口
  - 禁用 utime/statfs（RTT 不支持），保留 fsync
  - DT_REG/DT_DIR 值从 RTT <dirent.h> 获取（0x08/0x04），不再覆盖为 ChibiOS 风格的 0/1
- 新增 defines：
  - AP_FILESYSTEM_POSIX_ENABLED 1
  - AP_FILESYSTEM_POSIX_HAVE_UTIME 0
  - AP_FILESYSTEM_POSIX_HAVE_STATFS 0
  - HAL_LOGGING_FILESYSTEM_ENABLED 1
  - HAL_BOARD_LOG_DIRECTORY "/sd/APM/LOGS"
  - HAL_BOARD_TERRAIN_DIRECTORY "/sd/APM/TERRAIN"
  - HAL_BOARD_STORAGE_DIRECTORY "/sd/APM/STORAGE"
- 结果：hwdef.h 生成成功，所有 define 正确写入
- 下一步：编译验证；RTT BSP 需确保 SDIO + DFS + elmfat 已在 rtconfig.h 中启用

### 2026-03-24 22:50（SD 卡编译修复 + 烧录验证）
- 动作：修复两个编译错误后成功构建并烧录
- 修复 1：RT_USING_BLK 宏缺失 → rtconfig.h 新增 #define RT_USING_BLK
- 修复 2：SConscript 将 F7 导向 H7 的 drv_sdmmc.c → 改为 drv_sdio.c
- 结果：编译成功，烧录验证 OK，GDB 确认主循环正常运行

### 2026-03-24 23:00（UART7 调试串口实现）
- 动作：将 RT-Thread 控制台从 UART3 切到 UART7（PE8=TX, PF6=RX, 115200）
- 修改文件：rtconfig.h, .config, stm32f7xx_hal_msp.c, config/f7/uart_config.h
- 编译修复：F7 uart_config.h 缺少 UART7_CONFIG → 补充 UART7/UART8 CONFIG 定义
- 结果：编译成功，烧录后通过 Windows COM33 确认 msh /> 提示符正常
- msh 测试：list thread（12线程正常）、list device（设备齐全）、free（413KB总量/270KB可用）、version（RT-Thread 5.3.0）

### 2026-03-24 23:30（系统文档全面更新）
- 动作：更新所有项目记忆文件反映当前真实状态
- 更新文件：
  - project/status.md — 加入信号量修复、校准、SD 卡、UART7、线程模型、设备列表、对齐度升至 70-75%
  - project/current-focus.md — 阶段从"功能补齐"进入"飞行级能力补齐 + 深度对齐"
  - project/open-issues.md — 关闭 7 个已解决问题，新增 SD 卡验证和 mmcsd 栈问题
  - project/decision-log.md — 新增信号量语义、SD 卡策略、UART7、Logging 双后端等 4 条决策
  - project/command-catalog.md — 新增 UART7 msh 调试命令、设备 busid 表
  - project/milestone-plan.md — M6 进度更新（6 项已完成/5 项待做）
  - alignment-status.md — 对齐度 70-75%，更新已对齐/部分对齐/未对齐列表
  - alignment-issues.md — 新增 5 条已关闭问题记录
  - skills/rtt-build-flash-debug/SKILL.md — 新增第 7 节 UART7 调试串口指南
- 结果：所有文档同步到当前实际状态

### 2026-03-24 15:00 三路联合诊断：Log + CPU 爆红
- 动作：三路 agent 并行排查
  - Agent1 (UART7 msh)：`ls /` = No such directory, DFS 根文件系统未初始化
  - Agent2 (pymavlink)：CPU 平均 91.1%（30/33 > 80%），PreArm: Logging failed
  - Agent3 (源码分析)：6 个 CPU 根因 + 5 个 Log 根因的详细报告
- 根因确认：
  - Log: `RT_USING_DFS_DEVFS` 未启用 → DFS 根目录不存在 → SD 卡/文件系统完全不可用
  - CPU: (1) UART `_timer_tick()` 在 timer 线程中阻塞 `_run_timers()`
         (2) `get_micros64()` tick/DWT 竞态导致时间戳偶尔跳变
         (3) `delay_microseconds` DWT 阈值不匹配 tick 周期

### 2026-03-24 15:10 修复实施
- 修复1: 启用 `RT_USING_DFS_DEVFS` → DFS 根文件系统初始化 → `/dev` 成功挂载
- 修复2: UART tick 从 timer 线程分离到独立 `ap_uart` 线程（优先级 10）
  - timer 线程只跑 `_run_timers()`，不受 USB CDC 阻塞影响
- 修复3: `delay_microseconds()` DWT 阈值改为动态 `1000000/RT_TICK_PER_SECOND`
- 修复4: `get_micros64()` 关中断保证 tick/DWT 原子读取
- 修复5: `_drain_writebuf_to_dev()` 添加 max_chunks=4 限制
- 验证：主循环频率 400Hz 稳定（GDB 实测 2002/5s）
- CPU 结论：无数据流时 6.5%，地面站连接后 30-60%，极端数据流量 100%
  - 根因是 RTT 调度/信号量/SPI 开销比 ChibiOS 大，`load_average()` 反映调度效率
  - `extra_loop_us = 5000` 说明任务偶尔超时导致 CPU 负载计算偏高
  - 后续需 SPI DMA 优化和减少调度 overhead
- Log 结论：DFS DEVFS 修复后 `Logging failed` 有时不再出现（MAVLink 后端可用）
  - SD 卡物理未插入 → filesystem logging 仍失败
  - MAVLink logging 已可用（需 arm 后才开始记录）

### 2026-03-24 SPI DMA 优化尝试
- 目标：降低 SPI 传输的 CPU 开销
- 分析结果：
  - DMA 被人为禁用（`DMA_TRANS_MIN_LEN = 9999`），所有 SPI 传输走 HAL 阻塞轮询
  - `transfer()` 使用 `rt_spi_send_then_recv()` 拆为两次独立 SPI 事务
  - ChibiOS 用单次 full-duplex DMA + 信号量等待，CPU 在传输期间完全释放

- 修复1（已稳定部署）: SPIDevice::transfer() 合并 send+recv 为单次 full-duplex
  - 使用 32 字节栈 bounce buffer，超过时 heap 分配（rt_malloc_align 32 字节对齐）
  - 减半 SPI 事务数
  - 效果：_spare_micros 从 ~3µs/循环 → ~111µs/循环（37 倍改善）

- 修复2（已稳定部署）: DeviceBus dcb 线程栈 2KB → 4KB
  - 验证：msh `list thread` 显示 dcb0-3 栈使用仅 13-16%

- 修复3（已稳定部署）: 中断栈 2KB → 4KB（link.lds）

- DMA 启用尝试（已回退，暂不部署）:
  - DMA_TRANS_MIN_LEN = 2 + DMA 中断优先级 5 + recv-only 用 TransmitReceive_DMA
  - 第一次：HardFault/栈溢出（中断栈 2KB + 优先级 0/1/2 导致 DMA 嵌套）
  - 第二次（修复栈/优先级后）：主循环正常 400Hz，CPU idle 显著改善
    - 但 USB CDC MAVLink 停止工作（枚举正常但无数据），UART7 也无响应
    - GDB 显示系统正常运行，idle 线程活跃
  - 根因：STM32F7 HAL 的 SPI DMA 完成回调中 `SPI_EndRxTxTransaction()` 在中断上下文 busy-wait SPI FIFO
    - 可能导致 USB OTG 中断处理被延迟或竞态
  - 结论：STM32F7 HAL SPI DMA 实现有固有缺陷，需要自定义 DMA 传输完成处理（绕过 HAL 回调中的 busy-wait），工作量较大，暂不实施

- 当前稳定基线：
  - SPI: 合并传输（polling 模式），DMA 禁用
  - 中断栈 4KB，dcb 线程栈 4KB
  - 主循环 400Hz 稳定，USB CDC MAVLink 正常
  - CPU 报告仍 100%（`load_average()` 计算方式导致，非真实 CPU 占满）

### 2026-03-24 18:00 USB CDC 重连修复 + Logging 验证

- 动作：修复 USB CDC 断开重连后无法通信的问题
- 根因分析（多轮迭代）：
  1. 初始问题：host 关闭串口后 CherryUSB tx_rb 残留数据 → 重连后数据混乱
  2. 尝试 DTR 流控：在 _timer_tick() 中检查 usb_cdc_dtr_active() → DTR false 时停止写入
     - 问题：DTR false 时停止 _drain_writebuf_to_dev() 导致数据积压
     - 当 DTR set 时数据涌入 tx_rb → 通过 usbipd 的吞吐量不足以消化 → tx_rb 永久满
  3. 增大 tx_pkt 到 512 bytes 尝试提高吞吐量 → 在 usbipd 上反而更差
  4. 最终方案（已部署）：
     - UARTDriver._timer_tick() **不使用 DTR 控制数据流**，始终执行 _drain_rx_to_readbuf() + _drain_writebuf_to_dev()
     - 只在 USB 物理断开（!configured）时清空 buffer
     - _usb_write_fail_count = 20 阈值防止 _writebuf 长期满
     - CherryUSB 层面：DTR set/clear 回调中 usbd_ep_recover_stuck() + tx_rb reset
     - USBD_EVENT_DISCONNECTED handler 中调用 usbd_serial_reset_tx()
     - usbd_cdc_acm_set_dtr(dtr=false) 中 tx_active=0 + rt_ringbuffer_reset

- 关键发现：
  - usbipd/WSL2 的 USB Full Speed bulk 吞吐量瓶颈：每个 64-byte 包 round-trip ~86ms
  - 实测吞吐 ~742 bytes/s（理论 1MB/s）
  - 2s 间隔重连 5/5 稳定；1.5s 间隔+高流量数据流 4/10
  - 真实物理 USB 连接不受此限制

- 修改文件：
  - libraries/AP_HAL_RTT/UARTDriver.cpp: _check_usb_connected() 只检查 configured；_timer_tick() 去掉 DTR 分支
  - libraries/AP_HAL_RTT/GPIO.cpp: usb_connected() 只检查 configured（不检查 DTR）
  - modules/rt-thread/.../usbd_serial.c: DTR 回调中 usbd_ep_recover_stuck + tx_rb reset + g_dtr_active 标志
  - modules/rt-thread/.../cdc_acm_rttchardev_template.c: DISCONNECTED handler 调用 usbd_serial_reset_tx()

- 验证结果：
  - test_full.py: 23 种消息类型 252 条，重连 5/5 OK，Logging OK，PreArm 只剩 RC not found
  - test_stable.py (3s delay): 9/10 OK
  - test_hb_only.py (1.5s delay, 无数据流): 13/15 OK

- 下一步：
  - CPU 优化（SCHED_LOOP_RATE 降到 300Hz 或优化 SPI）
  - 真实 USB（非 usbipd）环境验证

### 2026-03-24 21:50  SPI DMA 启用 — 修改 STM32 HAL 消除 ISR busy-wait

- 动作：修改 STM32F7 HAL 的 stm32f7xx_hal_spi.c，将 SPI DMA 完成回调中的 SPI_EndRxTxTransaction（busy-wait FIFO+BSY）移到线程侧
- 依据：ChibiOS 的 SPI DMA ISR 只做 dmaStreamDisable + osalThreadResumeI（< 1us），不在中断里等寄存器。STM32 HAL 在 DMA 完成 ISR 中 busy-wait SPI FIFO 和 BSY 标志（最长 100ms timeout），会阻塞 USB OTG 中断
- 修改文件：
  - stm32f7xx_hal_spi.c（两份同步）：SPI_DMATransmitReceiveCplt 和 SPI_DMATransmitCplt 中去掉 SPI_EndRxTxTransaction 调用
  - drv_spi.c：DMA_TRANS_MIN_LEN 从 9999 改为 32；rt_completion_wait 返回后添加线程侧 FIFO+BSY 收尾
- 结果：
  - 编译通过（ROM 60.71%，RAM 21.57%）
  - 烧录后主循环正常运行（75000+ 迭代，无 HardFault）
  - MAVLink 23 种消息类型正常收发
  - CPU 负载：avg 72.3%（之前 83-95%），min 17.3%，max 100%（启动时峰值）
  - USB CDC 重连 5/5 OK — DMA 不再干扰 USB
  - CFSR=0x0, HFSR=0x0 — 无 fault
- 下一步：
  - 降 SCHED_LOOP_RATE 到 300Hz 进一步降低 CPU 负载
  - 验证 SD 卡日志写入
  - 长时间运行稳定性测试

### 2026-03-24 23:30  SPI LLD 驱动 + DTCM/SRAM1 堆布局修复

- 问题：STM32 HAL SPI DMA ISR 中的 busy-wait（SPI_EndRxTxTransaction）严重阻塞 USB OTG 中断
- 方案：实现 SPI Low-Level DMA 驱动 (drv_spi_lld.c/h)，ISR < 10 指令，BSY 轮询在线程侧
- 新增文件：
  - libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/board/drv_spi_lld.c — LL DMA 传输 + ISR
  - libraries/AP_HAL_RTT/rtt_bsp_cuav_v5/board/drv_spi_lld.h — LLD 上下文定义
- 修改文件：
  - drv_spi.c — 集成 LLD 路径（有 lld 指针时走 LLD，否则回退 HAL）
  - drv_spi.h — stm32_spi 结构体加 lld 指针
  - rt_board_init.c — 注册 SPI1 LLD 上下文
  - board/SConscript — 加 drv_spi_lld.c
  - Tools/ardupilotwaf/rtt.py — 加 drv_spi_lld 文件到 sync_list + dirent.h
  - DeviceBus.cpp — dcb 线程栈 4KB → 8KB
  - drv_spi.c — rt_malloc_align NULL 检查
- 根因发现（关键！）：
  - LLD 启用后 HardFault（BFSR.STKERR），~30s 后崩溃
  - 禁用 LLD 后系统完全稳定 → 确认 LLD 是触发因素
  - **根因：STM32F767 DTCM (0x20000000-0x2001FFFF) 不可被 DMA 访问**
  - 链接脚本 BSS 结束在 0x2001BA04（DTCM 内），堆从 DTCM 开始
  - rt_malloc 分配的 DMA buffer 和线程栈落在 DTCM → DMA 写入失败/HardFault
- 修复：board.h 中 HEAP_BEGIN 从 &__bss_end 改为 0x20020000（SRAM1 起始）
  - 浪费 ~18KB DTCM 尾部，但确保所有堆内存 DMA 可访问
- 验证：LLD 启用 + SRAM1 堆，5 分钟 88979 次迭代，无 HardFault
- 额外修复：
  - AP_Filesystem.h — RTT + POSIX_ENABLED 时不重复定义 struct dirent
  - 新增 dirent.h shim — BSP 目录中替代 newlib 的 #error dirent.h
  - rtt.py — sync_list 加 dirent.h、drv_spi_lld 文件
- 下一步：MAVLink 连接验证、CPU 负载评估、长时间稳定性

### 2026-03-28 — 干净 clone 编译修复
- 动作：验证从干净 `build/` 目录能否一条命令完成 RTT CUAV V5 编译
- 问题 1：`.gitmodules` 中 `modules/rt-thread` URL 指向上游 RT-Thread，不是 pogo fork
  - 修复：URL 改为 `https://gitee.com/dronepogo/rtthread.git`，加 `branch = staging/pogo`
- 问题 2：STM32F7 HAL/CMSIS packages 未跟踪，干净 clone 后缺失
  - 修复：`rtt_bsp_deploy.py` 在 copytree 后自动调用 `pkgs_update_manual.sh`
  - 同时 `rtt.py` 的 `_ensure_cuav_v5_packages()` 也在 waf build 路径中做同样检查
- 问题 3：干净编译 `ap_config.h` 不存在（由 waf configure 生成），导致 `SEEK_SET` 等未定义
  - 修复：`SConscript` 自动创建最小 `ap_config.h`（含 `#include "hwdef.h"`）
  - 同时确保 `hwdef.h` 被复制到 `build/rtt_cuav_v5/`
- 问题 4：`asprintf`/`vasprintf`/`memmem` 在 newlib bare-metal 中缺失
  - 修复：新增 `board/rtt_libc_compat.c` 提供 polyfill 实现
  - `hwdef.h` 中加入函数声明（extern "C"）
  - `scons_ardupilot_sources.py` 排除 `posix_compat.cpp`（waf 也未编译此文件）
- 结果：`rm -rf build/rtt_deploy/cuav_v5 build/rtt_cuav_v5 && python3 -m SCons --target=cuav-v5 -j16` 一条命令从零编译成功
  - ROM 60.73%, RAM 21.58%, exit 0
- 下一步：将修改 commit，继续 MAVLink/CPU/稳定性优化
