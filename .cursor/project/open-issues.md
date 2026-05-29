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
