# AP_HAL_RTT 当前状态

> 基线：`CUAV v5` / `STM32F767` / `ArduCopter V4.7.0-dev on RT-Thread 5.3.0`
> 最后更新：2026-05-29（**CherryUSB 已设生产默认，milestone `85c4f83b3e`**；受控全量回归 PASS：双 backend 构建 IRQ 唯一 + L0 gate exit 0 + MAVFTP 6/6 + Mission PASS + 600s soak fault=0/无 IWDG + 3 轮全量参数 904×3；RCOut/RCIn/S_rc_chain 暂缓）

## 全量回归基线（2026-05-29，CherryUSB 默认）

> **边界**：以下建立在 milestone `85c4f83b3e`（CherryUSB 全量默认）。覆盖 USB/MAVLink L0、MAVFTP、Mission、长稳 soak、软重连、多轮参数；**不**覆盖 RCOut/RCIn/S_rc_chain（暂缓）、整机飞行链、物理 USB 拔插。

- **USB 默认化**：`rtt_usb_backend.py` 全量默认 `cherryusb`；`RTT_USB_BACKEND=native` 显式回退保留；两 backend 全量 ArduCopter 构建 PASS，`OTG_FS_IRQHandler` 各自唯一（cherry `080e7050` / native `080e2670`）
- **L0 gate**（`/tmp/cherryusb_main_l0_gate.sh --skip-build --skip-bl --json`）：**exit 0**；STANDBY(3)；**904/904** 参数；30s 流 1902 msg / 23 types；VTOR=`0x08008000`、CFSR/HFSR=0、IWDGRSTF=0
- **MAVFTP**：`tests/test_mavftp.py` **6/6 PASS**（首轮即过）
- **Mission**：`tests/test_mission_protocol.py` **PASS**（clear→upload 2→download→clear）
- **Soak ≥10min**：600.1s、47880 msg、~80 msg/s、HEARTBEAT 1208、全程 STANDBY、`max_silence=0`、无掉线；soak 后 VTOR/CFSR/HFSR=0、IWDGRSTF=0
- **USB 软重连**：3 轮（含 R3 重试）均 ~0.01s 恢复心跳；CDC 名恒为 `usb-APM_CUAV_V5_CDC_1_00001`→ttyACM1；3s 冷却即可；未做物理拔插
- **多轮参数**：独立连接 + drain 5s 流程下 **904×3 PASS**（22–28s/轮）；**方法论**：soak 后同连接未 drain 直接拉参数会不完整（测试方法问题，非固件故障）

## 分层驱动测试 — HAL / External / Subsystem 构建门禁（2026-05-29）

> **边界**：本节证明首批已登记的 **D\*/E\*/S\*** `scons --test=` 固件**可链接、可构建**；其中 **D_uart_hal / D_spi_hal / D_i2c_hal / D_storage / E_sdcard** 另有 **2026-05-29 上板证据**（见下节）。**不**表示整机 CDC MAVLink L0、RC 链或未恢复 SPIDEV 的 MS5611/FRAM 已上板通过。

- **manifest 自检（2026-05-29）**：旧 D/E 门禁 8 名已登记；非 RC 新增 9 名（`D_scheduler`、`D_analogin`、`D_usb_serial`、`E_imu`、`E_ms5611`、`E_fram`、`S_param_storage`、`S_sensors`、`S_mavlink_usb`）已登记并解析到 canonical test tree；`S_rc_chain` 未登记
- **串行构建（2026-05-29）**：`D_uart_hal`、`D_spi_hal`、`D_i2c_hal`、`D_storage`、`D_rcoutput`、`D_rcinput`、`E_sdcard`、`E_wspi_flash` — **8/8 PASS**（约 27s，增量/缓存）
- **非 RC 新增项统一复跑（2026-05-29）**：`D_scheduler`、`D_analogin`、`D_usb_serial`、`E_imu`、`E_ms5611`、`E_fram`、`S_param_storage`、`S_sensors`、`S_mavlink_usb` — **9/9 PASS**（构建）；**2026-05-29** cuav_v5 hwdef 恢复 `SPIDEV ms5611`/`ramtron` 后 **`D_spi_hal`/`E_imu`/`E_ms5611`/`E_fram`/`S_sensors` 串行构建 5/5 PASS**
- **非 RC 低风险项上板（2026-05-29）**：`D_scheduler`、`D_analogin`、`S_param_storage`、`E_imu` — **4/4 PASS**；独立 test 固件烧录 `0x08008000`，UART7 见 `RESULT: PASS`，CFSR/HFSR 均为 0。边界：`D_analogin` 为 HAL API smoke（ch6 SCALED_V3V3，`test_printf` 仅 `%lu`/mV，非全通道 ADC 校准）；`S_param_storage` 不是完整 `AP_Param`；`E_imu` 不是 `AP_InertialSensor` 全栈
- **实现形态（相对 2026-05-28 BUILD_ONLY）**：
  - **D_uart_hal / D_spi_hal / D_i2c_hal / D_storage / D_rcoutput / D_rcinput**：`main.cpp` 调用对应 `AP_HAL` API（serial、SPIDevice、I2CDevice、Storage、RCOutput、RCInput）；**非**单步 `test_runner` 占位
  - **D_scheduler / D_analogin**：真实 HAL smoke 已构建并上板通过；`D_scheduler` 验证 timer callback，`D_analogin` 验证 ch6 SCALED_V3V3 读数与 ADC 诊断
  - **E_imu / E_ms5611 / E_fram**：芯片层 smoke 已构建并上板通过；`E_imu` 是 WHO_AM_I/PWR_MGMT_1 薄测，`E_ms5611` 是 PROM/CRC，`E_fram` 是 FM25V02A RDID + scratch RW；均非完整 INS/Baro/参数持久化全栈
  - **S_param_storage / S_sensors / S_mavlink_usb**：首批非 RC 子系统 smoke 已构建；`S_param_storage` 非完整 `AP_Param`；`S_mavlink_usb` 为 HAL CDC + MAVLink HEARTBEAT（非 GCS 全栈）
  - **E_sdcard**：POSIX `/APM` 挂载轮询 + 文件 RW smoke（无卡 → `TEST_FAIL`）
  - **E_wspi_flash**：cuav_v5 运行时 **N/A**（hwdef 无 QUADSPI/WSPI）；构建 PASS 仅门禁
- **已知限制（构建 ≠ 上板；上板项亦见各条边界）**：无 RX/loopback（UART）；无 MS5611/FRAM 在部分 D/E 范围；`D_storage` 当前 **RAM stub**（非 FRAM 持久）；`D_rcoutput` **未**验证 PWM 波形；`D_rcinput` 无 SBUS/PPM → 运行失败；`E_sdcard` 已验证 SD/FS/POSIX smoke，但不等于日志长稳或整机 MAVFTP 全回归；详见 `driver-validation-matrix.md`
- **硬件状态（上板）**：**5/8** 已上板通过（`D_uart_hal`、`D_spi_hal`、`D_i2c_hal`、`D_storage`、`E_sdcard`）；`D_rcoutput`、`D_rcinput` **未上板**；`E_wspi_flash` @ cuav_v5 **N/A**；L3_uart/L4_spi 寄存器层 **未**在本轮重跑上板；`L7_cherryusb_cdc` 分层 USB echo 已上板通过
- **新增上板状态**：`D_scheduler`、`D_analogin`、`S_param_storage`、`E_imu`、`E_ms5611`、`E_fram`、`S_sensors`、`D_usb_serial`、`S_mavlink_usb` 已上板通过；**`D_usb_serial`**：ACM `1209:5741` @921600 beacon+echo；**`S_mavlink_usb`**：同链路 + pymavlink HEARTBEAT msgid=0（sys=1 comp=1）；二者 CFSR/HFSR=0；**非**整机 L0/参数

## 分层驱动测试 — Batch A 上板验收（2026-05-29）

> **验证边界**：每项独立烧录 **test 固件** 至 `0x08008000`（非全量 ArduCopter）；判据为 UART7 **115200** 日志 + OpenOCD **CFSR/HFSR=0**。**不**包含 USB CDC MAVLink、参数下载、RX/loopback、MS5611/FRAM、PWM 波形、SBUS、SD 插卡或 WSPI。

| `--test=` | 烧录 | UART7 关键日志 | Fault |
|-----------|------|----------------|-------|
| `D_uart_hal` | app @0x08008000 **Verified OK** | `[D_UART_HAL] RESULT: PASS`；`SERIAL6/UART7: write returned 24 bytes` | CFSR/HFSR **0** |
| `D_spi_hal` | **Verified OK** | ICM20689 `read_registers(0x75) -> ok, whoami=0x98`；`[D_SPI_HAL] RESULT: PASS` | CFSR/HFSR **0** |
| `D_i2c_hal` | **Verified OK** | IST8310 `wai=0x10`；`[D_I2C_HAL] RESULT: PASS` | CFSR/HFSR **0** |

- **串口路径**：CH343 → `/dev/serial/by-id/usb-1a86_USB_Single_Serial_*`（→ ttyACM0），**115200**；复位后 pyserial 读 14–18s（纯 `cat` 易 0 字节）
- **OpenOCD**：`program ... verify reset` 后 **须 exit**（`pgrep openocd` 为空）；`resume` 可能 warn target not halted（exit=1），不阻断 Verified OK
- **未覆盖**：`D_uart_hal` 无 RX/loopback；`D_spi_hal` 无 MS5611/DMA；`D_i2c_hal` 无 `AP_Compass` 全栈

## 分层驱动测试 — Storage / SD / USB 上板验收（2026-05-29）

> **验证边界**：以下均为独立 test 固件烧录至 `0x08008000`，不是全量 ArduCopter；OpenOCD `resume` 偶发 `target not halted` 但烧录 `Verified OK` 且检查后无残留进程。

| `--test=` | 烧录/运行证据 | Fault | 边界 |
|-----------|----------------|-------|------|
| `D_storage` | SCons PASS；UART7 `=== RTT LAYERED TEST: D_STORAGE ===`、`readback bytes: a5 5a c3 3c 96 69 0f f0`、`[D_STORAGE] RESULT: PASS` | CFSR/HFSR **0** | HAL Storage tail-8B scratch RW+restore；cuav_v5 当前为 RAM stub，**非 FRAM 持久化**；不测 SD |
| `E_sdcard` | 首轮失败暴露 SD 根因：SDIO timeout + `dfs_mount("sd0","/sdcard","elm") failed` + `/APM` 不存在；修复后 SCons PASS，UART7 `[sd] mounted sd on / ok`、`mount ok: stage=10 result=0`、`=== [E_SDCARD] RESULT: PASS ===`（约 615ms） | CFSR/HFSR **0** | 已验证 microSD 插卡场景下 DFS/ELM-FAT/POSIX `/APM` 文件写读删；不等于长稳 logging |
| `L7_cherryusb_cdc` | SCons PASS；USB 枚举 `1209:5741 Generic L7 CherryUSB`；by-id `usb-PogoAPM_L7_CherryUSB_0001` → ttyACM1；pyserial 写 `HELLO_L7\r\n` 并收到同样回显，`ECHO_OK True` | CFSR/HFSR **0** | CherryUSB CDC echo 分层门禁；不等于全量 MAVLink L0、参数多轮或 USB 重连长稳 |

### E_sdcard 根因修复事实

- 根因链：`rt_hw_sdio_init` 在卡供电 PG7 之前运行，且 `board/ports/sdcard_port.c` 额外重复 `rt_hw_sdio_init()` 并挂载 `/sdcard`，与 `rt_board_init.c` 的根挂载 `/` + `/APM` 语义冲突。
- 修复：`rt_board_init.c` 使用 `INIT_PREV_EXPORT` 提前 `VDD_3V3_SD_CARD_EN`，只保留后台 `sdmnt` 线程挂载 `sd`/`sd0` 到 `/` 并创建 `/APM/{LOGS,TERRAIN,STORAGE,scripts}`；`sdcard_port.c` 保留为空兼容单元，避免重复 init/mount。
- 仍可观察：启动早期可能有短暂 `[E/drv.sdio] wait completed timeout`，只要随后 `stage=10 result=0` 并 `E_sdcard` PASS，不作为失败。

## 构建与 Cherry 冒烟门禁（2026-05-28，composer-2.5 门禁跑通）

> **边界**：本节只证明**历史遗留清理后**，分层测试构建、native/cherryusb 双 backend 全量编译与 CherryUSB 显式 backend 硬件冒烟**可复现**。**不**表示可进入全量验证（长稳 soak、USB 重连/多轮参数、Cherry 生产默认化、工作区大改拆分提交等仍见 `open-issues.md`）。

- **分层模块测试构建/上板（`libraries/AP_HAL_RTT/test/`，`scons --target=cuav_v5 --test=`）**：
  - `L0_system` **PASS**（ROM ~110KB）
  - `L4_spi` **PASS**（首次与 `L7` 并行 scons 时链接失败，单独重跑 **PASS**）
  - `L7_cherryusb_cdc` **构建 PASS + 上板 PASS**（2026-05-29；`1209:5741 Generic L7 CherryUSB` + ttyACM1 echo OK；CherryUSB 分层门禁固件）
- **全量 ArduCopter 构建**：
  - 默认 **native**（未设 `RTT_USB_BACKEND`）：`python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**（ROM ~83.5%）
  - 显式 **cherryusb**：`RTT_USB_BACKEND=cherryusb python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**（ROM ~83.9%；`OTG_FS_IRQHandler` **唯一**）
- **CherryUSB 硬件回归（本机，未改默认 backend、未烧录 gate 外新镜像）**：
  - `/tmp/cherryusb_main_l0_gate.sh --skip-build --skip-flash --skip-bl --json` → **exit 0**（STANDBY、904/904 参数、30s 2260 msg / 22 types、post-L0 fault 清零）
  - `tests/test_mavftp.py` → **6/6 PASS**（首轮 4/6 后 CDC 瞬断，间隔重试通过）
  - `tests/test_mission_protocol.py` → **PASS**

## USB 栈候选（2026-05-28，L0 + MAVFTP）

> **验证边界**：本节已覆盖 **USB CDC 枚举 + MAVLink 心跳/参数/短流 + OpenOCD 无 HardFault**（L0）、**MAVFTP 综合回归** 与 **Mission protocol smoke**。**不**代表 USB 重连、多轮参数下载、长稳 soak 或 Cherry 默认化已完成。

### 生产默认（主仓树，已长期验证）

- 全量 ArduCopter 默认 **`RTT_USB_BACKEND` 未设 → `native`**：`hal_usb_lld_rtt.c` + `usb_cdc_rtt.c`，VID/PID **1209:5741**
- 下列 April 基线（参数 941/943、MAVFTP 6/6、Mission smoke、USB 重连等）均建立在 **native 栈** 上，**不能**自动外推到 Cherry 未默认化或未跑全功能回归前的状态

### CherryUSB（主线候选，主仓显式 backend L0 已通过）

- 构建：须 **`RTT_USB_BACKEND=cherryusb` 显式指定**（默认仍为 native，未切换生产默认）
- **主仓 L0 已通过（2026-05-28，本机构建+烧录+gate）**：
  - 三类补丁合仓：CherryUSB RX **8×64B ring**（ISR enqueue / poll drain）；UARTDriver 背压**仅真断开**才 `_writebuf.clear()`；SPIDevice **每总线 `rt_mutex`**（替代长段 `__disable_irq`）
  - 并行构建：`Tools/scripts/rtt_ar_archive.py` + `TempFileMunge`（`MAXLINELENGTH=8192`）修复 ARG_MAX；`RTT_USB_BACKEND=cherryusb python3 -m SCons --target=cuav-v5 -j8` **PASS**；ELF **唯一** `OTG_FS_IRQHandler`
  - `lsusb` **1209:5741**；by-id **`usb-APM_CUAV_V5_CDC_1_*`**（与历史 `usb-ArduPilot_*` 别名并存，gate 脚本已兼容）
  - MAVLink：**HEARTBEAT → STANDBY(3)**、`FORMAT_VERSION=120.0`、**904/904** 参数、30s 流 **1798 msg / 22 types / 60 HEARTBEAT**
  - Post-L0 OpenOCD：**VTOR=0x08008000**、**CFSR/HFSR=0**、**RCC_CSR IWDGRSTF=0**
  - Gate：`POGO_APM_ROOT=... RTT_USB_BACKEND=cherryusb /tmp/cherryusb_main_l0_gate.sh --skip-build --skip-bl --json --wait 30` → **exit 0**
- **诊断计数（不阻断 L0）**：`rtt_uart_usb_diag_write_fails` 观测值约 **20593** — 只读评估为 **TX 背压诊断计数**，非功能失败；列入长稳/性能观察（见 `open-issues.md`）
- **主仓 MAVFTP 已通过（2026-05-28，CherryUSB 显式 backend）**：
  - 根因修复：RTT `stat()` ABI mismatch 曾导致 MAVFTP Create 本地文件路径栈破坏并引发 CDC 重枚举/HardFault；已通过 `ap_rtt_posix_stat()` C 兼容层修复
  - 回归命令：`MAVFTP_PORT=/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 python3 tests/test_mavftp.py` → **6/6 PASS**
  - 覆盖项：`ListDirectory /`、`ListDirectory /APM`、`@PARAM/param.pck`、真实文件 `Create/Write/OpenRO/Read/Delete`、`ResetSessions`、post-FTP 稳定性
  - Create-only 复核：唯一文件名 `/APM/statfix_213003.tmp` Create **Ack**，post HEARTBEAT **STANDBY(3)**；随后 Remove **Ack**
  - Post-MAVFTP OpenOCD：`CFSR/HFSR=0`，HardFault 记录为 0；固定文件名 `Nack err=2 errno=254` 可由文件残留/状态解释，不再代表 Create 触发崩溃
- **主仓 Mission protocol smoke 已通过（2026-05-28，CherryUSB 显式 backend）**：
  - 命令：`python3 tests/test_mission_protocol.py --port /dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 --baud 115200`
  - 结果：`MISSION_CLEAR_ALL -> MISSION_COUNT(2) -> MISSION_REQUEST loop [0,1] -> MISSION_ACK -> REQUEST_LIST/download -> final CLEAR_ALL` 全部 PASS
- 历史 worktree `cherryusb-85f5a6da` 证据仍有效，但**当前权威基线为主仓树上述 gate**

### TinyUSB（备选栈）

- worktree `tinyusb-l0-097421d9` 在 **并发补丁**（`rt_hw_interrupt_disable` 串行化 poll/send；`tud_mounted` 门控；send 内禁止 `tud_task`/`mdelay`）后，**同 gate L0 已通过**（912 参数、30s 流、无 fault）
- **已知问题（主仓最小合入路径）**：`CFG_TUSB_OS=OPT_OS_NONE` 下 `usb_lld_send_rtt` 与 `OTG_FS_IRQHandler` 重入 → 参数阶段 `deadbeef` / USB 断开（约 12s）；补丁可缓解，长期可考虑 `OPT_OS_RTTHREAD` + osal
- 父代理裁决：**CherryUSB 主线、TinyUSB 备选**；主仓默认仍 **native**

### USB 构建制品（历史遗留清理后，2026-05-28）

- **已 staged / 工作区就位**：`Tools/scripts/rtt_usb_backend.py`、`thirdparty/cherryusb/`、`cherryusb_board/`、`hal_usb_cherryusb_shim.c`、`libraries/AP_HAL_RTT/test/`（L0–L7）、`archive/stray-bsp/`
- **已消除**：HAL 根下误拷贝 Cherry 五件套（`class/common/core/osal/port/cherryusb`）**不在磁盘**；`libraries/AP_HAL_RTT/SConscript` **不存在**（inventory 中 cherryusb group 断点已过时）
- **生产默认仍为 native**；Cherry 须 `RTT_USB_BACKEND=cherryusb` 显式指定 — 默认化决策未做

## 历史遗留清理（2026-05-28，子任务 2 已实施）

> **验证边界**：以下经 **Git 索引整理 + 双 backend 全量 scons PASS**（本子任务未重跑构建；证据来自子任务 2 构建输出）。**不**包含全量验证或硬件新回归。

- **旧 BSP / `.bak` 索引**：`git rm` 已清除 index 中 `rtt_bsp_fmuv2`、`rtt_bsp_pixhawk6c_mini` 与三份 `*.bak`（工作区文件此前已删）
- **归档**：`libraries/AP_HAL_RTT/archive/stray-bsp/` 已 **staged**（pixhawk6c_mini / fmuv2 整树）；**CUAV v5 生产路径不读此目录**（仍走 `hwdef/common`）
- **USB clean set**：`rtt_usb_backend.py`、`thirdparty/cherryusb`、`cherryusb_board`、`hal_usb_cherryusb_shim.c`、`test/` 等已 **staged**（入库边界收口，待用户要求时 commit）
- **旧路径文档**：`board-matrix.md`、`command-catalog.md`、`Tools/ardupilotwaf/rtt.py` docstring 已改为 `hwdef/common` → `build/rtt_deploy/cuav_v5`（不再写 `rtt_bsp_cuav_v5`）
- **SPI 双份 LLD**：`scons_ardupilot_sources.py` 已排除根级 `hal_spi_lld.c` / `hal_spi_lld_rtt.c`；生产 SPI 走 `hwdef/common` 的 `drv_spi_lld` + CMSIS
- **双构建 PASS**（子任务 2）：默认 native 与 `RTT_USB_BACKEND=cherryusb` 的 `python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` 均通过；ELF 无 `rtt_spi_lld` / `spi_lld_*_rtt` 符号

## AP_HAL_RTT 目录布局（2026-05-28）

- **CUAV v5 scons 基线**不依赖 HAL 根目录下的 `rtt_bsp_*` 整树；部署走 `hwdef/common` + `hwdef/cuav_v5`（`Tools/scripts/rtt_bsp_deploy.py` hwdef 模式）。
- 旧整树 BSP **`rtt_bsp_pixhawk6c_mini`**、**`rtt_bsp_fmuv2`** 已移至 `libraries/AP_HAL_RTT/archive/stray-bsp/`；脚本（legacy pixhawk deploy、waf `rtt.py`）指向归档路径。
- **归档 ≠ 已验证**：pixhawk6c_mini / fmuv2 未在当前 HAL/USB 栈上完成实机回归；迁移到 `hwdef/common` 仍为开放项。

## 当前稳定成立的事实

- `boot_stub -> Reset_Handler -> RTT app -> scheduler -> main -> hal.run()` 主链路已跑通
- **boot_stub**：自制 36 字节最小向量表+跳转 stub 烧录在 0x08000000，读取 app 向量表并跳转
- **HAL_Init() 已移除**：替换为 FLASH ART enable + NVIC priority grouping 直接寄存器操作
- **SysTick 已修正**：在 SystemClock_Config() 后调用 rt_hw_systick_init()，保证 216MHz 下正确计时
- **MPU 寄存器化**：直接写 MPU->RNR/RBAR/RASR，非 Shareable（S=0）避免 STM32F7 AXI 全局独占监视器 PRECISERR
- **Flash.cpp 寄存器化**：Flash erase/write 均使用直接寄存器操作，消除 HAL_FLASH_* 依赖
- **HEAP/BSS 安全**：HEAP_BEGIN = max(&_end, SRAM1_START)，&_end 在 .bss 和 .sram1_bss 段之后，防止 cache_buf 等 DMA 缓冲区与堆重叠
- **栈大小**：MSP 初始栈 16KB（link.lds _system_stack_size = 0x4000）
- **开发环境**：Ubuntu 24.04 物理机（kernel 6.17.0-19-generic），OpenOCD 0.12.0 直连 ST-Link V2
- **启动文件修复**：自定义 `startup_rtt_override.S` 覆盖 CMSIS 弱 `Reset_Handler`，跳过 `__libc_init_array`（C++ 全局构造函数由 `INIT_COMPONENT_EXPORT` 在堆初始化后执行），调用 `entry()` 进入 RT-Thread 标准启动链路
- `CUAV v5` 上的 USB CDC / MAVLink 已在 Ubuntu 物理机、Windows 侧验证到可用
- SPI 传感器链已带起，`BMI055`（IMU）与 `MS5611`（Baro）形成有效数据路径
- **SPI LLD**（`drv_spi_lld.c/h`）：低层 DMA 驱动替代 STM32 HAL 在 ISR 内的 busy-wait；SPI1（IMU）已注册 LLD 上下文
- **SPI4/SPI2 DMA 已禁用**：改用轮询模式（HAL SPI DMA 完成中断不工作，根因待查）；MS5611 @20MHz 轮询足够
- **ROM 优化**：RTT 板子使用 -Os（节省约 40%），禁用 HAL_ETH_MODULE（无需以太网），总 ROM ~1.27MB / 2016KB
- **SPI 短传输 LL 化**：`<16B` 传输使用 `spi_xfer_poll_ll()` 直接寄存器轮询替代 HAL 状态机（~8 指令/字节 vs ~50 指令/字节）
- **GPIO 直接寄存器**：`drv_gpio.c` 的 `stm32_pin_write`/`stm32_pin_read` 已替换为 BSRR/IDR 直接操作
- **USB CDC TX 事件驱动**：UART 线程使用 `rt_sem_take(_uart_wake_sem, 1ms)` 替代固定 `rt_thread_mdelay(1)`；`UARTDriver::_write()` 写入后立即唤醒
- **CherryUSB FIFO 增大**：CDC IN TX FIFO 64→128B（允许双包队列）；CDC 软件环形缓冲 2048→4096B（`usbd_serial.c`）；TX ring buffer 2048→8192B（`rtconfig.h CONFIG_USBDEV_SERIAL_TX_BUFSIZE`）
- I2C3 软件驱动已初始化，`IST8310`（磁力计）已识别
- 已观测到 23 种 MAVLink 消息类型（HEARTBEAT、ATTITUDE、RAW_IMU、SYS_STATUS 等）
- **MAVLink 参数下载**：**943 全部完成**（FTP 协议，快速）
- **MAVFTP 综合回归通过**：`tests/test_mavftp.py` 在 Ubuntu 物理机 `/dev/ttyACM1` / CherryUSB by-id 上 **6/6 PASS**（根目录列举、`@PARAM/param.pck`、真实文件 Create/Write/OpenRO/Read/Delete、ResetSessions、稳定性）
- **Mission 基础协议 smoke 已通过**：`tests/test_mission_protocol.py` 已在真实硬件上完成 `MISSION_CLEAR_ALL -> MISSION_COUNT -> REQUEST/ITEM -> MISSION_ACK -> REQUEST_LIST -> 下载 -> CLEAR_ALL` 闭环
- 主循环频率：**~400Hz 稳定运行**（DeviceBus 重构 + 10kHz SysTick + OS sleep 优化后）
- **CPU 真实利用率 ~1%**（DWT idle hook 测量），ArduPilot `load_average()` 已改用 DWT 数据源
- MAVLink 参数下载：**941 params / 10.9s**
- **DTCM 与 DMA**：STM32F767 的 DTCM（0x20000000–0x2001FFFF）不可被 DMA 访问
- **内存布局**：.data+.stack 在 DTCM，.bss 从 DTCM 溢出到 SRAM1，堆从 _ebss 之后开始
- 参数持久化：Flash 后端验证闭环（on-chip Flash page 10-11, 0x08180000）
- D-Cache 已启用、编译优化 `-O2 -Os`、无阻塞性 `rt_kprintf`
- **信号量语义已对齐 ChibiOS**：`take(0)` / `wait(0)` = 非阻塞；`take_blocking()` / `wait_blocking()` = 永久阻塞
- **校准功能已验证**：加速度计校准、Level 校准均可正常完成，地面站不再冻结
- **SD 卡文件系统支持已实现**：SDMMC1 驱动 + ELM-FAT + DFS 挂载 `/sd`，ArduPilot 目录已创建
- **Filesystem Logging 已启用**：`HAL_LOGGING_FILESYSTEM_ENABLED=1` + `AP_FILESYSTEM_POSIX_ENABLED=1` + `statfs` 支持
- **RTT POSIX `stat()` ABI 已加兼容层**：`AP_Filesystem_Posix::stat()` 在 RTT 上不再把 C++ 侧 60B `struct stat` 直接传给 DFS/ELM-FAT；改为先经 C 编译单元 `ap_rtt_posix_stat()` 以 RT-Thread 原生 `struct stat` 调 `stat()`，再把共用字段拷回 C++，避免本地文件 `stat()` 踩坏相邻栈数据
- **`AP_Scripting` 延时 HardFault 已跨过 80s 观测窗**：先将 RTT `log_io` 栈从 2KB 提升到 4KB 修掉第一层 `thread_timer.timeout_func=0 -> rt_timer_check()->blx 0`；随后把第二层 `log_io` / `f_stat()` 路径上的 RTT `stat()` ABI mismatch 修掉后，固件在两次单次 GDB 检查中分别跑过约 40s 和约 80s，`main_loop_iterations` 从 `0x33eb` 增到 `0x8329`，`rtt_dbg_hardfault_*` 保持为 0
- **UART7 已有 `ap_rate` 调试命令**：可直接在 `msh` 中打印 `cpu_idle/load/loop_us/loop_hz/work_us/overrun/iterations`，用于不接 GDB 时快速判断 CPU 是否真忙、主循环是否真降频
- **RTT Copter 默认 MAVLink 流率已加运行时 fallback**：若启动时检测到 `streamRates[]` 整组仍为 0，则仅在内存中为 `RAW_SENS/EXT_STAT/RC_CHAN/POSITION/EXTRA1/EXTRA2/EXTRA3` 填入默认值，再初始化 message intervals，不改持久化参数
- **MAVLink 消息频率三层修复**（2026-03-31）：(1) 主循环末尾显式 `call_delay_cb()` 弥补 RTT `delay_microseconds_boost()` 不触发 delay callback 的差异；(2) `should_send_message_in_delay_callback()` 对 RTT 返回 true 允许所有消息类型在 delay callback 中发送；(3) USB CDC TX ring buffer 从 2048B 增大到 8192B + `_usb_write_fail_count` 逻辑修正（仅 buffer 非空时递增）。修复后默认流率 96.6 msgs/s，ATTITUDE 13.3Hz
- **IWDG 独立看门狗骨架已就位**（2026-03-31）：LSI enable + /256 prescaler + 10s reload；`watchdog_pat()` 中 kick；`set_system_initialized()` 中启动（当前 `#if 0` 暂禁用，因 GDB 无法在 reset loop 中 halt）；`was_watchdog_reset()` 读 RCC_CSR IWDGRSTF/WWDGRSTF 标志
- **RAW_IMU 加速度修复已验证**（2026-04-03）：`send_raw_imu()` 改用 `AP::ahrs().get_primary_accel_index()` 取主 IMU 索引而非硬编码实例 0，加速度字段不再为 0
- **PWM 输出 TIM1/4/12 全部运行**（2026-04-03）：所有 8 通道已通过 RTT PWM 框架初始化；TIM1（CH1-4）、TIM4（CH1-4）、TIM12 均已启用 PWM 模式
- **SD 卡挂载改为非阻塞**（2026-04-03）：从同步 60s 阻塞改为 `INIT_APP_EXPORT` 后台线程挂载，不阻塞主启动链路；挂载点 `/sd`，自动创建 `/sd/APM/{LOGS,TERRAIN,STORAGE,scripts}`
- **构建系统 drv_pwm.o/drv_tim.o 链接修复**（2026-04-03）：解决 PWM 驱动对象从 shared HAL drivers 路径正确链接
- **boot 序列修复**（2026-04-03）：`main()` 入口已正确启动，`boot_stub -> Reset_Handler -> RTT app -> scheduler -> main -> hal.run()` 主链路稳定
- **低频定位已闭环**：CPU 真实空闲仍约 98-99%，主循环稳态 `loop_us` 约 2185us（~457Hz）；修复前默认 `ATTITUDE/RAW_IMU/SYS_STATUS` 仅约 `0.07/0.33/0.33 Hz`，经三层修复后默认流率提升到 `13.3/5.4/4.1 Hz`（总 96.6 msgs/s）
- **RTT MAVFTP OpenFileRO 本地文件路径**：对真实文件改为 **open-first + `lseek(SEEK_END)`** 取大小；`@PARAM` 等虚拟后端仍保留原 `stat()+open()` 路径
- **MAVLink Logging 已禁用**（`HAL_LOGGING_MAVLINK_ENABLED=0`），避免无客户端时 PreArm 失败
- **UART7 调试串口已启用**：PE8=TX / PF6=RX / 115200，RT-Thread msh 控制台已切到 UART7
- AnalogIn `_timer_tick()` 已挂入 Scheduler 1kHz 路径
- **USB CDC 重连稳定**：2s 间隔 5/5 成功（含数据流），纯心跳 13/15 成功
- **CherryUSB DTR 处理已完善**：DTR set/clear 回调中正确 reset TX 状态 + `usbd_ep_recover_stuck`
- **Logging PreArm 已解除**：只剩 "PreArm: RC not found"（预期行为）
- **Lua hello 自动化已通过**：`tests/test_lua_hello.py` 现已在 `/APM/scripts/hello_world.lua` 路径下完成上传、`SCR_ENABLE=1`、重启重连，并收到 `STATUSTEXT: hello, world`
- **构建链路**：支持 `git clone --recursive` 后一条命令全量编译；`.gitmodules` 中 rt-thread 已指向 pogo fork；`rtt_bsp_deploy.py` 自动下载 packages；`SConscript` 自动创建 `ap_config.h`
- **newlib polyfill**：`rtt_libc_compat.c` 提供 `asprintf` / `vasprintf` / `memmem`；`hwdef.h` 中含对应 `extern "C"` 声明
- **LL/寄存器级 BSP 驱动层**（`board/drivers_ll/`）：已实现 6 个驱动（clock/common/gpio/usart/spi/flash），均使用 LL 库或直接寄存器操作，不依赖 STM32 HAL
- **分层模块测试体系**（`libraries/AP_HAL_RTT/test/`）：bring-up L*、USB L7、**D\*/E\*** 已登记 manifest；**2026-05-29** 8 个 D/E **构建 PASS**；`D_uart_hal`/`D_spi_hal`/`D_i2c_hal`/`D_storage`/`E_sdcard` **已上板 PASS**，`L7_cherryusb_cdc` 分层 CDC echo **已上板 PASS**（见 matrix/status 验收表）；RCOut/RCIn 仍未上板
- **BMI055 IMU 验证**：通过 LL SPI1（6.75MHz, Mode 3）读取 BMI055 加速度计（ID=0xFA, Z≈1g）和陀螺仪（ID=0x0F），polled 采样率 ~27kHz；**关键经验**：SPI1 上 5 个传感器共线，必须将所有 CS 拉高

## 线程模型（重构后）

| 线程 | 优先级 | 栈大小 | 用途 |
|------|--------|--------|------|
| main | 8(boost)/10(normal) | 32KB | ArduPilot 主循环 |
| ap_timer | 4 | 8KB | 1kHz 定时器回调 |
| ap_uart | 10 | 4KB | UART 刷新 |
| SPI1 | 5 | 8KB | SPI1 总线回调（BMI055 accel+gyro + ICM 等，共享单线程） |
| SPI4 | 5 | 8KB | SPI4 总线回调（MS5611 Baro） |
| ap_io | 16 | 8KB | IO 线程 |
| storage | 18 | 2KB | 存储后端 |
| tshell | 20 | 4KB | msh 控制台（UART7） |
| cpumon | 31 | 1KB | DWT idle hook CPU 测量（每秒更新） |
| tidle0 | 31 | 256B | 空闲线程（DWT idle 计数在此执行） |

**DeviceBus 重构**：从"每回调一线程"改为 ChibiOS 式"每物理总线一线程 + 回调链表"。SPI1 上的 bmi055_a 和 bmi055_g 共享同一线程，消除相位漂移和 SPI 总线竞争。

## 内存使用

- RAM：约 141KB / 512KB（27.02%）
- ROM：约 1271KB / 2016KB（63.1%，-Os 优化后从 2.06MB 降至 1.27MB）

## 设备列表（实测）

- `uart7` — 控制台（ref=2）
- `uart3` — 原控制台（ref=3）
- `usb-acm0` — USB CDC MAVLink（ref=3）
- `sd0` / `sd` — SD 卡 Block Device
- `spi1` / `spi2` / `spi4` — SPI 总线
- `spi11-15` / `spi21` / `spi41` — SPI 子设备
- `i2c3` — 软件 I2C 总线
- `pin` — GPIO

## 与 ChibiOS 对齐度

整体约 **97%**（以 fmuv5/CUAV V5 上 ChibiOS HAL API 覆盖为尺度）

### 已对齐模块（本轮新增标 ★）

#### Scheduler ★
- 完整线程模型：monitor(3) + timer(4) + rcout(4) + rcin(5) + uart(9) + io(16) + storage(18)
- `disable_interrupts_save()` / `restore_interrupts()` 基于 `rt_hw_interrupt_disable/enable`
- `calculate_thread_priority()` 完整 priority_base 映射表（BOOST/MAIN/SPI/I2C/CAN/TIMER/RCOUT/RCIN/LED/IO/UART/STORAGE/SCRIPTING/NET）
- `watchdog_pat()` + `last_watchdog_pat_ms`
- monitor 线程：500ms 主循环卡死检测 → `AP::internalerror()`，GPIO `timer_tick()` 调用

#### Semaphores ★
- `take(0)` = HAL_SEMAPHORE_BLOCK_FOREVER = 永久阻塞（对齐 ChibiOS 语义）
- `BinarySemaphore::signal_ISR()` ISR 安全信号
- `Semaphore::check_owner()` / `assert_owner()`
- `BinarySemaphore::wait()` 60ms 分段循环避免 16 位定时器溢出

#### GPIO ★★
- `valid_pin()`, `pin_to_servo_channel()`, `wait_pin()`
- `timer_tick()` ISR 洪泛检测 + `arming_checks()` 报告
- **RGB LED GPIO 直驱已修复**（2026-04-04）：PH10/11/12 open-drain + BSRR 直写 + HAL_GPIO_LED_ON=0
- **I2CDeviceManager bus_mask 扩展**：0x04→0x07，i2c3 纳入内部总线（IST8310 探测）

#### RCOutput ★
- `timer_tick()` 在独立 rcout 线程（50Hz PWM 模式）
- `set_default_rate()`, `set_output_mode()` / `get_output_mode()`
- `timer_info()` 调试输出
- DShot/BDShot/串口ESC/LED 等高级功能使用基类安全默认值

#### RCInput ★
- 独立 rcin 线程 1kHz 调用 `_timer_tick()`
- `pulse_input_enable()`, `get_rssi()`, `get_rx_link_quality()`
- RSSI/link_quality 从 AP_RCProtocol 实时更新

#### UARTDriver ★
- `set_options()` / `get_options()`, `configure_parity()`, `set_stop_bits()`
- `set_RTS_pin()` / `set_CTS_pin()`, `set_unbuffered_writes()`
- `get_usb_baud()` / `get_usb_parity()`, `disable_rxtx()`
- `receive_time_constraint_us()`, `get_total_tx/rx_bytes()`

#### I2CDevice ★
- `set_address()` override
- `clear_bus()` / `clear_all_buses()` 静态方法

#### AnalogIn ★
- `accumulated_power_status_flags()` 累积电源状态

#### Util ★
- `safety_switch_state()` → SAFETY_ARMED（无独立安全开关时）
- `toneAlarm_init()` / `toneAlarm_set_buzzer_tone()` 桩
- `was_watchdog_reset()`, `malloc_type()` / `free_type()`
- `get_random_vals()` 使用 STM32 硬件 RNG
- `set_soft_armed()`

#### Storage ★
- `get_storage_ptr()` 直接返回内存缓冲区指针

#### 前期已对齐（未变）
- 启动链路（boot → main → scheduler）
- USB CDC / MAVLink 通信（含断线重连）
- CPU 负载（DWT idle hook，真实 ~1%）与主循环节拍（check_called_boost）
- SPI 传感器数据（IMU/Baro）+ SPI LLD DMA
- I2C 传感器（IST8310 磁力计）
- 参数持久化（Flash）、时间 API（DWT+tick）
- 校准流程、SD 卡、Filesystem Logging
- AnalogIn 1kHz 采样、UART7 msh、GPIO usb_connected()
- USB CDC DTR 处理

#### IO 线程 ★★
- IO 线程内 SD `retry_mount(3s, disarmed)` 与 ChibiOS _io_thread 对齐
- `_check_stack_free(5s)` 遍历所有 RT-Thread 线程，栈余量 < 64B 报 `stack_overflow`
- `reboot()` 前 `StopLogging` + `unmount` + `force_safety_on`（防 SD 损坏）

#### system.cpp ★★
- `millis16()` / `micros16()` 实现
- `panic()` 打印后禁中断死循环（对齐 ChibiOS）
- HardFault/BusFault/UsageFault/MemManage 处理函数

#### Filesystem ★★
- `AP_FILESYSTEM_POSIX_HAVE_STATFS=1`（RT-Thread DFS 的 `statfs()` 已可用）
- `disk_free()` / `disk_space()` 返回实际值而非 -1

#### HAL 启动顺序 ★★
- `scheduler->init()` → `serial(0)->begin()` → `analogin->init()` → `setup()`
- 主循环 yield 50µs（对齐 ChibiOS）
- `watchdog_pat()` 每次 loop 末尾调用

#### UARTDriver CDC 改进 ★★
- `_usb_write_fail_count` 逻辑修正：仅在 buffer 非空时递增，buffer 清空时重置为 0（修复无条件递增导致每 ~100ms 清空写缓冲丢数据的 bug）
- CDC max_chunks 提升到 8（4096B/tick），提高突发吞吐
- UART7 msh 诊断：每 5s 打印 `[USB0] wb_avail/fails/clears`

#### MAVLink 压测基线
- 默认流率：96.6 msgs/s（ATTITUDE 13.3Hz, RAW_IMU 5.4Hz）
- S1 参数全量下载 × 5 轮：≥4/5 PASS（单连接 12-30s/轮）
- S2 断连重连 × 10 轮：≥8/10 PASS
- S3 10 分钟长流：26.4 msg/s, CPU 0.9%, 无内存泄漏
- Integration 6/6 ALL PASS：2224 msgs/60s (37.1/s), max_gap 0.70s

### 未对齐部分（硬件或板级依赖）
- CAN（硬件不需要）
- IOMCU（✅ 已验证通过：ROMFS pipeline + UART8 通信 + MOTOR_OUTPUTS present+healthy）
- DShot 完整协议（需 DMA + 定时器捕获，当前桩返回安全默认）
- UARTDriver DMA TX/RX（当前用环形缓冲 + 设备框架）
- PPM 脉冲捕获硬件路径（当前走串口协议 RC）
- 看门狗 IWDG 实际启用
- Shared_DMA / bounce_buffer（RTT 由 BSP 驱动层管理 DMA）

## 当前已知限制

- SYS_STATUS load=9 (0.9%)，真实 CPU 空闲 99%（DWT idle hook），主循环 ~400Hz 稳定
- `SET_MESSAGE_INTERVAL` 设置特定频率后反而退化（96.6 → 16.2 msgs/s），可能与 `scheduler_delay_callback` 的 20ms 间隔限制有关
- `mmcsd_detect` 线程栈使用率 90%，接近溢出
- `EKF3` 仍存在内存压力，允许回退到 `DCM active`
- IWDG 看门狗暂禁用（需 GDB 调试 prescaler/timeout 配置）

## 当前活跃待验证项（2026-04-04）

- ~~**SD 卡 SDMMC1 已验证通过**~~：rtt_sd_mount_stage=10, rtt_sd_mount_result=0（成功挂载）；2026-05-29 `E_sdcard` 分层测试复核 `/APM` POSIX 写读删 PASS，但整机 logging 长稳仍需单独验证
- **RGB LED（PH10/11/12）**：**已修复** 2026-04-04。三个问题：(1) `HAL_GPIO_LED_ON=1` 应为 `0`（active-low open-drain，ChibiOS 默认=0）；(2) OTYPER 未设 open-drain（ChibiOS hwdef 用 OPENDRAIN）；(3) rt_pin_write 不可靠 → 改用 BSRR 直写。修复后 ODR 在 0xFFFF↔0xF3FF 间切换，黄色闪烁（pre-arm failing）已确认。LED 是 GPIO 驱动，不是 IS31FL3195 I2C。
- **RCInput SBUS 验证**：SBUS 串口协议路径已实现但未实机验证
- **Servo 输出验证**：PWM TIM1(50Hz)/TIM4(100Hz) 已运行，CCR=0（未解锁状态正常），需通过 GCS 命令实际驱动电调/舵机验证
- **IOMCU ✅ 已实机验证**（2026-04-12）：ROMFS pipeline 完成（`rtt_hwdef.py` → `embed.py` → `ap_romfs_embedded.h`），`io_firmware.bin` 成功嵌入固件。烧录后 MAVLink 验证：SYS_STATUS `MOTOR_OUTPUTS` present + healthy，RC_CHANNELS 19 条消息（chancount=0 = 无RC接收器正常）。`HAL_WITH_IO_MCU=1` 已启用。详见 open-issues.md

## 调试方法论

- **ChibiOS 对比法**：当遇到无法仅通过软件调试解决的硬件问题时，刷 ChibiOS CUAV V5 固件做对比测试。ChibiOS 是 ArduPilot 在 STM32 上的参考实现，若 ChibiOS 下硬件同样不工作则可排除固件问题
