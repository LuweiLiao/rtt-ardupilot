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
| RTT-GAP-003 | 主循环 | PARTIAL | post-CAN loop gate 缺平均 loop rate 基线 | `rtt_post_can_loop_rate_*` | gate 默认启用 baseline snapshot |
| RTT-GAP-004 | 主循环 | OPEN | scheduler task overrun 计数仍会累积 | `rtt_loop_rate_gate.py` metrics | 逐 task 定位真实超预算项 |
| RTT-GAP-005 | 主循环 | OPEN | debug loop rate 与真实 SCHED_LOOP_RATE 容易混淆 | README/loop gate | 输出字段重命名或解释强化 |
| RTT-GAP-006 | 主循环 | OPEN | scheduler task 数组符号在 release 缺失 | `missing_symbols` | release-safe task metadata |
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
| RTT-GAP-087 | Build | PARTIAL | SCons 构建通过，但 CI test scripts 曾失败 | GitHub workflow | 专用 workflow 策略 |
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
| RTT-GAP-103 | Test | OPEN | 缺 USB 端口占用冲突自动诊断 | scripts | lsof/fuser report |
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
| RTT-GAP-121 | Build | OPEN | RT-Thread `dirent.h` 与 `AP_Filesystem.h` 重复定义 `DT_DIR/DT_REG/DT_LNK` | `rtt_gap091_build_20260621T191220Z` | 统一 include/宏保护策略 |
| RTT-GAP-122 | Build | OPEN | `GCS_Common.cpp` 出现未知 pragma `GCSS diagnostic` warning | `rtt_gap091_build_20260621T191220Z` | 查是否拼写或条件编译问题 |
| RTT-GAP-123 | SDCard | OPEN | `sdcard.cpp` 存在未使用变量和未使用 static helper warning | `rtt_gap091_build_20260621T191220Z` | 清理死代码或接入缺失检测路径 |
| RTT-GAP-124 | Build | OPEN | `AP_OSD_Backend::write()` overloaded virtual warning 在 RTT 构建中仍出现 | `rtt_gap091_build_20260621T191220Z` | 判断上游共性或 RTT include 差异 |

## 本轮已处理

- `RTT-GAP-092`：删除 `libraries/AP_HAL_RTT/` 根目录重复 `SPIDevice.cpp.cmsis` / `SPIDevice.h.cmsis`。这两个文件与 `archive/spi-cmsis/` 内副本完全一致，且未被构建引用；保留 archive 一份作为旧实验资料。
- `RTT-GAP-096`：更新 `docs/rtt-porting/CUAV_V5_RTT_ACCEPTANCE_CURRENT.md`，把状态从 2026-06-20 旧证据同步到 2026-06-21/22 hrtimer、参数、MAVFTP、外设、日志、SocketCAN/pydronecan、最终构建证据。
- `RTT-GAP-089`：移除 F7 SCons defines 中的 `HAL_STORAGE_SIZE=16384`，让 CUAV V5 的 `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` 通过生成的 `hwdef.h` 提供 `HAL_STORAGE_SIZE=32768`。验证：`python3 -m SCons --target=cuav-v5 -j16` 通过，`results/execution/rtt_gap089_build_20260621T190556Z/build.log` 无 `HAL_STORAGE_SIZE redefined`，当前生成配置不再注入 16KB。
- `RTT-GAP-091`：新增 `Tools/scripts/rtt_source_artifact_audit.py`，先审计再把 276 个未跟踪源码树产物搬入 `archive/recycle/rtt_source_artifacts_20260621T191147Z/`。验证：搬迁后 audit 为 0；再次 `python3 -m SCons --target=cuav-v5 -j16` 通过；构建后源码树 artifact audit 仍为 0。
- 新发现 `RTT-GAP-121` 到 `RTT-GAP-124`：本轮构建日志继续暴露 RT-Thread `dirent.h` 宏重定义、未知 pragma、SDCard 未使用代码、OSD overloaded virtual warning，均已登记，后续逐项修复。
- 新增本台账，首批记录 120 项差距，后续每轮按 ID 修复、验证、关闭。
