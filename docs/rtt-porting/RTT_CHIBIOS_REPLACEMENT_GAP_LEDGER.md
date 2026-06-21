# RTT 替代 ChibiOS 差距台账

目标：持续查找并修复 ArduPilot RTT 移植相对 ChibiOS 的缺陷和不足，最终达到无缝替换 ChibiOS 的工程质量。本文不是完成声明，而是长期台账；每一项都需要证据、修复、验证，不能用整机偶然 GREEN 代替。

当前基线：

```text
branch: issue/rtt-spi-lld-real-activation
commit: 47e1145df7 AP_HAL_RTT: stabilize CUAV V5 loop rate
board: CUAV V5 / STM32F767
```

状态定义：

| 状态 | 含义 |
|------|------|
| OPEN | 未修复或未验证 |
| FIXED | 已有修复并通过对应门禁 |
| PARTIAL | 有修复或证据，但覆盖不完整 |
| DOC | 文档/流程/验收一致性问题 |
| HW | 需要额外硬件或实机条件 |
| N/A | 当前 CUAV V5 不适用，但多板替代时要跟踪 |

## 当前首批 120 项

| ID | 分类 | 状态 | 缺陷或不足 | 证据/位置 | 下一步 |
|----|------|------|------------|-----------|--------|
| RTT-GAP-001 | 主循环 | FIXED | RTT 1kHz tick delay 无法稳定支撑 400Hz 主循环 | `rtt_hrtimer_*_gate` | 保持 hrtimer 回归门禁 |
| RTT-GAP-002 | 主循环 | FIXED | DWT busy-wait 会饿死 IMU producer | 失败实验已归档 | 禁止恢复纯 busy-wait |
| RTT-GAP-003 | 主循环 | FIXED | loop-rate gate 默认启用 OpenOCD 前后双快照，常规/post-CAN 回归会生成 `openocd_average_loop_rate.avg_loop_hz`；需要时可用 `--no-openocd-before-sample` 关闭 | `rtt_gap003_default_baseline_20260621T205323Z` | 保持平均主循环频率作为硬件验收主证据 |
| RTT-GAP-004 | 主循环 | PARTIAL | scheduler task overrun 计数仍会累积；loop-rate gate 现已在默认 release 证据中把 `scheduler_task_max` / `scheduler_task_last` 映射为任务名、来源、频率、优先级、预算和超预算差值 | `rtt_gap004_task_identity_20260621T210011Z` | 下一轮用硬件 gate 捕获真实超预算任务并修复根因 |
| RTT-GAP-005 | 主循环 | FIXED | loop-rate gate 已把真实判定证据、单次周期样本、INS debug loop rate、SCHED_LOOP_RATE 参数显式拆开命名，steady wait 不再要求 `rtt_dbg_ins_loop_rate == target` | `rtt_gap005_loop_gate_fields_20260621T204807Z` | 保持平均主循环计数优先，INS debug 字段只作辅助 |
| RTT-GAP-006 | 主循环 | FIXED | 默认 release 构建已常驻导出 scheduler task 名称/优先级/频率/预算/来源元数据，loop-rate gate 将重型逐任务计数、线程 hook、电机分段计时归为 optional，不再把关闭的诊断符号当硬缺失 | `rtt_gap006_task_meta_20260621T203856Z` | 保持 release-safe task metadata；需要逐任务运行计数时再启用 `HAL_RTT_LOOP_DIAG` |
| RTT-GAP-007 | 主循环 | OPEN | hrtimer 只实现 STM32F7 后端 | `rtt_clock_time_stm32f7.c` | 抽象到 F7/H7/其他 MCU |
| RTT-GAP-008 | 主循环 | OPEN | TIM5 被固定占用，未做资源冲突审计 | `rtt_clock_time_stm32f7.c` | 与 hwdef timer 资源表联动 |
| RTT-GAP-009 | 主循环 | OPEN | hrtimer fallback 路径缺独立故障注入测试 | `Scheduler.cpp` | 增加 forced-fail gate |
| RTT-GAP-010 | 主循环 | OPEN | RT_USING_CLOCK_TIME 对其他 BSP 的影响未审计 | `.config` / `rtconfig.h` | 多 target 构建矩阵 |
| RTT-GAP-011 | USB | PARTIAL | Mission Planner Windows 连接仍需持续 A/B 回归 | Windows 文档 | 加 Windows QGC/MP 实测矩阵 |
| RTT-GAP-012 | USB | PARTIAL | USB CDC 参数下载已快，但缺长时间重复统计 | `param_download.json` 单轮 | 增加 N 轮 p50/p95 gate |
| RTT-GAP-013 | USB | PARTIAL | MAVFTP 单轮 GREEN，但背靠背极限历史有抖动 | open-issues 历史 | 重新跑背靠背 N 轮 |
| RTT-GAP-014 | USB | OPEN | CherryUSB DWC2 EPENA-stuck 历史根因未完全关闭 | open-issues | vendor 层状态机审计 |
| RTT-GAP-015 | USB | OPEN | CDC TX ring/FIFO 历史调参缺最终可读结论页 | open-issues | 归档为 USB TX 设计文档 |
| RTT-GAP-016 | USB | OPEN | 双 CDC Windows INF/COM 号稳定性需持续验证 | Windows acceptance | 增加 descriptor + MP open gate |
| RTT-GAP-017 | USB | OPEN | QGC GUI 实际连接路径未自动化验证 | 用户问题 | 采集 QGC 日志/配置 gate |
| RTT-GAP-018 | USB | OPEN | USB unplug/replug 物理恢复未形成常规 gate | open-issues | 软件 unbind/bind + 物理测试分开 |
| RTT-GAP-019 | USB | OPEN | USB host DTR/RTS 语义与 ChibiOS 未完整对齐 | USB docs | 对照 ChibiOS SerialUSBDriver |
| RTT-GAP-020 | USB | OPEN | CDC ACM line coding 对高波特假设缺测试 | descriptors/scripts | 增加 SET_LINE_CODING gate |
| RTT-GAP-021 | USB | OPEN | CDC TX 空间估计与 GCS 调度耦合仍有风险 | `UARTDriver.cpp` | 统计 txspace/pending 与 p95 |
| RTT-GAP-022 | USB | OPEN | MSC/composite 规划未收口 | open-issues | 明确 N/A 或路线 |
| RTT-GAP-023 | MAVLink | FIXED | 参数下载曾卡 Mission Planner | `rtt_release_param_download_*` | 保持 full-param gate |
| RTT-GAP-024 | MAVLink | PARTIAL | PARAM_REQUEST_READ count_parameters 历史开销需复查 | open-issues | 压测 READ spam |
| RTT-GAP-025 | MAVLink | PARTIAL | PARAM_SET 异步保存路径需持久化门禁覆盖 | param persist docs | 跑 `rtt_param_persist_gate.py` |
| RTT-GAP-026 | MAVLink | OPEN | GCS delay callback 与 ChibiOS 频率差异需审计 | open-issues | 对照 AP_Vehicle/ChibiOS |
| RTT-GAP-027 | MAVLink | OPEN | MAVLink stream rate under load 缺 p95 数据 | perf plan | 建 RTT/ChibiOS A/B runner |
| RTT-GAP-028 | MAVLink | OPEN | Mission protocol 不在最新验收摘要里 | scripts/tests | 加回 current acceptance |
| RTT-GAP-029 | MAVLink | OPEN | MAVROS 重复验证报告过期 | `.cursor/project/mavros-*` | 重跑 MAVROS smoke |
| RTT-GAP-030 | MAVLink | OPEN | STATUSTEXT 稳定采样窗口只有 35s | status gate | 扩到 10min soak |
| RTT-GAP-031 | MAVFTP | FIXED | MAVFTP 读 `@PARAM/param.pck` 已过 | `rtt_release_mavftp_*` | 保持 gate |
| RTT-GAP-032 | MAVFTP | OPEN | MAVFTP 大文件下载 p95 未统计 | log gate | 增加多日志大小矩阵 |
| RTT-GAP-033 | MAVFTP | OPEN | MAVFTP session reset/EOF 历史残留未系统化 | open-issues | 背靠背 N 轮 |
| RTT-GAP-034 | MAVFTP | OPEN | FTP 与日志下载并发缺测试 | scripts | 增加并发/互斥场景 |
| RTT-GAP-035 | MAVFTP | OPEN | FTP 后参数下载回归未固定门禁 | scripts | acceptance suite 串联 |
| RTT-GAP-036 | Storage | PARTIAL | D_storage 可落 RAM stub，不能证明持久化 | driver matrix | 禁止把 RAM stub 当 FRAM |
| RTT-GAP-037 | Storage | OPEN | FRAM 后端与 AP_Param 全链路当前未作为最终 gate | `Storage.cpp` / docs | 重跑 param persist |
| RTT-GAP-038 | Storage | OPEN | Flash fallback 与 ChibiOS 行为差异需审计 | `Flash.cpp` | 擦写/掉电风险分析 |
| RTT-GAP-039 | Storage | OPEN | `_save_backup()` microSD 备份路径未实现 | audit_storage.md | 设计 SD backup |
| RTT-GAP-040 | Storage | OPEN | RAM stub fallback 可能掩盖真实存储失败 | `Storage.cpp` | release 禁止 silent stub |
| RTT-GAP-041 | Storage | OPEN | Storage backend boot log 与 health flag 需统一 | param-storage-report | 强制 STATUSTEXT/diag |
| RTT-GAP-042 | SDCard | FIXED | 当前日志列取和下载非空日志已过 | `rtt_release_log_download_*` | 保持 gate |
| RTT-GAP-043 | SDCard | OPEN | SDCard 长稳 logging 未做小时级 soak | driver matrix | 1h disarmed logging |
| RTT-GAP-044 | SDCard | OPEN | SDMMC 寄存器层 `L6_sdmmc` 待实现 | driver matrix | 增加 L6 test |
| RTT-GAP-045 | SDCard | OPEN | SD 热插拔/无卡降级未验 | sdcard.cpp | no-card/insert gate |
| RTT-GAP-046 | SDCard | OPEN | FATFS/DFS fd 表历史风险需重新量化 | open-issues | fd pressure gate |
| RTT-GAP-047 | SPI | PARTIAL | SPI1 LLD 代码存在但生产路径激活状态复杂 | SPI_LLD_DESIGN/open-issues | 明确 CMSIS vs LLD 策略 |
| RTT-GAP-048 | SPI | OPEN | SPI LLD runtime gate 未纳入最新 acceptance | `hal_spi_lld.c` | 增加 init/xfer counter gate |
| RTT-GAP-049 | SPI | OPEN | SPI4 LLD 未覆盖 | status docs | 评估低频总线收益 |
| RTT-GAP-050 | SPI | OPEN | SPI2 FRAM 与 DMA/LLD 关系未最终定型 | spi2-fram-debug | 固化 FRAM 策略 |
| RTT-GAP-051 | SPI | OPEN | SPI DMA buffer 所在内存缺自动静态检查 | architecture docs | 增加 DTCM DMA lint |
| RTT-GAP-052 | SPI | OPEN | SPI bus mutex 与 ChibiOS bus lock 语义需对照 | `SPIDevice.cpp` | 并发 transfer test |
| RTT-GAP-053 | I2C | PARTIAL | IST8310 数据流正常，但 L5_i2c 待实现 | driver matrix | 增加寄存器层 I2C test |
| RTT-GAP-054 | I2C | OPEN | I2C clear_bus-on-timeout 策略需与 ChibiOS 对齐 | open-issues | timeout fault injection |
| RTT-GAP-055 | I2C | OPEN | I2C multi-device scan/恢复未验 | I2CDevice.cpp | bus recovery gate |
| RTT-GAP-056 | IMU | FIXED | `IMU0 0.0kHz` 告警在最新 gate 未出现 | `peripherals3/status_text` | 保持 banner check |
| RTT-GAP-057 | IMU | PARTIAL | gyro backend rate 字段历史未初始化风险需代码审计 | open-issues | 构造字段全初始化 audit |
| RTT-GAP-058 | IMU | OPEN | 多 IMU 实例只验证当前 active backend | peripherals gate | 逐实例 mask + data |
| RTT-GAP-059 | IMU | OPEN | fast sampling FIFO 实际 kHz 与 banner 缺数值 gate | INS symbols | 增加 backend rate readout |
| RTT-GAP-060 | IMU | OPEN | 振动/clip/温度漂移未长稳验 | MAVLink VIBRATION | 10min sensor soak |
| RTT-GAP-061 | Baro | FIXED | SCALED_PRESSURE 正常 | peripherals gate | 保持 gate |
| RTT-GAP-062 | Baro | OPEN | MS5611 PROM CRC 只在分层历史验证，最新整机未显式记录 | driver matrix | acceptance 引用 E_ms5611 |
| RTT-GAP-063 | Compass | FIXED | 磁力计 RAW_IMU mag 非零，S_compass 历史通过 | peripherals gate | 保持 gate |
| RTT-GAP-064 | Compass | HW | compass calibration 未做 | Mission Planner | 实机校准后验证 |
| RTT-GAP-065 | AHRS/EKF | PARTIAL | EKF_STATUS_REPORT 存在，但飞行级 EKF 健康未闭环 | peripherals gate | GPS/RC/calibrated scenario |
| RTT-GAP-066 | AHRS/EKF | OPEN | EKF double/single precision 与内存策略需 ChibiOS 对照 | hwdef/status | A/B memory + EKF perf |
| RTT-GAP-067 | IOMCU | PARTIAL | 稳定状态文本无 IOMCU unhealthy，但历史有 OpenOCD 干扰 | status_text | clean reboot IOMCU soak |
| RTT-GAP-068 | IOMCU | OPEN | IOMCU firmware upload 完整性 gate 未在最新摘要 | AP_IOMCU.cpp | 加 IOMCU version gate |
| RTT-GAP-069 | IOMCU | OPEN | UART/IOMCU 超时与 main loop 互扰需 p95 | loop metrics | 统计 read_registers p95 |
| RTT-GAP-070 | RCInput | HW | RCInput 无接收机验收 | driver matrix | 接 SBUS/PPM 或模拟源 |
| RTT-GAP-071 | RCInput | OPEN | `AP_RCPROTOCOL_ENABLED=0` 历史限制需清理 | driver matrix | 启用协议栈并测 |
| RTT-GAP-072 | RCInput | OPEN | SoftSigReader 路径未飞控级验证 | SoftSigReader* | RC pulse gate |
| RTT-GAP-073 | RCOutput | PARTIAL | IOMCU motor outputs healthy，但 FMU PWM 波形未验 | driver matrix | 示波器/无桨 PWM gate |
| RTT-GAP-074 | RCOutput | OPEN | DShot/bdshot 未实现或未验证 | rtt-chibios-file-matrix | 与 ChibiOS RCOutput 对齐 |
| RTT-GAP-075 | RCOutput | OPEN | TIM/DMA shared resource 与 SPI1 DMA 冲突待规划 | open-issues B1b | shared_dma plan |
| RTT-GAP-076 | CAN | FIXED | USB SLCAN -> SocketCAN -> pydronecan 已过 | socketcan gate | 保持 gate |
| RTT-GAP-077 | CAN | PARTIAL | 外接 1a86 调试器不响应 SLCAN ASCII | slcan_ascii gate | 确认调试器固件/模式 |
| RTT-GAP-078 | CAN | OPEN | CAN bus-off/error counter 长稳未验 | CanIface.cpp | bus-off injection |
| RTT-GAP-079 | CAN | OPEN | DroneCAN 动态节点/param/file 未全测 | socketcan gate | pydronecan 扩展用例 |
| RTT-GAP-080 | CAN | OPEN | CAN2/多 CAN iface 未验 | hwdef | 双 CAN gate |
| RTT-GAP-081 | Fault | PARTIAL | HardFault/Panic BKP 捕获历史存在，但最新验收未重跑 | rtt_dbg_bkp.* | fault injection gate |
| RTT-GAP-082 | Fault | OPEN | panic 语义与 ChibiOS crashdump 尚未完全对齐 | system.cpp | crash report format |
| RTT-GAP-083 | Fault | OPEN | stack overflow hook 未纳入 release gate | status/open-issues | RT hook + test |
| RTT-GAP-084 | Fault | OPEN | IWDG reset 原因上报需常规 gate | BKP/IWDG docs | watchdog injection |
| RTT-GAP-085 | Boot | PARTIAL | Bootloader/app offset 当前正确，但多板偏移需矩阵 | build logs | board matrix gate |
| RTT-GAP-086 | Boot | OPEN | SITL-on-hardware RTT 覆盖不足 | user request/scripts | 跑 `rtt_sitl_on_hw_gate.py` |
| RTT-GAP-087 | Build | FIXED | GitHub `test_rtt.yml` 在 apt SCons 4.0.1 下曾把 `--v=ArduCopter` 误解析为内建 `--version`；CI 现不传 `--v`、本地保留安全 `--vehicle` 长选项且兼容 legacy `--v`，full 与 l0_boot 本地和远端路径均通过 | `rtt_gap087_ci_scope_20260621T210411Z`, `rtt_gap087_scons_vehicle_ci_full2_20260621T212053Z`, `rtt_gap087_l0_boot_staged2_20260621T212433Z`, GitHub runs `27919122896`, `27920402333` | 保持 CI 使用 `--target/--test`，避免 SCons 4.0.1 option 歧义回归 |
| RTT-GAP-088 | Build | OPEN | Waf 与 SCons 源列表同步仍是风险 | `scons_ardupilot_sources.py` | source-list diff gate |
| RTT-GAP-089 | Build | FIXED | `HAL_STORAGE_SIZE` redefined warning 已清理，F7 不再从 SCons 命令行硬编码 16KB | `rtt_gap089_build_20260621T190556Z` | 保持 hwdef 为唯一板级真相源 |
| RTT-GAP-090 | Build | OPEN | ROM 93%+ 接近上限 | build log | size budget dashboard |
| RTT-GAP-091 | Build | FIXED | 源码树残留 `.o`/`.sconsign.dblite` 已搬入专用回收站，并增加审计脚本防复发 | `rtt_gap091_*` | 保持 `rtt_source_artifact_audit.py` gate |
| RTT-GAP-092 | Build | FIXED | 根目录重复 `SPIDevice.*.cmsis` 已删除 | 本次修改 | 保留 archive 一份 |
| RTT-GAP-093 | Build | OPEN | packages 下载/版本锁定未完全可复现 | hwdef/common/packages | lockfile/hash |
| RTT-GAP-094 | Build | OPEN | 多 target `pixhawk6c_mini` 未跟 CUAV 同步验收 | board matrix | multi-board build |
| RTT-GAP-095 | Docs | FIXED | 根 README 已改为中文分支说明 | README.md | 持续同步 |
| RTT-GAP-096 | Docs | FIXED | 当前验收状态文档已从 6/20 更新到 6/21/22 | 本次修改 | 避免双事实源 |
| RTT-GAP-097 | Docs | OPEN | `.cursor/project/status.md` 仍含旧 boot_stub 等过期事实 | status.md | 拆迁到 archive/history |
| RTT-GAP-098 | Docs | OPEN | open-issues 太长且混合已修/未修 | open-issues.md | 分成 active/archived |
| RTT-GAP-099 | Docs | OPEN | driver matrix 与最新整机验收未自动同步 | matrix + README | 生成摘要脚本 |
| RTT-GAP-100 | Docs | OPEN | ChibiOS A/B 性能计划仍是 framework only | perf plan | 采集同板 ChibiOS 数据 |
| RTT-GAP-101 | Test | OPEN | 缺“至少 N 轮全链路 acceptance suite”固定入口 | rtt_acceptance_suite.py | 串联 loop/param/ftp/can/log |
| RTT-GAP-102 | Test | OPEN | 缺 QGC/MP 主机侧日志采集工具 | user QGC request | QGC log parser |
| RTT-GAP-103 | Test | FIXED | 已新增 Linux 主机侧 USB 端口占用冲突自动诊断，枚举 ttyACM/ttyUSB/by-id/pyserial/sysfs，标注 MAVLink CDC/SLCAN CDC/外接 USB 串口，采集 lsof/fuser owner、ModemManager/brltty/QGC/MissionPlanner/slcand/openocd 干扰进程，并输出 JSON verdict | `Tools/scripts/rtt_usb_port_conflict_diag.py`, `test_rtt_usb_port_conflict_diag.py`, `rtt_gap103_usb_conflict_diag_20260622T000000Z/usb_port_conflict_diag.json` | 在 QGC/MP 连接失败前后运行该脚本，RED 表示端口被占或 OpenOCD 残留 |
| RTT-GAP-104 | Test | OPEN | 缺 OpenOCD 与 CDC 互斥守卫统一库 | scripts | common fixture guard |
| RTT-GAP-105 | Test | OPEN | 缺 hardware capability manifest | docs/scripts | board capabilities JSON |
| RTT-GAP-106 | Perf | OPEN | RTT/ChibiOS 同板参数下载 p95 未测 | perf plan | paired A/B |
| RTT-GAP-107 | Perf | OPEN | RTT/ChibiOS CPU idle under load 未测 | perf plan | UART7 ap_rate |
| RTT-GAP-108 | Perf | OPEN | RAW_IMU/ATTITUDE stream p95 未测 | perf plan | rate runner |
| RTT-GAP-109 | Perf | OPEN | logging throughput 与 ChibiOS 未对齐 | log gate | large log write/read |
| RTT-GAP-110 | Perf | OPEN | SD write latency 对 main loop 抖动影响未量化 | log metrics | logging stress |
| RTT-GAP-111 | Memory | OPEN | DTCM/SRAM/DMA 分区缺自动验证 | architecture | linker map lint |
| RTT-GAP-112 | Memory | OPEN | SRAM1 heap fragmentation 未长稳统计 | Util/RT heap | heap soak |
| RTT-GAP-113 | Memory | OPEN | thread stack high-water 未常规输出 | RT-Thread | thread dump gate |
| RTT-GAP-114 | Memory | OPEN | EKF/USB/logging 共享 heap 风险未隔离 | status | memory pool plan |
| RTT-GAP-115 | Multi-board | OPEN | Pixhawk6C Mini RTT 未达到 CUAV 当前验收等级 | hwdef/pixhawk6c | H7 build/flash gate |
| RTT-GAP-116 | Multi-board | OPEN | FMUv2 legacy 支持状态不清 | open-issues | mark deprecated or port |
| RTT-GAP-117 | Multi-board | OPEN | WSPI H7 路径仅 N/A/build-only | WSPIDevice.h | H7 board validation |
| RTT-GAP-118 | Release | OPEN | 版本/board name/USB product 与 ChibiOS 兼容策略需最终定稿 | USB docs | Windows driver matrix |
| RTT-GAP-119 | Release | OPEN | 无“可替代 ChibiOS”总验收证书模板 | docs | release checklist |
| RTT-GAP-120 | Release | OPEN | 仍缺飞行前完整校准、RC、安全开关、无桨电机输出整体验收 | bench boundary | staged flight-readiness plan |
| RTT-GAP-121 | Build | FIXED | RTT POSIX 路径改由 RT-Thread `dirent.h` 提供 `DT_DIR/DT_REG/DT_LNK`，不再由 `AP_Filesystem.h` 重复定义 | `rtt_gap121_build_20260621T191704Z` | 保持 POSIX `dirent` 为真相源 |
| RTT-GAP-122 | Build | FIXED | `GCS_Common.cpp` 中误写为 `#pragma GCSS diagnostic pop` 的拼写错误已改回 `#pragma GCC diagnostic pop` | `rtt_gap122_build_20260621T193900Z` | 保持 failure-creation diagnostic push/pop 配对 |
| RTT-GAP-123 | SDCard | FIXED | `sdcard.cpp` direct SDIO fallback 已清理未接入 helper，并修正 ACMD6/RCA/CMD13/card-mode/SDSC CMD16 初始化路径 | `rtt_gap123_build_20260621T195302Z` | 继续做 no-card/热插拔/长稳日志 gate |
| RTT-GAP-124 | Build | FIXED | OSD 后端派生类显式恢复 `AP_OSD_Backend::write` 重载集可见性，避免窄 `write(text)` override 隐藏格式化写接口 | `rtt_gap124_131_build_20260621T201158Z` | 保持 OSD 格式化写构建 gate |
| RTT-GAP-125 | Filesystem | FIXED | `posix_compat.h` 先 `#undef clearerr/ferror/feof` 再重定向到 APFS，且 `feof` 修正为 `apfs_feof` | `rtt_gap125_build_20260621T192155Z` | 保持 Lua stdio EOF/error 语义 |
| RTT-GAP-126 | Fault | FIXED | `rtt_dbg_bkp.c` 解包故障线程名改为显式 buffer 长度，不再对形参使用 `sizeof(out)` | `rtt_gap126_build_20260621T192558Z` | 保持 BKP fault gate |
| RTT-GAP-127 | USB | FIXED | `hal_usb_cherryusb_shim.c` 中未使用且会硬停 IN endpoint 的 `cherry_tx_stop_locked` 已删除，避免误接回 Windows/Mission Planner DTR 关闭热路径 | `rtt_gap127_build_20260621T193347Z` | 保持 DTR close 软恢复策略 |
| RTT-GAP-128 | Storage | FIXED | RT-Thread MMCSD busy/status 轮询已初始化 `status`，并修正 eMMC busy 错误处理不再把 `err` 当 R1 位解析 | `rtt_gap128_build2_20260621T200623Z` | 后续做 SD/MMC busy fault injection |
| RTT-GAP-129 | Build | FIXED | 板级 `board.h`/`board.c` 已提供 legacy `SystemClock_Config()` ABI 并转调 `rtt_clock_init()`，RT-Thread STM32 弱初始化路径不再缺原型 | `rtt_gap129_build_20260621T200033Z` | 保持当前强 `rt_hw_board_init()` 直接使用 LL clock path |
| RTT-GAP-130 | Build | FIXED | `AP_Math.cpp` 的 clang-only diagnostic pragma 已用 `#if defined(__clang__)` 保护，GCC RTT 构建不再解析 clang pragma | `rtt_gap130_build_20260621T194317Z` | 保持 `is_equal()` 浮点逻辑不变 |
| RTT-GAP-131 | Build | FIXED | `BufferPrinter` 与 `AP_HAL_Empty::GPIO` 显式恢复基类 `read/write/pinMode` 重载集可见性，消除隐藏重载风险 | `rtt_gap124_131_build_20260621T201158Z` | 保持 HAL interface warning gate |
| RTT-GAP-132 | Scripting | FIXED | RTT SCons 生成的 ArduPilot define 集合已补齐 `ARDUPILOT_BUILD=1`，Lua runtime 重新进入 ArduPilot 嵌入式沙箱裁剪路径，`loadlib/lbaselib/lstrlib/liolib` unused static warning 消失 | `rtt_gap132_fix_build_20260621T202516Z` | 保持 RTT SCons 与 waf 的核心构建宏一致 |
| RTT-GAP-133 | ADC | FIXED | `hal_adc_lld_rtt.c` 改为先包含 STM32F7 CMSIS 头并使用其 `ADC_SR_EOC` 定义，本地只补缺失兼容宏，消除 ADC LLD 与 `stm32f767xx.h` 宏重定义 | `rtt_gap133_build_20260621T203110Z` | 保持 CMSIS 为寄存器位真相源 |
| RTT-GAP-134 | Build | FIXED | RTT module test 解析曾直接使用 `libraries/AP_HAL_RTT/test/` 源码树，导致 `l0_boot` 构建把 `.o` 留在源码目录；现优先使用 deployed BSP `tests/`，并在 build 目录建立 `_common` 兼容入口 | `rtt_gap087_l0_boot_staged2_20260621T212433Z`, `rtt_gap087_l0_boot_clean_20260621T212016Z/source_artifact_recycle.json` | 保持 `rtt_source_artifact_audit.py` 在 module test 后为 0 |
| RTT-GAP-135 | CI | FIXED | `issue/rtt-*` push 曾触发 copter/plane/rover/chibios/macos/cygwin/dds 等普通 ArduPilot 大矩阵，产生与 RTT 无关的失败并稀释专用验收；21 个普通 push workflow 已加入 `branches-ignore: issue/rtt-*`，后续 RTT push 只触发 RTT 专用 workflow | GitHub run `27917986917..27917986962`, run `27918833283`, workflow YAML audit | 保持 RTT 分支 CI 分流，PR/workflow_dispatch 仍可按需触发普通 workflow |
| RTT-GAP-136 | CI | FIXED | 远端 `test_rtt.yml` 已越过旧 SCons `--v` 歧义，但 CI 容器曾缺 `arm-none-eabi-gcc`，同时测试构建在 BSP SCons 未产出 bin/elf 时不会立刻失败；workflow 已安装 ARM GCC/binutils，根 `SConstruct` 增加 `rtthread.bin`/`rt-thread.elf` 必需产物 gate，远端 RTT matrix 两个 job 已通过 | `rtt_gap136_ci_toolchain_full_20260621T214226Z`, `rtt_gap136_ci_toolchain_l0_20260621T214335Z`, GitHub run `27919122896` | 保持工具链安装与产物 gate，后续新增 target 也必须走同一检查 |
| RTT-GAP-137 | Build | FIXED | 同一工作树内并行运行多个相同 RTT target 的根 SCons 曾共享 `build/rtt_deploy/cuav_v5/_hwdef_gen` 和 deploy 目录，dry-run 在 `hwdef.h.tmp -> hwdef.h` rename 上互相踩踏；根 SCons 现对同一 target 持有 `build/rtt_deploy/<target>.lock` 覆盖 deploy、mavgen/dronecangen、BSP SCons、产物检查和 APJ 包装，standalone deploy helper 也使用同名锁且识别父进程已持有锁 | `rtt_gap137_target_lock_20260622T000000Z/full_dry_run.log`, `l0_dry_run.log`, `standalone_deploy.*`, `source_artifact_audit.json`, GitHub run `27920402333`, `rtt_gap137_github_run_27920402333/*.log` | 保持同 target 根 SCons invocation 串行，避免共享 deploy 目录竞态 |
| RTT-GAP-138 | Build | FIXED | clean CI full build 生成的 RTT `ap_config.h` 曾强制 `#undef HAL_NUM_CAN_IFACES` 并设为 0，覆盖 CUAV V5 hwdef/命令行的 CAN=2，导致 `AP_CANManager.cpp` 找不到 `HAL_CANIface` 且 DroneCAN 把 iface 数组视作 0 长度；模板已改为只包含 `hwdef.h` 并清理旧错误覆盖段，远端 full copter 已越过 CAN 编译/链接 | GitHub runs `27918448978`, `27918833283`, `rtt_gap138_can_apconfig_rebuild_20260621T215615Z` | 保持 `ap_config.h` 不覆盖 hwdef CAN 数量，继续由 `RTT-GAP-139` 追 CI GDB 依赖 |
| RTT-GAP-139 | CI | FIXED | clean CI full build 已越过 CAN 编译/链接并生成 `rt-thread.elf`/`rtthread.bin`，但 Reset_Handler 二进制完整性检查 fallback 需要 GDB，容器只装 GCC/binutils，没有 `arm-none-eabi-gdb`，导致 `rtt_verify_bin.py` 报 `No such file or directory: arm-none-eabi-gdb` 后失败；workflow 已安装 `gdb-multiarch`，脚本在找不到 `arm-none-eabi-gdb` 时自动 fallback 到 `gdb-multiarch`，远端 full copter binary integrity check 已通过 | GitHub runs `27918833283`, `27919122896`, `rtt_gap139_github_run_27919122896/cuav-v5-copter.log` | 保持 GDB fallback，避免发行版包名差异再次打断完整性检查 |
| RTT-GAP-140 | CI | FIXED | GitHub container 内 `set_app_descriptor.py` 曾在 SCons 构建期间无法取得 git hash，最初为 dubious ownership，随后收敛为 RT-Thread SCons post action 环境 `$HOME not set`，导致 APP_DESCRIPTOR hash 为 `0x00000000`；descriptor 脚本现为 git 创建临时 HOME，在该私有 HOME 中写入 `safe.directory=<source_root>` 后读取短 hash，单元测试覆盖该路径，远端 L0/full 均写入真实非零 hash | GitHub runs `27919122896`, `27919445368`, `27919693436`, `27919941352`, `rtt_gap140_github_run_27919941352/*.log` | 保持 APP_DESCRIPTOR 输出真实 `git_hash`，禁止回退到 HOME/dubious/hash=0 降级路径 |

## 本轮已处理

- `RTT-GAP-092`：删除 `libraries/AP_HAL_RTT/` 根目录重复 `SPIDevice.cpp.cmsis` / `SPIDevice.h.cmsis`。这两个文件与 `archive/spi-cmsis/` 内副本完全一致，且未被构建引用；保留 archive 一份作为旧实验资料。
- `RTT-GAP-096`：更新 `docs/rtt-porting/CUAV_V5_RTT_ACCEPTANCE_CURRENT.md`，把状态从 2026-06-20 旧证据同步到 2026-06-21/22 hrtimer、参数、MAVFTP、外设、日志、SocketCAN/pydronecan、最终构建证据。
- `RTT-GAP-089`：移除 F7 SCons defines 中的 `HAL_STORAGE_SIZE=16384`，让 CUAV V5 的 `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` 通过生成的 `hwdef.h` 提供 `HAL_STORAGE_SIZE=32768`。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap089_build_20260621T190556Z/build.log` 无 `HAL_STORAGE_SIZE redefined`，当前生成配置不再注入 16KB。
- `RTT-GAP-091`：新增 `Tools/scripts/rtt_source_artifact_audit.py`，先审计再把 276 个未跟踪源码树产物搬入 `archive/recycle/rtt_source_artifacts_20260621T191147Z/`。验证：搬迁后 audit 为 0；再次 `python3 -m SCons --target=cuav-v5 -j16` 通过；构建后源码树 artifact audit 仍为 0。
- 新发现 `RTT-GAP-121` 到 `RTT-GAP-124`：本轮构建日志继续暴露 RT-Thread `dirent.h` 宏重定义、未知 pragma、SDCard 未使用代码、OSD overloaded virtual warning，均已登记，后续逐项修复。
- `RTT-GAP-121`：将 `AP_Filesystem.h` 中 RTT 的简化 `DT_REG/DT_DIR/DT_LNK` 定义限制到 `!AP_FILESYSTEM_POSIX_ENABLED`，RTT POSIX 构建改用 RT-Thread `<dirent.h>` 的 d_type 常量。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap121_build_20260621T191704Z/build.log` 中 `DT_* redefined` 检索为空，源码树 artifact audit 仍为 0。
- 新发现 `RTT-GAP-125`：修复 `DT_*` 后，构建日志继续暴露 `posix_compat.h` 的 `clearerr/ferror/feof` 重定义 warning，已登记为下一轮 filesystem 兼容清理项。
- `RTT-GAP-125`：在 `posix_compat.h` 中先 `#undef clearerr/ferror/feof` 再映射到 APFS，消除 newlib `stdio.h` 宏重定义 warning；同时修正 `feof(stream)` 原来错误指向 `apfs_ferror(stream)` 的行为，改为 `apfs_feof(stream)`。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap125_build_20260621T192155Z/build.log` 中 `clearerr/ferror/feof redefined` 检索为空，源码树 artifact audit 仍为 0。
- 新发现 `RTT-GAP-126` 到 `RTT-GAP-127`：本轮构建继续暴露 `rtt_dbg_bkp.c` 数组形参 `sizeof` 误用和 CherryUSB 未使用 stop helper，已登记为故障诊断/USB 后续清理项。
- `RTT-GAP-126`：`unpack_thread_name()` 改为接收显式 `out_len`，调用处传入 `sizeof(rtt_last_fault_thr)`，用 `snprintf()` 写入默认 `?`，避免数组形参退化为指针后的错误 `sizeof(out)`。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap126_build_20260621T192558Z/build.log` 中 `sizeof-array-argument` / `sizeof-pointer-memaccess` 检索为空，源码树 artifact audit 仍为 0。
- `RTT-GAP-127`：删除 CherryUSB 中未使用的 `cherry_tx_stop_locked()` 和只服务于该死路径的 DTR-closed endpoint 调试计数，并同步 `Tools/scripts/rtt_usb_debug_snapshot.py` 的符号列表。该 helper 会在 DTR 下降时执行 endpoint disable/FIFO flush，与当前为 Windows usbser/Mission Planner 保留 configured/open hint 的策略冲突，因此不接回运行路径。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过后，`build.log` 中 `cherry_tx_stop_locked` / `unused-function` 检索应为空，源码树 artifact audit 仍为 0。
- 新发现 `RTT-GAP-128` 到 `RTT-GAP-132`：`rtt_gap127_build_20260621T193347Z/build.log` 继续暴露 RT-Thread MMCSD 未初始化风险、STM32 BSP clock hook 隐式声明、GCC 下 clang pragma 噪声、HAL overloaded virtual warning、Lua 裁剪后 unused static warning，已登记为后续构建质量和脚本裁剪收敛项。
- `RTT-GAP-122`：修正 `GCS_Common.cpp` failure-creation 分支里 `#pragma GCSS diagnostic pop` 拼写错误，恢复为 `#pragma GCC diagnostic pop`，与同块 `#pragma GCC diagnostic push` 正确配对。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过后，`build.log` 中 `GCSS diagnostic` / `unknown-pragmas` 的 GCS 条目应为空。
- `RTT-GAP-130`：将 `AP_Math.cpp` 中只给 clang 使用的 `#pragma clang diagnostic push/ignored/pop` 包在 `#if defined(__clang__)` 内，避免 arm-none-eabi-g++ 把 clang pragma 当作 unknown pragma。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过后，`build.log` 中 `AP_Math.cpp` / `unknown-pragmas` 检索应为空。
- `RTT-GAP-123`：收敛 `sdcard.cpp` direct SDIO fallback 初始化路径：删除未接入的 PIO/CMD6/CMD17/SCR helper，修正 ACMD6 常量为 SET_BUS_WIDTH，CMD3 只保留 RCA 高 16 位，CMD7 后接 CMD13 状态/ready 检查，card mode 不再无条件覆盖为 high-capacity，并让 SDSC CMD16 失败时退出重试。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap123_build_20260621T195302Z/build.log` 中 `sdcard.cpp:.*warning` 与旧 helper/变量 warning 检索为空，源码树 artifact audit 仍为 0。
- `RTT-GAP-129`：在 RTT CUAV V5 板级 `board.h` 声明 `SystemClock_Config()`，并在 `board.c` 提供兼容 wrapper 转调现有 `rtt_clock_init()`。当前实际运行仍由强 `rt_hw_board_init()` 直接调用 LL clock path；wrapper 用于补齐 RT-Thread STM32 HAL_Drivers 弱初始化路径的 legacy ABI，避免 `drv_common.c` 隐式声明和未来测试固件链接风险。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap129_build_20260621T200033Z/build.log` 中 `SystemClock_Config` / `drv_common.c:.*implicit declaration` 检索为空，源码树 artifact audit 仍为 0。
- `RTT-GAP-128`：修复 RT-Thread MMCSD busy/status 轮询风险。`dev_mmc.c:mmc_poll_for_busy()` 将 `status` 初始化为 0，并把错误判断从 `R1_STATUS(err)` 改为直接检查 `err`，避免把负错误码当 R1 响应位解析；同类地，`dev_block.c` 的 CMD13 helper 改为零初始化 `struct rt_mmcsd_cmd`，busy 状态初值也置 0。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap128_build2_20260621T200623Z/build.log` 中 `dev_mmcsd_core.h:141` / `status' may be used uninitialized` / `dev_mmc.c:.*warning` 检索为空，源码树 artifact audit 仍为 0。
- `RTT-GAP-124` / `RTT-GAP-131`：修复 overloaded virtual 隐藏重载风险。`BufferPrinter` 使用 `using AP_HAL::BetterStream::read/write` 恢复 `BetterStream` 的完整重载集，`AP_HAL_Empty::GPIO` 使用 `using AP_HAL::GPIO::pinMode` 恢复三参数 alt pinMode，OSD MAX7456/MSP/MSP DisplayPort/SITL 后端使用 `using AP_OSD_Backend::write` 恢复格式化写接口。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap124_131_build_20260621T201158Z/build.log` 中 `warning:` / `overloaded-virtual` / `was hidden` / `AP_OSD_Backend.h:38` / `BetterStream` / `GPIO.h:54` 目标检索为空，源码树 artifact audit 仍为 0。
- `RTT-GAP-132`：修复 RTT SCons 与 waf 构建宏不一致。`Tools/scripts/scons_ardupilot_sources.py` 现在为 F7/H7 RTT ArduPilot 对象统一加入 `ARDUPILOT_BUILD=1`，让 Lua `ARDUPILOT_BUILD` 条件编译重新生效；这不只是消除 warning，也让 `loadlib/luaconf/linit/loslib` 等 Lua runtime 回到 ArduPilot 嵌入式沙箱行为。验证：先强制删除 35 个当前 CUAV V5 RTT Lua 对象并构建，`results/execution/rtt_gap132_rebuild_20260621T201914Z/build.log` 复现 `createclibstable/luaB_xpcall/str_dump/io_tmpfile` 等 unused warning；修复后再次强制删除 Lua 对象并构建，`results/execution/rtt_gap132_fix_build_20260621T202516Z/build.log` 中上述 Lua warning/`unused-function` 检索为空，生成的 `scons_ardupilot_config.py` 含 `ARDUPILOT_BUILD=1`，源码树 artifact audit 仍为 0。
- 新发现 `RTT-GAP-133`：修复 `RTT-GAP-132` 后，完整构建日志仍有 `hal_adc_lld_rtt.c` 手写 `ADC_SR_EOC` 与 STM32F7 CMSIS `stm32f767xx.h` 重定义 warning，已登记为下一轮 ADC LLD/CMSIS 对齐项。
- `RTT-GAP-133`：修复 ADC LLD 与 CMSIS 寄存器宏边界。`hal_adc_lld_rtt.c` 现在先包含 `<stm32f7xx.h>` 和本地头，再只为 CMSIS 缺失的 `ADC_SR_ADRDY/ADC_CR2_ADCAL` 提供兼容定义；标准 `ADC_SR_EOC` 改用 STM32F7 CMSIS 头里的定义，ADC 初始化/转换流程不变。验证：强制删除 `hal_adc_lld_rtt.o` 后执行 `python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap133_build_20260621T203110Z/build.log` 中 `warning:` / `ADC_SR_EOC` / `redefined` 检索为空，二进制完整性检查通过，源码树 artifact audit 仍为 0。
- `RTT-GAP-103`：新增 `Tools/scripts/rtt_usb_port_conflict_diag.py` 作为 Linux 主机侧 USB 端口占用冲突诊断。脚本只读系统状态，不 reset USB、不杀进程；它综合 `/dev/ttyACM*`、`/dev/ttyUSB*`、`/dev/serial/by-id`、pyserial、sysfs USB VID/PID、`lsof`、`fuser` 和 `pgrep`，标注 RTT MAVLink CDC、RTT SLCAN CDC、外接 USB 串口/CAN 调试器、STLink 等角色，并检测 QGroundControl、Mission Planner/Wine、slcand、ModemManager、brltty、openocd 是否占用或干扰端口。当前实机诊断 `rtt_gap103_usb_conflict_diag_20260622T000000Z/usb_port_conflict_diag.json` 给出 `GREEN`：`/dev/ttyACM1` 为 `rtt_mavlink_cdc`、`/dev/ttyACM2` 为 `rtt_slcan_cdc`，两者均未被占用，`/dev/ttyACM0` 为外接 `1a86_USB_Single_Serial` 调试器；单元测试、py_compile 均通过。
- `RTT-GAP-087`：修复 GitHub RTT workflow 与旧 SCons 的命令行歧义。远端日志 `rtt_gap087_ci_scope_20260621T210411Z/github_logs/cuav-v5-*.log` 显示 `python3 -m SCons --v=ArduCopter ...` 在 SCons 4.0.1 下失败为 `SCons Error: --version option does not take a value`；`.github/workflows/test_rtt.yml` 已改为直接使用默认 ArduCopter 车辆并只传 `--target/--test`，根 `SConstruct` 新增正式 `--vehicle` 选项且保留本地 legacy `--v`。验证：`timeout 120 python3 -m SCons --target=cuav-v5 -n` 与 `--test=l0_boot -n` 均返回 0；`results/execution/rtt_gap087_scons_vehicle_ci_full2_20260621T212053Z/build.log` 完整构建通过、warning/error 检索为空、二进制完整性检查通过，源码树 artifact audit 为 0；`results/execution/rtt_gap087_l0_boot_staged2_20260621T212433Z/build.log` L0 boot 构建通过且 warning/error 检索为空。后续远端 runs `27919122896` 与 `27920402333` 的 RTT full/L0 matrix 均 GREEN，证明 CI 不再触发 SCons option 歧义，因此主表标记 FIXED。
- `RTT-GAP-134`：修复 module test 构建污染源码树。复现证据：`rtt_gap087_l0_boot_clean_20260621T212016Z/post_build_artifact_audit.json` 发现 `libraries/AP_HAL_RTT/test/_common/test_app_descriptor.o`、`test_stubs.o`、`bringup/l0_boot/main.o` 三个未跟踪产物；已通过 `source_artifact_recycle.json` 搬入 `archive/recycle/rtt_source_artifacts_20260621T212320Z/`。`rtt_test_manifest.resolve_test_paths()` 现在优先返回 `build/rtt_deploy/cuav_v5/tests/test_l0_boot`，公共 `SConscript` 在 build 目录创建 `_common -> tests/common` 兼容入口；重跑 `rtt_gap087_l0_boot_staged2_20260621T212433Z` 后对象落在 `build/rtt_deploy/cuav_v5/_common` 和 `tests/test_l0_boot`，源码树 artifact audit 为 0。
- `RTT-GAP-135`：新增 RTT 分支 CI 分流。`641b0f1b2e` 的 push 同时触发了 `test copter/plane/rover/sub/blimp/chibios/ap_periph/dds/macos/cygwin/...` 等普通大矩阵，其中多项 failure 但不验证 RTT SCons 固件本身；现对 21 个非 RTT push workflow 添加 `branches-ignore: issue/rtt-*`，保留 `test_rtt.yml`、PR 路径和 `workflow_dispatch`。验证：所有 workflow YAML 可由 PyYAML 解析，文本断言确认 21 个普通 push workflow 均带 `branches-ignore`，missing=[]；后续 GitHub run `27918833283` 只触发 `test rtt_fmuv2`，没有再触发普通大矩阵，因此主表标记 FIXED。
- `RTT-GAP-136`：修复 GitHub RTT workflow 缺 ARM 工具链与测试构建失败信号滞后的问题。远端 run `27917986937` 日志显示旧 `--v` 歧义已消失，新的失败为 `the toolchain path (/opt/gcc-arm-none-eabi-10-2020-q4-major/bin/) is not exist`；`.github/workflows/test_rtt.yml` 现在安装 `gcc-arm-none-eabi`/`binutils-arm-none-eabi` 并打印 `arm-none-eabi-gcc --version`，让根 `SConstruct` 的 `RTT_EXEC_PATH` 自动指向 CI PATH 中的工具链。根 `SConstruct` 新增 `_require_rtt_artifacts()`，在 BSP SCons 返回 0 后要求 `rtthread.bin` 与 `rt-thread.elf` 非空存在，避免 L0/test 构建把缺产物问题推迟到 workflow 后续 `test -s` 步骤才暴露。验证：workflow YAML 解析通过，`git diff --check` 通过，`timeout 120 python3 -m SCons --target=cuav-v5 -n` 通过；`results/execution/rtt_gap136_ci_toolchain_full_20260621T214226Z/build.log` 完整构建通过、ROM 93.79%、Reset_Handler 二进制完整性检查通过、warning/error/undefined/toolchain 检索为空、源码树 artifact audit 为 0；`results/execution/rtt_gap136_ci_toolchain_l0_20260621T214335Z/build.log` L0 boot 构建通过、测试 bin/elf 产物 gate 通过、warning/error/toolchain 检索为空、源码树 artifact audit 为 0；远端 run `27919122896` 的 L0/full matrix 均 GREEN，因此主表标记 FIXED。
- `RTT-GAP-137`：修复同一 RTT target 并发根 SCons invocation 的 deploy 目录竞态。旧问题是 full dry-run 与 `--test=l0_boot` dry-run 同时使用 `build/rtt_deploy/cuav_v5/_hwdef_gen`，其中一个进程在 `hwdef.h.tmp -> hwdef.h` rename 时遇到 `FileNotFoundError`。现在根 `SConstruct` 对同一 target 持有 `build/rtt_deploy/<target>.lock`，锁覆盖 deploy、mavgen/dronecangen、BSP SCons、产物检查、bin copy 和 APJ 包装；`Tools/scripts/rtt_bsp_deploy.py` standalone 调用也使用同名锁，并通过 `RTT_TARGET_LOCK_HELD=<target>` 避免被父进程自锁。验证：并发执行 `python3 -m SCons --target=cuav-v5 -n` 与 `python3 -m SCons --target=cuav-v5 --test=l0_boot -n`，两个进程均显示 `RTT target lock` 且返回 0，日志无 `FileNotFoundError` / `hwdef.h.tmp` / `SCons Error`；standalone deploy stdout 仍只输出 deploy 路径，锁日志进入 stderr；`git diff --check`、RTT script unittest、source artifact audit 均通过。远端 run `27920402333` 两个 matrix job 均 GREEN，L0/full 日志均显示 `RTT target lock`，APP_DESCRIPTOR 写入 `hash=0x077eb9f9`，full 日志 `Binary integrity check PASSED`，未出现旧 `_hwdef_gen` rename、HOME、descriptor hash=0 或 CAN 编译错误。
- `RTT-GAP-138`：修复 clean CI full build 的 CAN 宏被 `ap_config.h` 覆盖问题。远端 run `27918448978` 证明 `gcc-arm-none-eabi` 安装有效且 L0 job 已 GREEN，但 full copter job 在 `AP_CANManager.cpp:158/326` 报 `expected type-specifier before 'HAL_CANIface'`，同时 `AP_DroneCAN.cpp` 报 `CanardInterface::ifaces` 被视为 `AP_HAL::CANIface* [0]`；根因是 RTT full 构建首次生成的 `build/rtt_cuav_v5/ap_config.h` 模板在 include `hwdef.h` 后又 `#undef HAL_NUM_CAN_IFACES` 并强制设 0，覆盖 CUAV V5 的 `HAL_NUM_CAN_IFACES=2`。现在 `libraries/AP_HAL_RTT/hwdef/common/SConscript` 生成的 `ap_config.h` 只包含 `hwdef.h`，并会清理旧模板残留的 CAN=0 覆盖段。验证：删除 `build/rtt_cuav_v5/ap_config.h` 与 CAN/DroneCAN 对象后执行 `python3 -m SCons --target=cuav-v5 -j16`，`results/execution/rtt_gap138_can_apconfig_rebuild_20260621T215615Z/build.log` 完整构建通过，`AP_CANManager/AP_DroneCAN` 相关对象重编通过，ROM 93.79%，Reset_Handler 二进制完整性检查通过，`warning/error/HAL_CANIface/array subscript` 检索为空；新生成 `ap_config.h` 不再含 `undef HAL_NUM_CAN_IFACES` 或 `define HAL_NUM_CAN_IFACES 0`；远端 run `27918833283` full copter 已成功完成 C/C++ 编译、归档、链接、objcopy/size/descriptor 写入，日志中不再出现 `HAL_CANIface` 或 `ifaces[0]`，新失败转为 `RTT-GAP-139` 的 GDB 依赖，因此主表标记 FIXED。
- `RTT-GAP-139`：修复 CI binary integrity fallback 缺 GDB 的问题。推送 `RTT-GAP-138` 后，GitHub run `27918833283` 证明 L0 boot job 全绿，full copter job 已成功编译、归档、链接并生成 `rt-thread.elf`/`rtthread.bin`，旧的 `HAL_CANIface`/`ifaces[0]` CAN 编译错误消失；新的失败点变为二进制完整性检查 fallback：脚本找不到 Reset_Handler literal pool pattern 后尝试 GDB ELF comparison，但 CI 容器只有 `gcc-arm-none-eabi` 与 `binutils-arm-none-eabi`，没有 `arm-none-eabi-gdb`。现在 `.github/workflows/test_rtt.yml` 安装 `gdb-multiarch` 并打印版本，`Tools/scripts/rtt_verify_bin.py` 在找不到 `arm-none-eabi-gdb` 时自动 fallback 到 `gdb-multiarch`，避免绑定到某个发行版是否提供 `gdb-arm-none-eabi` 包。验证：本地 `python3 -m py_compile Tools/scripts/rtt_verify_bin.py`、workflow YAML 解析、fallback monkeypatch 单测、`python3 Tools/scripts/rtt_verify_bin.py build/rtt_cuav_v5/rtthread.bin build/rtt_deploy/cuav_v5/rt-thread.elf` 均通过；远端 run `27919122896` 安装 `gdb-multiarch`，full copter 日志出现 `Binary integrity check PASSED`，两个 matrix job 均 GREEN，因此主表标记 FIXED。
- `RTT-GAP-140`：修复 CI APP_DESCRIPTOR git hash 降级路径。远端 run `27919122896` 的 L0/full 日志出现 `set_app_descriptor.py` 报 `git rev-parse failed: fatal: detected dubious ownership`，APP_DESCRIPTOR 虽然写入成功但 hash 为 `0x00000000`；即使 workflow build step 已执行 `git config --global --add safe.directory "$GITHUB_WORKSPACE"`，SCons/RTT post action 内部仍可能因 HOME/global config 时序不同而看不到该设置。第一次修复使用 `GIT_CONFIG_COUNT/GIT_CONFIG_KEY_0/GIT_CONFIG_VALUE_0` 对单个 git 子进程注入 safe.directory，远端 run `27919445368` 证明仍未消除 dubious ownership；第二次改为 descriptor 脚本在当前 HOME 写入 global safe.directory，远端 run `27919693436` 把根因收敛为 `$HOME not set`。最终 `git_short_hash()` 自己创建临时 HOME，在该私有 HOME 中写入 `safe.directory=<source_root>` 后执行 `git -C <source_root> rev-parse --short HEAD`，并由 `Tools/scripts/build_tests/test_rtt_set_app_descriptor.py` 覆盖。远端 run `27919941352` 两个 matrix job 均 GREEN，L0 日志 `git_hash=0x03712b32`、`Applied APP_DESCRIPTOR ... hash=0x03712b32`，full 日志同样写入 `hash=0x03712b32` 且 `Binary integrity check PASSED`；日志未再出现 dubious ownership、HOME not set、git rev-parse failed 或 hash=0。
- `RTT-GAP-006`：修复 release/default 构建缺少 scheduler task 元数据的问题。`AP_Scheduler.cpp` 新增默认开启的 `HAL_RTT_LOOP_TASK_META`，常驻导出 128 个任务槽的 `priority/rate_hz/allowed_us/source/name[0..5]`，只在首次看到任务时填充，避免把 `HAL_RTT_LOOP_DIAG` 的逐循环计数热路径带入默认固件；`rtt_loop_rate_gate.py` 拆分 `RTT_REQUIRED_SYMBOLS` 与 `RTT_OPTIONAL_SYMBOLS`，把旧的 `rtt_dbg_run_tasks_us/rtt_dbg_extra_loop`、逐任务运行计数、线程切换 hook、电机分段计时等诊断符号归为 optional，同时保留 snapshot 输出 `optional_missing_symbols`。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap006_task_meta_20260621T203856Z/build.log` 中 `warning:` / `error:` / `undefined reference` / `redefined` 检索为空，`Reset_Handler` 二进制完整性检查通过；`PYTHONPATH=Tools/scripts` 导入 gate 后对 `build/rtt_deploy/cuav_v5/rt-thread.elf` 检查得到 `required_missing=[]`、`metadata_present_count=10`、`optional_missing_count=50`；源码树 artifact audit 仍为 0。
- `RTT-GAP-005`：修复 loop-rate 诊断输出语义混淆。`rtt_loop_rate_gate.py` 现在优先用 `rtt_dbg_main_loop_iterations` 两次 OpenOCD snapshot 计算出的 `avg_loop_hz` 作为真实主循环验收证据，并新增 `rate_evidence_source`；旧的 `debug_loop_us/debug_loop_hz` 改名为 `last_loop_period_sample_us/last_loop_period_sample_hz`，`ins_loop_rate` 改名为 `ins_debug_loop_rate_hz`，`target_loop_rate_hz` 旁明确输出 `sched_loop_rate_param_hz`。`wait_for_openocd_steady()` 只要求 main loop counter 增长，不再把 `rtt_dbg_ins_loop_rate == target` 当稳态条件；当平均主循环频率达标时，diagnosis 固定为 `AVERAGE_LOOP_RATE_OK`，低的单次周期样本只保留为 symptom。README 与当前验收文档同步使用新字段名。离线验证：`results/execution/rtt_gap005_loop_gate_fields_20260621T204807Z/classify_offline.json` 构造 `avg_loop_hz=399.2`、`rtt_dbg_loop_time_us=10000`、`rtt_dbg_ins_loop_rate=0` 的 payload，`classify()` 给出 `verdict=GREEN`、`diagnosis=AVERAGE_LOOP_RATE_OK`、`rate_evidence_source=openocd_average_loop_rate`，证明 INS debug 字段和单次周期样本不再覆盖平均主循环证据；`python3 -m py_compile Tools/scripts/rtt_loop_rate_gate.py` 通过。
- `RTT-GAP-003`：修复 post-CAN/常规 loop-rate gate 默认缺少平均主循环基线的问题。`rtt_loop_rate_gate.py` 的 `--openocd-before-sample` 改为 `argparse.BooleanOptionalAction` 且默认开启，脚本默认先等待 main loop counter 增长并抓取 baseline OpenOCD snapshot，再进行 MAVLink 采样和结束 snapshot，从而自动生成 `openocd_average_loop_rate.avg_loop_hz`、task/thread/scalar deltas；特殊场景仍可用 `--no-openocd-before-sample` 关闭。验证：`python3 -m py_compile Tools/scripts/rtt_loop_rate_gate.py` 通过，`python3 Tools/scripts/rtt_loop_rate_gate.py --help` 显示 `--openocd-before-sample | --no-openocd-before-sample`，SCons 构建通过后源码树 artifact audit 仍为 0。
- 新增本台账，首批记录 120 项差距，后续每轮按 ID 修复、验证、关闭。
