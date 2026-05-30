### Issue: Main loop never runs [RESOLVED 2026-04-08]
- **Root cause**: Flash storage erase (`Flash::erasepage()`) held interrupts disabled for 2-4 seconds during 256KB sector erase, starving RTT scheduler
- **Fix**: Only disable interrupts around STRT register write, then poll BSY with periodic `rt_thread_yield()`
- **Also fixed**: Linker script ROM region now correctly reserves 512KB for storage (pages 10-11)

### Issue: App "returns to bootloader" in free-run [RESOLVED 2026-04-12]
- **Root cause**: ArduPilot bootloader waits 5 seconds (bootloader(5000)) for firmware upload before jump_to_app(). All previous tests only waited 1-3 seconds before halting.
- **Evidence**: DTCM marker at 0x200000F0 unchanged after 3s (= app not started yet), overwritten after 12s (= app running). After 60s: rtt_dbg_main_loop_iterations=11279, VTOR=0x08008000.
- **Conclusion**: App runs perfectly. No HardFault, no watchdog, no crash.

### IOMCU ✅ 已实机验证通过 (2026-04-12 16:30)
- **ROMFS pipeline**: `rtt_hwdef.py write_ROMFS()` → `embed.py create_embedded_h()` → `ap_romfs_embedded.h` 成功嵌入 `io_firmware.bin`
- **hwdef.dat**: `ROMFS io_firmware.bin Tools/IO_Firmware/iofirmware_lowpolh.bin`
- **hwdef.h**: `HAL_HAVE_AP_ROMFS_EMBEDDED_H=1`, `HAL_WITH_IO_MCU=1`
- **UART8 链路**: PE0/PE1 AF8 → BSP UART8 → UARTDriver idx2 → AP_IOMCU
- **MAVLink 验证结果**:
  - SYS_STATUS: `MOTOR_OUTPUTS` present + healthy (bit 15)
  - RC_CHANNELS: 19 条消息，chancount=0（无RC接收器连接，正常）
  - 传感器: 3D_GYRO/ACCEL present, ABS_PRESSURE present+healthy, 3D_MAG healthy
  - 主循环: ~305/s 稳定
  - 气压计: 校准完成
  - EKF3: 双IMU初始化成功，AHRS EKF3 active
  - PreArm: 仅 "Motors: Check frame class and type"（正常，未配置frame）
- **构建**: ROM 93.15%, RAM 34.11%

### Current Status (2026-04-12)
- **Main loop**: ~305/s 稳定运行
- **Boot sequence**: bootloader 5s wait → jump_to_app → app init → main loop
- **Flash layout**: 0x08000000=bootloader(16KB), 0x08008000=app(~1.4MB)
- **Build/flash**: scons → openocd program bootloader + app
- **Serial**: `/dev/serial/by-id/usb-ArduPilot_CUAVv5_RTT_RTTUSB0001-if00` @ 57600 baud

### 全量验证前阻塞（AP_HAL_RTT 目录与 legacy 板）

- [x] HAL 根目录 stray 整树 BSP 已移走：`rtt_bsp_pixhawk6c_mini`、`rtt_bsp_fmuv2` → `libraries/AP_HAL_RTT/archive/stray-bsp/`（2026-05-28）
- [x] Git index 旧 BSP / `.bak` 幽灵项已 `git rm`；`archive/stray-bsp/` 与 USB clean set 已 **staged**（2026-05-28 子任务 2；**未 commit**）
- [x] 误拷贝 `AP_HAL_RTT/{class,common,core,osal,port,cherryusb}/`：**磁盘不存在**（2026-05-28 审计）
- [x] `libraries/AP_HAL_RTT/SConscript` cherryusb group 断点：**文件不存在**，inventory 项已过时
- [x] `rtt_usb_backend.py` + `thirdparty/cherryusb` vendor：**已纳入 clean set（staged）**
- [x] BSP `RT_CHERRYUSB_DEVICE` 与 HAL Cherry **双栈**：生产 CUAV 路径未启用 BSP 侧 Cherry 与 HAL 并存（`hwdef/common` deploy）
- [x] 根级 `hal_spi_lld.c` / `hal_spi_lld_rtt.c` 双份链接风险：**已从 glob 排除**（子任务 2 双构建 PASS）
- [x] 项目文档 `rtt_bsp_cuav_v5` 旧路径：**board-matrix / command-catalog / rtt.py 已修正**
- [ ] **pixhawk6c_mini / fmuv2 legacy 支持**：脚本已指向 `archive/stray-bsp/`，**未**实机验证，**未**迁移到 `hwdef/common`；fmuv2 仅 waf 路径，deprecated 维护
- [ ] **工作区非本次清理的大改**：仍有 unstaged 修改待拆分/单独提交（与 USB clean set staged 项分开）

### USB 栈合入与清理（2026-05-28 起）

- **主线候选**：CherryUSB（`RTT_USB_BACKEND=cherryusb`）— **主仓显式 backend L0 已闭环**（2026-05-28 gate exit 0）；**生产默认仍为 native**，未切换
- **备选**：TinyUSB — clean worktree L0 可过；主仓最小合入曾现参数阶段 USB 断开（并发/ISR），已定位补丁方向
- **已闭环（主仓 Cherry L0）**：
  - [x] 三类补丁合仓：RX 8×64B ring、UART 背压真断开才 clear、SPI 每总线 `rt_mutex`；`rtt_ar_archive.py` / TempFileMunge 并行构建
  - [x] `RTT_USB_BACKEND=cherryusb` 全量 scons `-j8` + 唯一 `OTG_FS_IRQHandler`
  - [x] L0 gate：`/tmp/cherryusb_main_l0_gate.sh --skip-build --skip-bl --json` → STANDBY、904/904 参数、30s 流、VTOR/CFSR/HFSR/IWDGRSTF 判据
- **半合入清理**（2026-05-28 子任务 2 已收口至 staged；**commit 仍待用户明确要求**）：
  - [x] `Tools/scripts/rtt_usb_backend.py` 与 `hwdef/common/SConscript` 选择器：**已 staged**
  - [x] 误拷贝 `libraries/AP_HAL_RTT/{class,common,core,osal,port,cherryusb}/`：**已不存在**
  - [x] `libraries/AP_HAL_RTT/SConscript`：**不存在**（无需修）
  - [x] vendor `thirdparty/cherryusb/`：**已 staged**；HAL 根无重复五件套
  - [x] BSP `RT_CHERRYUSB_DEVICE` 与 HAL Cherry 双栈：**未在生产路径并存**
- **Cherry 全功能回归与长稳**（L0 + MAVFTP **不能**代替全部回归）：
  - [x] 复跑 `tests/test_mavftp.py`（2026-05-28 CherryUSB 显式 backend **6/6 PASS**；真实文件 Create/Write/OpenRO/Read/Delete 已覆盖）
  - [x] 复跑 `tests/test_mission_protocol.py` smoke（2026-05-28 CherryUSB 显式 backend **PASS**）
  - [x] **全量验证前门禁区**（2026-05-28 晚）：`L0_system`/`L4_spi`/`L7_cherryusb_cdc` 构建 PASS；native + cherryusb 全量 ArduCopter 构建 PASS；L0 gate + MAVFTP + Mission 硬件 PASS（MAVFTP 首轮 4/6 瞬断，重试 6/6）
  - [x] **USB 软重连 / 参数多轮下载**（2026-05-29）：软重连 3 轮 ~0.01s 恢复；独立连接 +drain 5s 下全量参数 904×3 PASS；**物理拔插重连**仍未单独测
  - [x] **长流 soak（≥10min）**（2026-05-29）：600s/47880 msg/~80 msg·s⁻¹、全程 STANDBY、无静默、soak 后 fault=0/无 IWDG；TX 背压诊断计数 `rtt_uart_usb_diag_write_fails` 仍为观察项（非阻断）
  - [x] **分层测试并行 scons**：门禁脚本已固定**串行** `--test=`（command-catalog 已记）
  - [x] **MAVFTP 首跑**（2026-05-29 受控回归）：6/6 首轮即过；历史瞬断未复现，仍建议全量时独占 CDC
  - [x] **决策：`RTT_USB_BACKEND=cherryusb` 设为默认**（2026-05-29）：已设全量默认并提交 milestone `85c4f83b3e`；**合入 CI / push** 仍待用户明确
  - [ ] **方法论**：soak 后须「关闭→重开→drain 5s」再 `param_request_list`；同连接高吞吐流直接拉参数会不完整
  - [ ] CDC+MSC composite / 多接口未收口（若仍规划）

### MAVFTP 回归根因与 #1+#2 修复（2026-05-29，规划监督 × composer-2.5-fast 多代理闭环）

> **背景**：续接 jsonl 对话遗留的「MAVFTP 从 milestone `85c4f83b3e` 的 6/6 回归 + param.pck 垃圾 size 1330926404」blocker。经多轮只读调查 + 硬件实测逐层证伪/定位。

- **逐层证伪（均有证据）**：垃圾 size 1330926404 **不复现**（瞬态）；FRAM 持久化**闭环 PASS**（LOG_BITMASK 复位保持）；T3 param.pck **非代码回归**（10229 是 pack 正确完整长度，10944 仅 `count*12` 估值，相关文件相对 milestone **零 diff**）；`ap_rtt_posix_stat` shim **非稳定 bug**（T4 隔离曾 PASS、失败形态不一致）；I2C `clear_bus`-on-timeout + compass init 假设**证伪**（`HAL_I2C_CLEAR_ON_TIMEOUT` 默认 0、compass init milestone 已有）；USB 后端**未漂移**（默认仍 cherryusb，`.config.baseline` 关 `RT_USING_CHERRYUSB` 是 HAL 独占 OTG_FS 的设计使然）。
- **真根因（两路只读 diff 收敛 + 实测坐实）**：HEAD 即 milestone，CherryUSB 栈本体零 diff；回归来自 milestone 之后**未提交工作区**的两笔改动：
  1. `HAL_RTT_Class.cpp`(~255 主循环 / ~381 启动) + `Scheduler.cpp` `_poll_usb_if_active()`(~450)：把 `usb_lld_poll_rtt()` 包进 `rt_hw_interrupt_disable/enable` → **关 IRQ 期间 poll 阻塞 `OTG_FS_IRQHandler` 的 CDC TX 完成回调** → 背压、回复尾延迟 → MAVFTP 抖动。
  2. `UARTDriver.cpp`(~862-885)：删掉 milestone 的「连续 >500 次 drain 写 0 → `_writebuf.clear()`」背压泄放 → 连接态下队列单调增长。
- **修复 #1+#2（已落地工作区，未 commit）**：#1 回退为裸 `usb_lld_poll_rtt()`（不关 IRQ）；#2 恢复 `_usb_write_fail_count > 500 → _writebuf.clear()`（即使仍 connected）。
- **修复后实测**：单轮干净 `tests/test_mavftp.py` **R1=6/6**（= milestone 单轮验证同等水平）；T3 size 恢复正常 10944；`rtt_uart_usb_diag_clears>0`（背压泄放生效）、`fail_streak` 不再无限增长；CFSR/HFSR=0、VTOR=0x08008000、IWDGRSTF=0、无 HardFault。
- **仍未闭环（残留）**：**MAVFTP 背靠背连跑**未稳定 6/6×N——含两因：(a) 测试每轮重连/会话争用（fresh≈6/6 vs 10s 重连≈2/6），(b) 固件 FTP/SD 多轮背靠背 session/EOF 恢复残留（单持久连接 R1 后仍退到 ~3/6，T2/T3/T4 Nack err=6）。milestone 当时只验证**单轮 6/6**，未验证背靠背 N 轮，故残留属**超出 milestone 基线的额外强化项**，非"未达 milestone"。
- **下一步（待定）**：(A) 接受 milestone 同等水平，按 commit 拆分计划固化 #1+#2 + 工作区（待用户明确 push/CI）；或 (B) 继续攻 GCS_FTP 多轮 session/EOF 恢复（新子课题）。`#3 FRAM sync 写`已评估为**死路**（只在 param SET 触发，MAVFTP 不 set 参数）。

#### 背靠背残留：ChibiOS 对比 + 实机取证（2026-05-29，fd 耗尽已证伪 → CDC 背压坐实）

> 用 ChibiOS 对比法排查背靠背 MAVFTP 残留。静态对比头号嫌疑是「RTT `AP_FILESYSTEM_POSIX` 全局 DFS fd 表 `DFS_FD_MAX=16` + `opendir` 占 fd，劣于 ChibiOS `f_opendir` 独立 FatFS 池」。**GDB 取证证伪该假设**。

- **fd 耗尽证伪**：R1/R2/4 轮各 halt 点 `_fdtab.used=0`、`maxfd≤8`，从未接近 16；`@PARAM` 4 槽在 ResetSessions 后全 0。即便失败态见 `param[0].open=1`、异常 `ftp.fd=512`，fdtab 仍空（AP::FS fd 与 DFS 表计数不同步，但**非耗尽**）。
- **CDC TX 背压坐实（主因）**：`rtt_uart_usb_diag_write_fails` 18030→20169、失败轮 `fail_streak→71`；失败形态为 read timeout（如 offset 7887）、写后读回空、size 头污染（1330926404↔10944 交替）；T6 心跳恒 PASS、CFSR/HFSR=0 无 HardFault。`clears` 38→41（#2 背压泄放仍触发但不足）。
- **次要伴生**：T2 偶发 `err=6`（`gen_dir_entry` stat 失败假 EOF）、R4 目录条目乱码——属背压下传输/会话退化，非独立稳定 bug。
- **结论**：背靠背 N 轮残留 = **CherryUSB CDC TX 持续高吞吐背压**（同 `rtt_uart_usb_diag_write_fails` 长期观察项），**非** fd 泄漏/会话/stat 缺陷。GCS_FTP / AP_Filesystem_Param 为 ArduPilot 共享代码、相对 milestone 无 diff。
- **未改码**（取证后纪律性结论）：**不增 `DFS_FD_MAX`**（无 fd 打满证据）。真正的修复杠杆是 **CherryUSB CDC IN 端点队列深度**（当前 shim 仅 1 in-flight + 1 pending ×64B、TX1 FIFO≈16 words）——属 CDC TX 吞吐设计改动，单独评估。
- **待用户决策**：是否投入 CherryUSB CDC TX 吞吐改造（深化端点 pending 队列/FIFO）以提升背靠背稳定，还是接受单轮 6/6（= milestone）基线、把背靠背 N 轮列为已知 CDC 限制。

#### CDC TX ring+kick 修复（2026-05-29，已落地工作区、未 commit、显著改善）

> 据上条根因（pending 仅 1 槽 + 与 8192B `_writebuf` 断链 + producer 1kHz tick 与 ISR 解耦）实施修复。

- **修复内容（仅改 `libraries/AP_HAL_RTT/hal_usb_cherryusb_shim.c`）**：把单 1×64B pending 槽升级为 **32×64B shim TX ring**；新增 `cherry_tx_kick()`（`!busy && ring 非空` 时 peek→`cdc_tx_buf`→`usbd_ep_start_write` 一包）；`usb_lld_send_rtt()` 入 ring 后 kick；**`usbd_cdc_acm_bulk_in()`（TX 完成 ISR）连续 kick**，摆脱对 1kHz tick 的依赖。仍一次只在途一包、EPENA/busy 时不重 arm；新增 `rtt_dbg_cherry_tx_ring_{enqueued,dropped}`/`tx_kick_calls` 诊断。
- **实测改善（CherryUSB，st-flash 烧录）**：单轮 `tests/test_mavftp.py` **仍 6/6**（回归护栏未破）；背靠背 4 轮 **6/6·6/6·4/6·6/6**（修复前 3/4/3/2）、6 轮 **6/6·6/6·6/6·4/6·3/6·6/6**；`rtt_uart_usb_diag_write_fails` 由 ~20000 降到 **~2341**（约 10×）；`tx_start_fail=0`、`bulk_in≈tx_start_ok`（ISR 链式 kick 生效）；CFSR/HFSR=0、VTOR=0x08008000、无 HardFault；L0 HEARTBEAT+STANDBY+30s 流 PASS。
- **收尾增量（ring 32→64 + TX1 FIFO 64→128B）：已证伪并回退**——单轮护栏掉到 4-5/6、6 轮 5/4/6/5/3/2 反而更差（疑 TX1 FIFO 128B 副作用），按回退条件已退回 `ring32 + TX1=16` 基础版。
- **残留**：背靠背极端持续突发下偶有单轮 3-4/6（`ring_dropped≈2343`，CDC 吞吐抖动），已大幅改善但非 100% 全 6/6；进一步 FIFO 调优无效。判为当前 CDC TX 实践最优。
- **状态**：ring+kick 基础版**在工作区、未 commit**；可按用户决策像 #1+#2 一样本地固化。

#### 策略转向：自底向上重验（2026-05-29，反固着）

> 在 CDC 背靠背单点反复修复（裸poll→背压→ring→FIFO→多包→守卫）越改越糟、并一度把板子搞进 CDC/BL 乒乓后，用户要求**回到自底向上分步验证**。经验已沉淀为新 skill `.cursor/skills/rtt-systemic-escalation/SKILL.md`（含反固着升维规则、自底向上顺序、CDC/USB 系统级根因知识库）。

- **新主线**：L1 地基(flash/SD/SPI/I2C 各 `--test=` 上板 PASS) → L2 CDC 单独(echo) → L3 CDC+MAVLink(L0 gate) → L4 背靠背压力。任一层未绿不碰上层。
- **Phase 1 守卫改动**（EPENA 守卫 + busy 看门狗 EPDIS 恢复 + ring 背压）在工作区 `hal_usb_cherryusb_shim.c`（+111 行）**保留未提交**，待 L1-L3 验绿后在 L4 复验。
- **Phase 2 多包**（`CDC_TX_CHUNK_MAX=512` + `cherry_tx_ring_drain_to_buf` 合并多槽一笔发）已设计未实施。
- **已证伪死路**：native 默认化（当前树 app 无法驻留/BL 乒乓）；CDC TX1 FIFO 单独 128B（更差）。

#### 自底向上重验 L1-L4 结果（2026-05-29，分步验证）

> 按 `rtt-systemic-escalation` 自底向上逐层验绿，干净隔离崩溃域。

- **L1 地基（全绿）**：`D_storage`/`E_fram`/`E_sdcard`/`D_spi_hal`/`E_imu`/`E_ms5611`/`D_i2c_hal`/`E_ist8310` 8/8 上板 `RESULT: PASS`、CFSR/HFSR=0，**无任何最小固件出现 CDC/BL 乒乓或 HardFault** → flash/SD/SPI/I2C 地基稳固。
- **L2 CDC 单独（全绿）**：`L7_cherryusb_cdc` + `D_usb_serial` 枚举 + echo，30s 压力 ok=15/fail=0/**掉线=0**、CFSR/HFSR=0、无需 unbind/bind → CDC 物理链路本身稳。
- **L3 CDC+MAVLink 单发（全绿）**：`S_mavlink_usb` HEARTBEAT(**comp=1** 正常) + 全量 ArduCopter L0 gate **exit 0**（STANDBY、**912/912 参数**、30s 流 1890/23types、fault=0）。
- **结论**：崩溃/乒乓**既非地基、非 CDC 物理、非单发 L0**，**唯一出现在 L4 背靠背压力**。`comp=0`/`CRITICAL` 再次确认是背靠背帧损坏的症状（L3 单发为 comp=1）。

#### Phase 1 + Phase 2 修复进展（L4 背靠背）

- **Phase 1（EPENA 守卫 + busy 100ms 看门狗 EPDIS 恢复 + ring 背压，仅 `hal_usb_cherryusb_shim.c`）**：背靠背从"R1 后单调塌陷"改善到 **R1-R3 6/6**；但 R4 退化、**R5 USB 消失/崩溃**（需 reflash）。
- **Phase 2（多包：`CDC_TX_CHUNK_MAX=512` + `cdc_tx_buf[512]` + `cherry_tx_ring_drain_to_buf` 合并 ≤8×64B 一笔 `usbd_ep_start_write`，对齐 ChibiOS PKTCNT）**：单轮 6/6 护栏 PASS；背靠背 6 轮 **6/5/5/3/6/4**，**R5 USB 崩溃彻底消除**（CDC 全程在线、无需 reflash）；GDB 无 HardFault（CFSR/HFSR=0）。
- **残留**：背靠背 mid-run 仍有 timeout/帧错乱（R4 3/6，size=1330926404）；`epdis_recovery_count=301`、`tx_busy_max_ms=2923`(2.9s)、`ring_dropped=701` → 端点仍偶发长停摆（busy 看门狗高频救回，避免崩溃但拖吞吐）。
- **状态**：Phase1+Phase2 改动**在工作区 `hal_usb_cherryusb_shim.c`（约 +172 行）、未 commit**。
- **下一杠杆 Phase 3**：抬高 OTG_FS IRQ 优先级(当前5)到 SDMMC(2)/SD-DMA(3) 之上 或 填 FIFO 用 BASEPRI（ChibiOS `STM32_USB_OTGFIFO_FILL_BASEPRI`），减 SD 抢占致 XFRC 竞态/EPDIS 风暴；并查 2.9s 长停摆根因（EPENA 卡住/host NAK）。

#### Phase 3 诊断 + IRQ 优先级试验（2026-05-29，BLOCKED/已回退）

- **2.9s 长停摆根因（GDB 确诊）**：**(b) EPENA 挂起 + XFRC 未及时 + busy 看门狗循环为主**（halt 见 `DIEPCTL.EPENA=1`、`DIEPINT.XFRC=0`、`cherry_tx_busy=1`）；**(a) 主循环死锁已排除**（`rtt_dbg_main_loop_iterations` 0.5s +305、PC 在业务路径）；**(c) SD 抢占(OTG prio5 < SDMMC2/SD-DMA3) 为加剧因**。
- **IRQ 优先级 5→2 试验：证伪并回退**——单轮护栏掉到 4/6、背靠背 R4=1/6（更差），已恢复 `usb_dc_glue.c` OTG prio=5 + 重烧 Phase2。**「仅抬 OTG 优先级」不可交付**（会扰单轮/其它）。
- **现状交付基线**：commit `b659f5b64a`（Phase1+Phase2）= 致命崩溃已除、单轮 6/6、L1-L3 全绿；**背靠背极端压力下仍有 mid-run 吞吐抖动（深层 EPENA-stuck/host 交互），列为已知 CDC 限制**。
- **若继续（候选，单点可验，但有风险/递减）**：(i) shim 填 FIFO/poll 关键段用 **BASEPRI** 短临界（对齐 ChibiOS，而非整体抬 ISR）；(ii) busy 看门狗 100ms→30-50ms 缩短单次停摆（注意：busy_max 2.9s 说明部分场景看门狗未触发，需先查 watchdog 条件为何漏判）。**按 `rtt-systemic-escalation`：此残留点已多次迭代，继续前应有新证据（mid-run GDB），勿无限磨。**

#### Phase 3b BASEPRI 失败 + 元结论：ISR 抢占非真杠杆（2026-05-29，停止旋钮/升维）

- **Phase 3b（shim 内 BASEPRI=0x20 短临界，屏蔽 ≥SD prio2，对齐 ChibiOS）**：打坏单轮 6/6（→1-3/6，FTP Nack/timeout/帧错乱），背靠背未跑到，**已 git 回退**，板回 Phase2、单轮 6/6 恢复。
- **元结论（关键）**：**Phase3-IRQ(抬 OTG 优先级) 与 Phase3b-BASEPRI(屏蔽 SD) 两个 ISR 抢占类杠杆都让单轮更差**。若残留真由 (c) SD 抢占致，屏蔽/抬优先级应改善；实测反而回归 → **(c) 不是真杠杆，ISR 旋钮方向错误**。残留属 **(b) EPENA-stuck / DWC2-host 交互**深层机制，ISR 旋钮修不了且扰时序。
- **决策（反固着）**：**停止 ISR/优先级/BASEPRI 旋钮**。残留背靠背抖动列为**已知 CDC 限制**；如要继续须**升维到不同方向**：mid-run GDB 钉死 EPENA-stuck 的精确时序（host 是否 NAK/停拉）、或审 CherryUSB dwc2 IN 端点状态机对 EPENA 的处理（vendor 层，区别于 shim 旋钮）、或评估 CDC+MSC/接口/描述符层面。
- **交付基线**：commit `b659f5b64a`（Phase1+2）——致命崩溃已除、单轮 6/6、L1-L3 全绿；背靠背单轮可 6/6、连跑 6/5/5/3/6/4、R5 不再崩。

#### 统一根因综合：GCS 负载"反复重启/慢/内部错误"（2026-05-29，8-agent + 硬件实证 + ChibiOS 全栈对照）

> 用户 GCS 实测日志（反复 "Initialising ArduPilot"、参数慢、断链、内部错误）经 8 路并行只读 ChibiOS 对照 + 1 路硬件实证拆解为 4 类问题：

- **假象层（非复位）**：硬件实证 `rtt_dbg_main_loop_iterations` 负载全程**单调递增**、`rtt_boot_rcc_csr` 恒 `0x24000003` **无新复位**、`sw_reboot_count=0`。"反复 Initialising" = **param 下载 stall → GCS 重试 `PARAM_REQUEST_LIST` → `handle_param_request_list` 调 `send_banner()` 重发整套横幅**（GCS_Param.cpp:218）+ 每 5s init 进度消息。**MCU 一直在跑**。
- **真问题① 慢/stall**：CDC TX 逐包64B+EPENA-stuck（见上 Phase1/2/3）+ **RTT 主线程被 GCS 驱动过重**（250Hz `call_delay_cb` vs ChibiOS 50Hz、delay 回调发所有消息类型、`count_parameters`~30ms 每 PARAM_REQUEST_READ 主线程、PARAM_SET 主线程同步 `save_sync+flush`）→ 主循环慢 → `main_loop_stuck`(500ms) → **GCS 内部错误/CRITICAL**（非复位）。
- **真问题② 真崩溃（核心、危险）**：负载下偶发 **HardFault（USB/CherryUSB/DMA/MAVLink/SPI 路径）→ RTT handler（`context_gcc.S`）`CPSID I`+死循环丢 PC 现场 → 强制 ~2s IWDG 复位**（`0x24000003` IWDGRSTF=1 坐实）。`panic()` 同理（关中断自旋→IWDG）。**真崩溃被 IWDG 包装成"重启"且现场被抹**——这是迟迟抓不到根因之因。极端压力→`hardfault_hang`→USB 消失。
- **独立红旗**：`IMU0 0.0kHz` = `_gyro_backend_rate_hz` 在 banner 显示 0（构造未零初始化 `_gyro_backend_rate_hz`/`_fast_sampling`/`_gyro_fifo_downsample_rate`；EKF 实际在采样、PreArm 不报 gyro rate 失败 → 偏显示/未初始化，需 GDB 钉死）；`IOMCU 0 0 0`（IOMCU 未通信）；`Frame UNSUPPORTED`（未配 frame，仅 PreArm）——三者**独立**于复位。

##### 与 ChibiOS 的系统级偏离清单（≥12 处，修复优先级锚点）
1. **HardFault handler** `CPSID I`+自旋**丢现场** vs ChibiOS 保 PC/LR/CFSR + crashdump（**P0 诊断使能**）
2. **panic** 关中断自旋→IWDG vs ChibiOS delay 循环不关中断
3. **IWDG** 强制 ~2s vs ChibiOS 按需开（把任何 >2s hang/stall 放大成复位）
4. **GCS 驱动** 250Hz delay-cb + delay 回调发所有消息 vs ChibiOS 50Hz/少数
5. **count_parameters**(~30ms) 每 PARAM_REQUEST_READ 主线程全表遍历（RTT 直发旁路）
6. **PARAM_SET** 主线程同步 `save_sync+flush` vs ChibiOS IO 线程异步
7. **CDC TX** 逐包64B + 无 EPENA/XFRC 守卫历史（Phase1/2 已部分修）
8. **SysTick 双定义**：IWDG feed 不在被链接的 `drv_common.c` SysTick → 依赖主循环 pat + idle hook
9. **EKF 不分 FAST 池**（与 MAVLink/栈共用 ~380KiB SRAM1 堆）
10. **栈** mmcsd_detect(~90%)/log_io(1580B) 偏紧；**未注册** `rt_scheduler_stack_overflow_sethook`→溢出=挂死
11. **SPI1 DMA `#if 0`** 轮询 → CPU 占用/jitter；boost 时 IMU 线程可能饿死
12. **Invensense 构造未零初始化** rate/fast_sampling 字段

##### 修复方案（按优先级，下一步）
- **P0（诊断使能，最高）**：改 HardFault/panic handler **先把 PC/LR/CFSR/BFAR + faulting 线程存 BKP/RTC，下次 boot 用 STATUSTEXT 报告**（对齐 ChibiOS crashdump/watchdog 报告），再 hang/reset——**先让崩溃可见**，否则一直在猜。然后负载复现读现场，钉死真 HardFault 点。
- **P1**：减主线程 GCS 负载（delay-cb 降到 ~50Hz、限 delay 回调消息类型、缓存 count_parameters、PARAM_SET 改异步）；继续 CDC TX。
- **P2**：boot 报 IWDG 复位；SysTick IWDG feed 收口；栈溢出 AP 钩子 + mmcsd/log_io 加栈。
- **P3**：IMU banner 零初始化 + GDB 验证；SPI1 DMA；IOMCU/Frame 独立 bring-up。

#### P0/P1 执行结果（2026-05-30，工作区未提交）

> 在 milestone `85c4f83b3e` + Phase1/2(`b659f5b64a`) 之上叠加，**工作区改动未 commit/push**。

**P0 故障捕获闭环（诊断使能，net 已就位）**：
- 新增 `libraries/AP_HAL_RTT/rtt_dbg_bkp.c/.h`（BKP 槽布局 + fault_save/restore/consume + 全局 `rtt_last_fault_*`）；`context_gcc.S` HardFault 死循环前 `BL rtt_dbg_bkp_hardfault_from_asm`；`system.cpp` Bus/Usage/MemManage/panic 写 BKP；`HAL_RTT_Class.cpp` boot restore；`GCS_Common.cpp send_banner()` 发 `PrevFault PC/LR/CFSR/HFSR/BFAR + thr` 后 consume。
- **BKP 硬件**：`RCC_APB1ENR PWREN|RTCEN` + `PWR->CR1 DBP` + LSE/LSI；地址 **`0x40002850 + n*4`**（F7，**非** F1 的 `0x40022850`）。槽：BKP0R=Bootloader FWOK、BKP1R=witness、BKP2R=魔数`0xFA17C000|type`、BKP3-10R=PC/LR/CFSR/HFSR/BFAR/MMFAR/xPSR/线程名4B。
- **关键修复**：`rtt_dbg_bkp_restore_prev_fault()` 曾被 GCC 优化成空壳→`rtt_last_fault_pc` 恒 0；加 `__attribute__((noinline))`+`volatile` 读 BKP 后闭环。
- **注入验证（端到端通路）**：OpenOCD 注入 PC=`0x08092280`→`st-flash reset`→PARAM_REQUEST_LIST→GCS 实测 `PrevFault PC=08092280 thr=ap_u`；addr2line→`uart_poll_write`(UARTDriver.cpp:149)。**正常 boot 护栏 MAVFTP 6/6**。
- **真崩溃未复现**：3×MAVFTP + 45s param flood(144 LIST/875 VALUE) **未崩/未 USB 消失**；网在线等真 fault 自投。

**P1 GCS 负载（用户可见痛点，已量化、有效）**：
| 改动 | file:行 | 效果 |
|------|---------|------|
| A1 删主循环每轮 call_delay_cb | `HAL_RTT_Class.cpp:259-262` | 去 ~250Hz delay 路径 |
| A2 GCS update 250Hz→50Hz | `AP_Vehicle.cpp:726-731` | 对齐 ChibiOS |
| A3 删 RTT delay 回调 return true（全消息） | `GCS_Common.cpp:1230-1247` | delay 仅发 NEXT_PARAM/HEARTBEAT/HIGH_LATENCY2/AUTOPILOT_VERSION 白名单 |
| B PARAM_REQUEST_READ 直发后 return（免 io 双路径） | `GCS_Param.cpp:261-263` | 减 count_parameters 压力 |
| C PARAM_SET save_sync+flush→`vp->save()` 异步、去直发ACK | `GCS_Param.cpp:344-356` | **param 下载主杠杆** |
- **量化（改前→A+B+C）**：param 全量 **120.39s→59.18s（~51%↓）**、LIST 重发 1→0；MAVFTP 背靠背 5 轮 改前 4/3/4/4/4 → 现 R1 2/6*(param flood 后未 drain 过渡态)/R2 5/6/**R3-R5 6/6**；无回退、稳定态单轮恒 6/6。
- **待办**：补「STANDBY drain 5s 后 5 轮 MAVFTP + 负载期在线 loop/s 采样」干净复验 → 达标即升 status + 可作 milestone（含 P0 net）。

#### P0+P1 干净复验结果（2026-05-30）——**未达 milestone + 暴露强变异性**
- **param ×3**：R1 **912/912 但 92.16s**（上轮 59s，**同固件 1.5× 变异**）、0 LIST 重发；R2 599/912、R3 686/912 **不完整**（R1 重负载后仅 4s 冷却→下载中途 stall 早停，非重发）。
- **MAVFTP 背靠背 ×5**：4/2/3/3/**6**（仅 R5 6/6，1/5，判据 ≥4/5 **FAIL**）；R1-R4 受 param flood 余温，T3/T4 为主退化（T3 见 size 头污染 `1330926404`、read timeout——与历史 CDC TX 背压一致）；全程 USB **未消失**。
- **在线 loop_hz（RTT_CTL，非 halt）**：空闲 **927**、param 负载 424/997/483（最低~46%）、MAVFTP 负载 557/469；**overrun 627→8k→22k+**（主线程余量在负载下急剧收窄，但未归零）。
- **P0 网**：全程 CFSR/HFSR=0、PrevFault=0、无 IWDG、无 USB 崩溃——**保持在线**。
- **判定**：P1 单轮能力在（R1 912/0retry、R5 6/6），但**背靠背 reproducibility + 重负载后恢复**未过线；**核心病灶 = 持续/背靠背负载下主线程被挤占（overrun 暴涨）+ 重负载间恢复慢 + 强变异**。
- **结论指向 A/B**：59↔92s 同固件强变异需 **ChibiOS 同板同协议基准**判定是 主机/协议 还是 RTT 真回归；ChibiOS 的负载期 loop_hz/overrun 是决定性对照。

#### ChibiOS 真机 A/B 基准对比（2026-05-30 起，进行中）
> 同板/同主机/同线缆/同协议；ChibiOS `upstream/master` `d3a32025cd` CUAVv5（waf，`/home/llw/firmare/pogo-chibios-master/build/CUAVv5/bin/arducopter.apj`，board_id=50，app 0x08008000，共用 bootloader）。协议：冷启≥15s + STANDBY drain 5s；param×3 间隔≥30s idle；bulk 吞吐读 `@PARAM/param.pck`（两侧同文件）；MAVFTP×3 间隔≥30s。

| 指标 | **RTT（P0+P1）** | **ChibiOS master（目标）** | 差值/判定 |
|------|------------------|---------------------------|-----------|
| param 全量（个/s） | 912 / ~73s = **~12.5 个/s**（稳态 71–76s，±3.5%） | 1004 / 14.6s = **~69 个/s**，3 轮全 1004/0 重发 | **ChibiOS 快 ~5.5×** |
| bulk CDC 吞吐（param.pck） | **~2.4 KB/s**（1.3–3.6） | **~12.2 KB/s**（12.0–12.4，稳定） | **ChibiOS 快 ~5×** |
| MAVFTP ×3 | R1/R2 4/6、R3 6/6（需 idle 恢复） | **3×6/6 稳定** | ChibiOS 稳态满分 |
| MAVFTP 读 SD 日志 | 0 字节 | **open_fail（也读不出）** | **两侧都不行 → 协议-SD 大文件路径独立问题，非 RTT 回归** |
| USB 枚举 | `usb-APM_CUAV_V5_CDC_1_*` 1209:5741 | `usb-ArduPilot_CUAVv5_*` 1209:5740 | — |

**A/B 决定性结论（2026-05-30）**：同板/同主机/同线缆/同协议下，**RTT 比 ChibiOS 慢约 5×**（param 个/s 与 bulk KB/s 均 ~5×）。**证明：① 慢是 RTT 真回归，非主机/协议/线缆**（协议在 ChibiOS 满速）；**② bulk param.pck 仅 ~2.4 KB/s 且为分块往返延迟主导（~11KB/47块，RTT ~98ms/块 vs ChibiOS ~19ms/块）→ 瓶颈在 RTT 每次「USB RX→firmware 处理→USB TX」往返延迟**（USB 服务节拍 + TX 背压 + 主线程调度），非带宽、非 MAVLink 协议。**③ SD 大文件 MAVFTP 两侧皆失败 → 独立问题，从 RTT 回归清单剔除**。
- **下一步**：归因实验把 5× 拆成 USB栈(CherryUSB) vs 主线程——param 下载期采 `rtt_uart_usb_diag_write_fails/fail_streak/clears` 增量 + loop_hz/overrun：write_fails 暴涨→USB TX 背压主导；write_fails 低但参数细水→主线程 emit/调度主导。
- **A/B 方法已验证可用**，后续逐驱动（IMU 采样率/Baro/Storage/RC）复用同法。

#### 5× 慢归因结论（2026-05-30，硬件实测 Δ）
- **主因 ~70%：CherryUSB TX 背压/端点停顿恢复**。param 下载（912/80s）期间 Δ：`write_fails +71`、`tx_ring_dropped +73`、**`epdis_recovery_count +46`**（fail_streak=0、epena_guard=0 → 非 EPENA 卡死型，而是 **tx_busy 拿不到 XFRC → 100ms busy-watchdog → EPDIS 恢复**，历史 `tx_busy_max_ms` 达 2882ms）。单向 TX 持续吞吐仅 **0.5–0.9 KB/s**（绝对值极低）。**46×100ms ≈ 4.6s 纯恢复死时间**即 5× 慢主体。
- **次因 ~30%：主线程 GCS emit/调度**。`SET_MESSAGE_INTERVAL`/`request_data_stream` **不生效**（ATTITUDE 仅 ~0.8–2.4Hz，发了 7× COMMAND_ACK 仍拉不高）；param 负载期 loop_hz 343–506（≈空闲 46–57%）、overrun 攀升。
- **根因判断**：Phase1/2 的 busy-watchdog+EPDIS 是**从停顿恢复的创可贴，未修停顿根因**；需查 **RTT CherryUSB DWC2 TX 为何反复 tx_busy 无 XFRC**（TXFE FIFO 重填节拍 / EP 装载 PKTCNT-XFRSIZ / poll 频率 / IN-NAK），对照 ChibiOS USB TX 完成路径。
- **独立次 bug**：`SET_MESSAGE_INTERVAL` 不生效（streamRates/SR* 配置链）——RTT 特有，单列。

#### TX stall 根因 + ChibiOS 照抄方案（2026-05-30，两路只读合流）
- **CUAV V5 ChibiOS 实际走 OTGv1**（`modules/ChibiOS/.../LLD/OTGv1/hal_usb_lld.c`，**非 USBv1**）。TX 满速不 stall 机制：`usb_lld_start_in` 只 arm（PKTCNT/XFRSIZ + `DIEPEMPMSK`）→ `otg_txfifo_handler`(TXFE ISR) 续填 → `otg_epin_handler`(XFRC ISR)：`txsize<totsize` 则再 arm、否则 `_usb_isr_invoke_in_cb`→`sduDataTransmitted` **ISR 内链下一 buffer**。**全 ISR 自洽、零 poll、无 watchdog/EPDIS**。EP1 TX FIFO **128B**（`in_multiplier=2`，`usbcfg.c:45`），`STM32_USB_OTG1_IRQ_PRIORITY=14`（低），obqueue 768×4。
- **RTT CherryUSB 偏离（70% 根因）**：① EP1 TX FIFO **仅 64B**（`usb_config.h:CONFIG_USB_DWC2_EP1_TX_SIZE=16` words）→ TXFE 更频繁、负载欠载 stall；② shim `usb_lld_poll_rtt` **poll kick + 100ms busy-watchdog + `cherry_tx_epdis_recovery`** 创可贴（`hal_usb_cherryusb_shim.c:530-577,355-381`）；③ `usbd_cdc_acm_bulk_in`(XFRC) **ISR 内嵌套 `cherry_tx_kick`→start_write**，与 poll kick **双路径竞态**；④ OTG IRQ 优先级 5（高于 ChibiOS 14）。机制（TXFE/XFRC）本身在 `usb_dc_dwc2.c:381-422,1086-1118` 已有，问题在 **shim 状态机 + 浅 FIFO + poll 双路径**。
- **照抄修复（一次一处，对 A/B 基准量化：RTT 73s/2.4KB/s/46×epdis → 目标 ChibiOS 14.6s/12.2KB/s/0 epdis）**：
  - **Fix#1（最小先做）**：EP1 TX FIFO 64→128B（`CONFIG_USB_DWC2_EP1_TX_SIZE 16→32`；FIFO 预算 RX128+EP0 16+EP1 32+EP2 4+EP3 16=196 / 320 words OK）。
  - **Fix#2**：TX 纯 ISR 链式（XFRC cb 可靠链下一 chunk）+ **去 poll kick 双路径** + 退役 watchdog/EPDIS（对齐 sduDataTransmitted）。
  - **Fix#3**：OTG IRQ 优先级评估（高优先级未必更可靠）；SET_MESSAGE_INTERVAL 不生效。
- **注**：单独加大 FIFO 历史曾更差，但那在 Phase2 多包之前；现配 512B 多包应有效——仍须量化，跌破单轮 6/6 即回退。

#### Fix#1 结果（2026-05-30，有效保留，工作区未提交）
- 改动：`cherryusb_board/usb_config.h:26` `CONFIG_USB_DWC2_TX1_FIFO_SIZE (16)→(32)`（64B→128B）；FIFO 预算 196/320 words。
- 量化（A/B 同协议）：**下载期 `epdis_recovery`/`write_fails`/`tx_ring_dropped` Δ 全 +0（基线 +46/+71/+73）= stall 完全消除**；param median **73s→47s（19.4 个/s，~36%↑）**；bulk ~1.6KB/s（R2 2.8，未质变）；MAVFTP 冷启 6/6；负载期 loop_hz 高于基线。
- 判定：**FIFO 浅是 stall 必要根因（已对齐 ChibiOS 0 epdis）但不充分**；param 仍距 ChibiOS 14.6s ~3×、bulk 未阶跃 → 剩余瓶颈 = **TXFE 续填链 / poll kick 双路径 / 每传输周转**（Fix#2）。变异仍在（R2 71s vs R1/R3 ~45s）。
- **累计已验证有益且未提交**：P0(故障网) + P1(GCS 负载 120→59s) + Fix#1(FIFO，stall 清零+36%)；板稳（MAVFTP 冷启 6/6、无 stall、无 fault）→ **适合作为 Fix#2(较高风险 ISR 改) 前的 checkpoint/milestone 回滚点**。

#### Fix#2 结果：变差，已回退（2026-05-30）
- 改动：删 `usb_lld_poll_rtt` 573-575 poll kick（纯 ISR 链）。量化：param median **73.96s（12.3 个/s，比 Fix#1 47s 慢 ~57%）**、下载期 **epdis_recovery Δ=+7**（Fix#1 是 0）、MAVFTP R1 6/6→R2 4/6→R3 5/6 不稳。**已回退**，工作区回到 P0+P1+Fix#1，HEARTBEAT 正常。
- **反推结论（重要）**：**poll kick 是续发链高压下的必要补发**，非纯冗余竞态；去掉它反而暴露续发缺口。**ChibiOS 零-poll 纯 ISR 链不能直接照搬**到当前 shim 结构。连同 Phase3（IRQ 优先级/BASEPRI）失败，**两次"USB ISR 旋钮"均证伪**。
- **方法论修正（反固执）**：停止盲调 USB 旋钮。剩余 47s→14.6s（3×）先做**生产者 vs 消费者**判定：param 下载期采样 `cherry_tx_ring_count`(0..32) + `_writebuf` 占用——**ring 多空=生产者饿死(主线程 param-send/stream_slowdown 为限)**；**ring 多满=消费者堵死(USB TX 排不出为限)**。据此再定 Fix#3 方向（主线程 param-send 节拍 vs USB TX 吞吐），不再试旋钮。
- **里程碑判断**：P0+P1+Fix#1（param 120→47s、stall 清零、板稳）已是可交付大进展，剩余 3× 属深水区递减收益。

#### 瓶颈判定：生产者饿死（2026-05-30，非侵入在线采样，决定性纠偏）
- 方法：OpenOCD `mdw` 非停机在线读 `cherry_tx_ring_count`(@0x2001b164, 深度32) + `cherry_tx_busy`(@0x2001a95c)，param 下载期 ≥567 样本。
- **结果：ring_count min/med/max = 0/0/1（mean 0.02），100% ≤2，0% ≥28（从不接近满），`cherry_tx_busy` 仅 ~1%**。
- **决定性结论：USB TX 端 ~99% 空转等数据 = 消费者(USB)不是瓶颈；瓶颈在生产者（主线程 GCS param-send → `_writebuf` → ring 喂数太慢）**。param 本轮 15–23 个/s（ChibiOS 69）。
- **纠偏（重要）**：Fix#1 消除 stall 后，剩余 3× 的**主因是生产侧（之前判的"30% 次因"）**；此前 Fix#1/Fix#2/Phase3 全在**空闲的消费侧**使劲。**Fix#3 转生产侧**：`GCS_Param`/`AP_Param` 流式发送节拍、`stream_slowdown`、`GCS_MAVLINK::update_send`/`queued_param_send` 频率与每次条数、`txspace()`/`_writebuf` drain 是否误报致节流。**不再加 ring/调 USB IRQ**。
- 注：`_writebuf` 无全局符号，需要时加临时诊断计数确认"GCS 没产出"还是"_writebuf 满但 ap_uart→ring 慢"。

#### Fix#3 保留 / Fix#3b 回退 / 剩余根因=慢性 overrun（2026-05-30）
- **Fix#3 保留**（`GCS_Common.cpp:1669-1696` param 下载期 `_queued_parameter!=null` 跳过 bucket telemetry，只发 deferred）：param 47→**33.7s median(~27 个/s)、best 30s**；MAVFTP 复位后 6/6；telemetry 下载后恢复。
- **Fix#3b 回退**（`GCS_Param.cpp` queued_param_send 1ms→3ms）：每批条数计数 ~2→峰值~6（机制对），但 **param 27→12.5/s、MAVFTP 4/6、空闲 overrun 14706→28960** → **净负，已回退**。给 param 多 CPU 会饿死主循环其余部分。
- **param 旋钮路径已穷尽**（Fix#2 删 poll、Fix#3b 加预算 均证伪）。
- **剩余 ~2.5×（27 vs 69 个/s）+ R2 慢轮变异 的真根因 = 主循环慢性 overrun**：Fix#3 空闲 overrun ~14706、loop_hz ~953（应稳 ~400-1000 但 overrun 计数异常高），负载/加 CPU 即恶化 → `out_of_time()`（GCS.cpp:497-503，Copter 保留 250µs）频繁跳过 `update_send` → param 节流。**ChibiOS 无此 overrun**。
- **下一步（转系统调查，停 param 旋钮）**：只读查 RTT 主循环为何慢性 overrun——AP_Scheduler loop 预算/各 task 实际耗时、RTT Scheduler delay/yield 语义、`overrun` 计数定义、idle 也高 overrun 之因，对照 ChibiOS。修 overrun 才能同时收敛 param 剩余 2.5× + 变异。

#### 本轮 A/B 战役里程碑（2026-05-30，工作区未提交）
- **累计已验证有益**：P0(故障捕获网) + P1(GCS 负载 120→59s) + Fix#1(EP1 FIFO 64→128B，stall/epdis 清零) + Fix#3(param 期静音 telemetry)。
- **param 全量：最初 ~120s → 现 ~33.7s median（3.6×↑）**；USB TX stall(EPDIS) 清零；MAVFTP 复位后 6/6；故障网在线；板稳。
- **A/B 方法验证可用**（同板 ChibiOS upstream/master 真机对比 + 非侵入在线采样 + 一次一处量化回退），后续逐驱动复用。
- **可作 milestone 提交**（待用户确认）。

#### 剩余 2.5× 根因：SPI 线程 CPU 争用（2026-05-30，overrun 只读调查）
- `rtt_dbg_overrun_count`(HAL_RTT_Class.cpp:251-279)=自启动累计「单次 loop work 墙钟 >2500µs」次数，**非 bug、非 AP_Scheduler task overrun**；idle 高=确有比例迭代超 2.5ms。
- **根因排序**：① **SPI1/SPI4 DeviceBus @prio4、1000Hz IMU、CMSIS 字节轮询 + DMA 忙等（SPI1 LLD DMA `rt_board_init.c:183` `#if 0` 禁用）**→ 持续抢占 main(prio5)→`time_available` 耗尽→`out_of_time`(GCS.cpp:497, Copter 留 250µs)跳过 `update_send`→`queued_param_send` 1ms 墙钟内被抢占只发 ~2 条。② main(5) 被 timer(4)+SPI(4) 抢占。③ **RTT `delay_microseconds_boost`(Scheduler.cpp:518-544) 固定 `rt_thread_delay(1)`≈1ms、忽略 us**（ChibiOS 真睡 usec）→ inflate work/overrun。④ param 与主循环零和（Fix#3b 证伪）。ChibiOS SPI 在 DMA 上 `osalThreadSuspend` 睡眠不占 CPU、boost 真睡 → 无此 overrun、param 69/s 稳。
- **Fix#4 候选**：A=修 SPI1 DMA 或 `_spi_dma_xfer` 忙等改 IRQ/阻塞（高收益，**风险中**：DMA 曾因 cache/地址读 0xFF 被禁、错则 HardFault/IMU 错）；B=对齐 `delay_microseconds_boost`（低-中风险）；C=调优先级（高风险，违背 SPI>main 设计，ChibiOS 同样 SPI>main）；D=改 overrun 阈值（无效）。
- **动 SPI DMA 前先硬件确认**：非侵入测 param 负载期 `rtt_cpu_idle_pct`(vs idle ~99%) + `rtt_spi1_rt.spi1_xfer_calls`/字节率 + `overrun/iter` 比 + `work_time_max_us`，坐实 SPI CPU 大户、量可回收余量。

#### 冒烟枪：SPI 轮询烧 ~80% CPU（2026-05-30 硬件实测，systemic 根病）
- GDB halt 读 `rtt_cpu_idle_pct`(@0x2001EDD8)：**空闲 17–21%（即 ~80% CPU 已占用）**，param 负载期多次 **0%**，结束回 99%（有可回收余量）；`spi1_xfer_calls`(@0x2001B3D0) **~2339 次/s**；loop_us 负载峰 ~2µs→2ms、ov/iter≈5.7%。
- **推翻 status.md「CPU idle 99%/load ~1%」旧结论**：真实空闲 ~80% CPU 被 **SPI1 IMU 1kHz CMSIS 字节轮询 + DMA 忙等（SPI1 LLD DMA `rt_board_init.c:183` `#if 0`）** 吃掉。ChibiOS 同采样走 DMA+`osalThreadSuspend` 睡眠 ~1% CPU。
- **判定 Fix#4 走 A（SPI DMA/阻塞），非 B（boost）**：依据 param 期 idle→0% + SPI 2339/s（CPU 总量争用，非仅墙钟让出）。
- **这是剩余 2.5× + 变异 + 慢性 overrun 的共同根病**，且属 SPI 驱动架构债（呼应"逐驱动对比找问题"）。
- **前置（改 IMU SPI 前必须先只读搞清）**：SPI1 DMA 当年为何「读 0xFF」被禁（STM32F7 D-cache 一致性 / DMA 缓冲区放置 DTCM 不可 DMA / SCB_CleanInvalidateDCache 缺失 / 地址对齐），以及安全恢复 DMA+阻塞的正确做法。**风险中-高，触及关键 IMU 驱动**，需稳妥 + 可回退。

#### Fix#4-A 设计（SPI1 DMA+阻塞恢复，只读设计完成，未实施）
- **0xFF 根因**：注释（`rt_board_init.c:180-188`）记 DMA 路径 WHO_AM_I 读 0xFF（SPI4 DMA 正常）；最可信历史根因 = **DMA buffer 落 DTCM/非 DMA 可达 + 可能 stale RX FIFO**；stream 表现已正确(`SPIDevice.cpp:211` SPI1 remap Stream2/5)、DCache 现已全局关。
- **现状**：生产 IMU 路径 AP_HAL `_dev=nullptr` 绕过 RT 框架；`_spi_dma_xfer`(`SPIDevice.cpp:369-428`) **CPU 忙等 `DMA_SxCR_EN`**（设了 TCIE 不用 IRQ）+ 半双工 `spi1_poll_transfer` 字节轮询 = ~80% CPU。仓库已有 `drv_spi_lld.c`（`rt_completion_wait`@334 + RX DMA IRQ@427 + RX 卡死 workaround@287-324），但 SPI1 未注册、AP_HAL 未调用。
- **最小恢复路径（推荐，非仅解 `#if 0`）**：编译开关 `RTT_SPI1_AP_HAL_LLD`；①board 恢复 SPI1 LLD 注册+attach 后再开 NVIC；②`SPIDevice._spi_dma_xfer` 改调 `spi_lld_xfer`+`rt_completion_wait`（删 EN 忙等）；③`transfer_fullduplex` 接 `DeviceBus::bouncebuffer_setup`（防 DTCM/对齐），`_fifo_buffer` 已 SRAM1 可 fast-path + 地址断言；④DCache 关时无需 clean/invalidate（重开则加）。
- **风险/回退**：buffer 在 DTCM→0xFF、stream/通道错→HardFault、NVIC 早于 attach→启动 HardFault、RX TC 不触发→短读（LLD 有 workaround）。分步验证 WHO_AM_I(0x98)→FIFO→30s idle 看 cpu_idle 回升→整机 CDC/param→回归 MS5611/FRAM。回退=编译开关关。
- **低风险替代（若暂缓 DMA）**：仅把 `_spi_dma_xfer` EN 忙等改 `rt_completion_wait`+IRQ（保留现 DMA 寄存器写法，缺 RX workaround，中-高收益）；纯 yield 替代达不到 ChibiOS 量级。
- **预期收益**：SPI 线程 ~80%→个位数 %（对齐 ChibiOS ~1%），cpu_idle→70-90%+，param/overrun/变异同时显著改善。
- **决策点**：触及关键 IMU 驱动、多阶段、风险中-高；当前 P0+P1+Fix#1+Fix#3 是干净可交付里程碑（未提交）→ **建议先提交 checkpoint 作回滚点再做 Fix#4-A**，待用户定夺。

#### Fix#4-A-low 失败回退（2026-05-30）
- 改动：`_spi_dma_xfer` 大块 DMA 完成由 EN 忙等改 RX-DMA-TC IRQ→`rt_completion_wait(5ms)`+超时回退（编译开关 `RTT_SPI_DMA_IRQ_WAIT`，`SPIDevice.cpp:184-361,533-563`、新 `rtt_spi_dma_irq.h`、`drv_spi.c:907-925,1070-1088` IRQ hook）。
- **结果：`=1` 时 USB CDC 一打开就断开**（`device disconnected`），无法验 IMU/MAVLink；CFSR/HFSR=0 无 HardFault、fallback_count=0（IRQ 等待本身似工作）。**已回退 `=0`**，IMU(RAW_IMU 正常)/MAVLink 恢复；回退后 cpu_idle 单次 GDB 读 53%、param ~19/s、MAVFTP 4/6。
- **根因判断**：CMSIS 寄存器 DMA + `rt_completion`/RX-TC-IRQ 与 RTT USB(DWC2)/调度存在**致命冲突**（IRQ 优先级 / completion-in-ISR 语义 / 与 board HAL DMA 向量共享竞态）——**不能单靠"RX TC IRQ + completion"安全替换 EN 忙等**。
- **三次代码改动连续证伪**（Fix#2 删 poll、Fix#3b 加预算、Fix#4-A-low DMA-IRQ）→ 剩余 2.5× 需 **full SPI LLD 重构（ChibiOS 式 TC ISR→线程睡眠 + 半双工 DMA 化）**，大且风险高。
- **下一步（零风险只读）**：①查 DMA-IRQ-completion 为何崩 USB（定 full-LLD 可行性/安全做法，如 IRQ 优先级、`rt_completion_done` 是否 ISR 安全、向量冲突）；②半双工轮询路径**低风险 CPU 省法**（减 cs_take CR1 重配 / 合并 FIFO-count+data / 降事务数），不碰 DMA/IRQ/USB。择最低风险有效杠杆。
- **里程碑现状**：P0+P1+Fix#1+Fix#3，param 120→33s(best,~27/s median)、stall 清零、板稳、IMU/EKF 正常、故障网在线——**强烈建议提交**（已三次询问被跳过，按 git 策略未擅自提交）。

#### 最低风险杠杆调查 + 战役收尾结论（2026-05-30）
- **Fix#4-A-low 崩 USB 根因（多因）**：① TX DMA 仍走 `HAL_DMA_IRQHandler`（`drv_spi.c:935-948` 只 hook RX 未 hook TX）→ CMSIS 直写与 HAL 双主控；② OTG_FS(`usb_dc_glue.c:49`) 与 SPI1-DMA-RX(`SPIDevice.cpp:272`) **同 NVIC prio 5**；③ `rt_completion_done` 在 ISR → `rt_thread_resume`/`context_switch_interrupt` 唤醒 SPI(4) 抢占 main(5)/USB 收尾；④ `usb_lld_poll_rtt` 关 PRIMASK 写 EP 与之叠加。`fallback=0`+无 HardFault → 崩的是 USB 枚举时序非 SPI 读错。
- **full-LLD 可行但有前置 checklist（远期、需慎重）**：AP_HAL 改走 `spi_lld_xfer` 关 CMSIS 直写（单一主控）；TX/RX IRQ 统一进 LLD；NVIC 分层 OTG=4/SPI-DMA=6+（勿同 5）；OTG 补 `rt_interrupt_enter/leave`；buffer 非 DTCM（`_fifo_buffer` 已 MEM_DMA_SAFE）；分步验证含 **CDC-open stress 门禁**。**不要再在 CMSIS `_spi_dma_xfer` 上打补丁式 IRQ wait**。
- **低风险 Fix#4-B（可选渐进，~10-20% CPU，够不到 2.5×）**：`AP_InertialSensor_Invensense::_read_fifo` 用 cs_held 合并 FIFO-count+burst 去掉独立半双工事；`_spi_dma_xfer` 在 `_cs_held` 跳过 CR1/SPE 重配。注意 `_read_fifo` 是共享 ArduPilot 代码，改需 RTT 守卫/保语义。
- **战役收尾判定**：param 快速优化路径穷尽（Fix#2/3b/4A-low 三次证伪）；剩余 2.5× 唯一真解=full-LLD（大改/风险高/需先解 USB 冲突），属需用户决策的远期专项。**本会话交付：param 120→33s(3.6×)、stall 清零、故障网、A/B 方法、SPI 驱动 CPU 根因全图**。**P3 SPI1 DMA 项 = full-LLD checklist**。

#### Fix#4-B 失败回退（2026-05-30）——第四次连续证伪
- 改动：`AP_InertialSensor_Invensense::_read_fifo`（RTT 守卫）cs_held 下用 `read_registers(FIFO_COUNTH,2)` 合并 FIFO-count 读。**结果：IMU 卡 Initialising 90s+、无 RAW_IMU/ATTITUDE/STANDBY**（疑 CS/bus lock 未释放或 count 语义错）→ **回退**，IMU 恢复（acc≈124,197,-1002mg、gyro≈0、22s EKF 对齐）。
- **四次连续证伪**：Fix#2(删poll)、Fix#3b(加param预算)、Fix#4-A-low(DMA-IRQ崩USB)、Fix#4-B(IMU事务合并卡init)。**剩余 2.5× 的快速/中等修复空间已穷尽**。
- **明确结论**：剩余 2.5× 唯一真解 = **full SPI LLD 重构**（多阶段/高风险/已证会撞 USB，需 checklist+CDC-open 门禁），是**需用户明确授权的远期专项**，非单轮 executor 可稳妥完成。
- **当前板载/工作区 = Fix#3 基线（稳定，IMU/EKF 正常）**。建议：①提交里程碑锁成果；②full-LLD 作为专门的下一阶段（带回归门禁）；③或转其他低风险驱动项（P2 IWDG boot 报告/栈钩子、P3 IMU banner 0.0kHz 零初始化）。

### 分层驱动测试 — HAL smoke 构建已闭环；上板部分通过（2026-05-29 起）

- [x] **BUILD_ONLY 占位已替换（构建门禁）**：`D_uart_hal`、`D_spi_hal`、`D_i2c_hal`、`D_storage`、`D_rcoutput`、`D_rcinput`、`E_sdcard`、`E_wspi_flash` — **2026-05-29 manifest 自检 + 串行 scons 8/8 PASS**；固件调用真实 `hal.*` 或 SD POSIX 路径（**非**旧版单步 `test_runner` 占位）
- [x] **Batch A 上板（3/8 已通过）**（2026-05-29；证据见 `status.md` / `driver-validation-matrix.md` / `agent-trace.md`）：
  - [x] **`D_uart_hal`**：烧录 Verified OK；UART7 `[D_UART_HAL] RESULT: PASS`；CFSR/HFSR=0
  - [x] **`D_spi_hal`**：烧录 Verified OK；ICM20689 WHO_AM_I **0x98**；`[D_SPI_HAL] RESULT: PASS`；fault=0
  - [x] **`D_i2c_hal`**：烧录 Verified OK；IST8310 WAI **0x10**；`[D_I2C_HAL] RESULT: PASS`；fault=0
- [x] **Storage / SD / USB 分层上板（2026-05-29）**：
  - [x] **`D_storage`**：烧录 Verified OK；UART7 `[D_STORAGE] RESULT: PASS`；tail-8B scratch readback/restore；fault=0；**RAM stub，非 FRAM 持久**
  - [x] **`E_sdcard`**：首轮失败定位为 SD 供电时序 + `sdcard_port.c` 重复 init/mount `/sdcard`；修复后 `/APM` POSIX 写读删 PASS，`stage=10 result=0`，fault=0
  - [x] **`L7_cherryusb_cdc`**：`1209:5741 Generic L7 CherryUSB` 枚举；ttyACM1 echo OK；fault=0
- [ ] **上板 / 运行时验收（其余未做）**：`D_rcoutput`、`D_rcinput` — **未烧录/未 UART7 验收**；`E_wspi_flash` @ cuav_v5 仍为 **N/A**（仅构建门禁）
- [ ] **已上板项仍须遵守的局限（不得因 PASS 升级为整机能力）**：
  - **UART**：`D_uart_hal` — **无 RX/loopback**；仅 TX + `hal.serial(6)` smoke
  - **SPI**：`D_spi_hal` — 仅 ICM20689 WHO_AM_I；**无** MS5611/FRAM/DMA 在 D 层 smoke 内；cuav_v5 RTT hwdef 已恢复 `SPIDEV ms5611`/`ramtron`（2026-05-29 串行构建 PASS）；`E_ms5611` **已上板 PASS**；`E_fram` **已上板 PASS**（2026-05-29 SPI2 CMSIS 轮询修复）；`S_sensors` **已上板 PASS**（IMU + MS5611 薄组合）
  - **I2C**：`D_i2c_hal` — 仅 IST8310 WAI；**无** `AP_Compass` 全栈
- [ ] **未上板项局限（matrix 不得标「已上板通过」）**：
  - **Storage**：`D_storage` 已上板通过 tail-8B scratch RW；cuav_v5 **RAM stub**，**非** FRAM 持久化；`E_fram` **已上板 PASS**（FM25V02A RDID id 0x22/0x08 + 4B scratch RW；`D_storage` 仍 RAM stub）
  - **External chip**：`E_imu` **已上板通过**（ICM20689 chip WHO_AM_I=0x98，非 INS 全栈）；`E_ms5611` **已上板 PASS**（PROM/CRC）；`E_fram` **已上板 PASS**（见上）
  - **RC**：`D_rcoutput` 仅软件 `read`/`read_last_sent`；**PWM 波形未验证**（示波器/无桨 ESC）
  - **RCIn**：`D_rcinput` 无 SBUS/PPM → **TEST_FAIL**；`AP_RCPROTOCOL_ENABLED=0`；`E_sbus` / 协议栈变体 **待实现**
  - **SD**：`E_sdcard` 已在插卡场景下通过；覆盖 SD mount + `/APM` POSIX RW/unlink；整机日志长稳仍需单独跑
  - **WSPI**：`E_wspi_flash` 在 CUAV V5 **N/A**；H7 板 JEDEC smoke **延后**
- [x] **Subsystem smoke 首批（2026-05-29）**：`S_param_storage`、`S_sensors`、`S_mavlink_usb` 目录+manifest+**串行构建 PASS**；`S_rc_chain` **未**登记；`S_compass` 仍规划
  - `S_param_storage`：**已上板通过**（UART7 `RESULT: PASS`，CFSR/HFSR=0）；**非**完整 `AP_Param`（无 vehicle `var_info`）；storage tail-16B 子系统 scratch
  - `S_sensors`：**已上板通过**；IMU 步同 `E_imu`，MS5611 步同 `E_ms5611`；**无** INS/Baro 全栈
  - **`S_mavlink_usb` runtime（2026-05-29）**：CherryUSB + `hal.serial(0)` + `mavlink_msg_heartbeat_pack`（`build_old`/`build` GCS_MAVLink headers）；上板 pymavlink HEARTBEAT msgid=0 @921600；CFSR/HFSR=0；**不**替代整机 L0/参数/GCS 全栈
  - **`D_usb_serial` CDC 主机验收（2026-05-29）**：CherryUSB+HAL；上板 **CDC TX PASS**（`1209:5741` @921600，beacon+echo）；CFSR/HFSR=0；首包横幅仍可能因枚举时序错过
- **执行计划（历史）**：`.cursor/project/driver-validation-hal-smoke-plan.md`（Batch A–D）；Batch A **上板 3/3 完成**，Batch B 中 `D_storage` 与 `E_sdcard` 已上板通过；非 RC 低风险项 `D_scheduler` / `D_analogin` / `S_param_storage` / `E_imu` 已上板通过
- [x] **D_analogin printf/采样（2026-05-29）**：`test_printf` 无 `%f`/`%u` → `%lu`+mV；`rtt_adc_*` 诊断；smoke ch6（SCALED_V3V3）；上板 `raw=2064`、`conv=108`、无 zero-samples WARN
- **下一步**：`D_storage` 可选切 FRAM 后端；`S_compass`/`E_ist8310` 可选补齐；RC 项暂缓
- **规则**：构建 PASS ≠ 上板通过；一次只验收一个 `--test=`；稳定结论进 `status.md` 须父代理只读验收

### Remaining port items
- [x] Verify MAVLink over USB CDC (serial0) — ✅ 23+ 消息类型已验证
- [x] Verify barometer (MS5611 on SPI4) — ✅ 校准完成
- [x] Verify compass (IST8310 on I2C3) — ✅ 3D_MAG healthy
- [x] ROMFS support for IOMCU firmware binary — ✅ io_firmware.bin 嵌入成功
- [ ] Verify RC input with actual RC receiver (SBUS via IOMCU)
- [ ] Verify servo output with actual ESC/servo (RCOut PWM)
- [ ] **GPS**（2026-05-29 实测）：物理已接；`SERIAL3/4_PROTOCOL=5` 正确，但 **`GPS1_TYPE=0`/`GPS2_TYPE=0`（NONE）驱动被禁**；45s `GPS_RAW_INT` 全程 fix=0/sats=0、无 "detected" 日志。**根因=参数 TYPE=0**，非链路；下一步设 `GPS1_TYPE=1(AUTO)` 重启复测（室内或可仅"检测 OK"，fix≥3 需室外）。注意 `D_storage` RAM stub，参数持久化未切 FRAM，重启可能丢参
- [ ] **notify/RGB**（2026-05-29 实测）：notify 无报错；OpenOCD 三采样 RGB(PH10/11/12)+LED(PC6/7) GPIO ODR **有翻转**（子系统在驱动）；**灯色需肉眼确认**（halt 只抓单帧）
- [ ] SD card logging (SDMMC2 无响应，硬件问题)
- [ ] Frame configuration + motor test
- [ ] Clean up debug tracking code from source files
- [ ] Remove AP_INERTIALSENSOR_ALLOW_NO_SENSORS define (after IMU verified with real sensor data)
