# Decision Log

## 2026-03-23: 将项目记忆拆成多文件，而非继续堆到单一 trace
- 原因：长对话和单一 `agent-trace` 容易造成上下文膨胀，也容易把"中途现象"误当成"稳定事实"
- 决定：把当前状态、未关闭问题、设计决策、命令速查、里程碑计划分别拆到 `project/` 下
- 放弃方案：继续依赖长聊天导出或单文件 trace 做全部恢复

## 2026-03-23: Windows 主机验证优先于 WSL2 usbipd 结果
- 原因：WSL2 `usbipd` 下的 USB CDC 双向通信不稳定，容易把环境问题误判为固件问题
- 决定：USB CDC / MAVLink 的正确性以 Windows 主机侧验证为主，WSL2 结果只作辅助观察
- 放弃方案：把 WSL2 `/dev/ttyACM*` 作为唯一地面真相源

## 2026-03-23: 当前基线保持 `HAL_WITH_RAMTRON 0`
- 原因：FRAM 后端尚未形成可靠初始化与持久化闭环，过早开启会让 `Storage` 路径不稳定
- 决定：在当前稳定基线中继续把 RAMTRON 关闭，待后端成熟后再升级
- 放弃方案：在未验证后端完整性的情况下直接把 FRAM 当成默认持久化

## 2026-03-23: 当前基线允许 `HAL_WITH_EKF_DOUBLE 0`
- 原因：当前 RAM 裕量下，双精度 EKF 容易导致 `EKF3 not enough memory`
- 决定：先把"系统稳定运行 + 传感器/MAVLink 可用"作为更高优先级，允许单精度 EKF 作为当前基线
- 放弃方案：在尚未解决内存问题时强行坚持双精度 EKF

## 2026-03-23: 结构目标对齐 ChibiOS，但不复制板级硬编码
- 原因：未来目标是支持更多国产芯片，而不是仅把 `CUAV v5` 特解写得更深
- 决定：把 `hwdef.dat` 作为板级真相源，推动 `rtt_hwdef.py`、设备表、probe 列表、SPI attach 等走数据驱动路径
- 放弃方案：继续在 HAL 层和 `SPIDeviceManager` 中累积板级字符串和硬编码映射

## 2026-03-23: bootstrap 以 `project/*.md` 为唯一主入口
- 原因：若 `project/*.md` 与专项档案并列为两个入口，新 agent 会在新会话和对齐任务中出现"先读哪套状态"的歧义
- 决定：统一采用 `current-focus -> status -> open-issues` 作为主 bootstrap；`.cursor/alignment-status.md` 与 `.cursor/alignment-issues.md` 仅作为 `AP_HAL_RTT` 对齐任务的专项补充档案
- 放弃方案：继续让 `rtt-chibios-alignment` 维持独立于主治理系统之外的第二套入口

## 2026-03-23: Git 与 driver-validation 采用单一事实源
- 原因：同一主题若同时在 project 文档、Skill、命令目录中展开完整说明，容易产生漂移和重复阅读
- 决定：Git 流程以 `project/git-process.md` 为长文权威，`rtt-git-milestone` 只保留入口与检查清单；driver-validation 以 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 为正式方法论，矩阵管状态，Skill 管入口
- 放弃方案：让多个文件分别维护同一主题的完整长文版本

## 2026-03-23: delay_microseconds 混合策略（DWT busy-wait + rt_thread_delay）
- 原因：RT-Thread 同优先级线程间 busy-wait 会阻塞 round-robin 调度
- 决定：`delay_microseconds()` 采用混合策略：`< 100us` 使用 DWT CYCCNT busy-wait（精确、时间短不会影响调度）；`>= 100us` 使用 `rt_thread_delay()` 让出 CPU
- 放弃方案：(1) 全部走 rt_thread_delay — tick 量化导致短延迟不精确；(2) 全部走 busy-wait — 长延迟会饿死同优先级线程
- 前提：主线程与 DeviceBus 线程优先级已错开（boost=MAX/4, normal=MAX/3, timer=MAX/8）
- 实测验证：主循环 ~400Hz，CPU 负载 6.5%

## 2026-03-24: 主循环对齐 ChibiOS check_called_boost 条件延迟
- 原因：RTT 主循环在 `loop()` 后无条件执行 `delay_microseconds(50)` DWT 忙等；ChibiOS 在 `wait_for_sample()` 调用 `delay_microseconds_boost()` 后通过 `check_called_boost()` 跳过该延迟
- 决定：在主循环中加入 `if (!schedulerInstance.check_called_boost())` 条件判断，与 ChibiOS 完全对齐
- 效果：CPU 负载从 95-100% 降至 6.5%（GDB 60s soak 实测）
- 放弃方案：保留无条件 50us 延迟 — 会导致地面站 CPU 爆红

## 2026-03-24: 信号量语义对齐 ChibiOS（take(0) = 非阻塞）
- 原因：RTT 的 `Semaphore::take(0)` 和 `BinarySemaphore::wait(0)` 实现为 `RT_WAITING_FOREVER`（永久阻塞），但 ArduPilot 约定 `timeout=0` 表示非阻塞尝试
- 决定：修改 `take(0)` / `wait(0)` 为 `rt_mutex_take(_mtx, 0)` / `rt_sem_take(_sem, 0)`（非阻塞）；新增 `take_blocking()` / `wait_blocking()` 使用 `RT_WAITING_FOREVER`
- 效果：校准流程不再死锁，AccelCal 和 Level Cal 均正常完成
- 放弃方案：保持原语义 — 会导致校准和任何使用 `wait_nonblocking()` 的代码死锁

## 2026-03-24: SD 卡支持策略（SDMMC1 + ELM-FAT + POSIX Backend）
- 原因：CUAV V5 有 MicroSD 卡槽，ChibiOS 版本通过 FatFS 提供文件系统日志
- 决定：启用 RT-Thread SDIO 驱动 + ELM-FAT 文件系统 + DFS，通过 `INIT_ENV_EXPORT` 在启动时自动挂载 `/sd`；ArduPilot 侧使用 `AP_FILESYSTEM_POSIX_ENABLED=1` 走 POSIX 后端
- 关键实现：(1) `stm32f7xx_hal_msp.c` 新增 SDMMC1 GPIO/DMA MSP，(2) `rt_board_init.c` 新增 `sd_card_mount()` 含 PG7 电源使能，(3) F7 使用 `drv_sdio.c` 而非 H7 的 `drv_sdmmc.c`
- 放弃方案：(1) 仅用 MAVLink Logging — 丢失离线日志能力；(2) 直接操作 SDMMC 寄存器 — 工作量大且不可移植

## 2026-03-24: UART7 作为 RT-Thread 控制台（替代 UART3）
- 原因：用户添加了 UART7 调试串口硬件连接（PE8=TX, PF6=RX），希望用于 rt_kprintf + msh shell
- 决定：将 `RT_CONSOLE_DEVICE_NAME` 从 `"uart3"` 改为 `"uart7"`；在 F7 `uart_config.h` 补充 `UART7_CONFIG`（原文件只到 UART6）；MSP 新增 UART7 GPIO 初始化（AF8）
- 效果：COM33（CH343 USB-TTL）115200 可直接读到 `msh />` 和 `rt_kprintf` 输出
- 放弃方案：(1) 保持 UART3 作为控制台 — 用户已接好 UART7 硬件；(2) UART7 作为额外 ArduPilot SERIAL 端口 — 用户明确要控制台

## 2026-03-24: Logging 策略升级为 Filesystem + MAVLink 双后端
- 原因：SD 卡支持实现后，可同时使用文件系统日志和 MAVLink 远程日志
- 决定：`HAL_LOGGING_FILESYSTEM_ENABLED=1` + `HAL_LOGGING_MAVLINK_ENABLED=1`；日志目录 `/sd/APM/LOGS`
- 效果：PreArm: Logging failed 消除（文件系统后端提供默认日志能力）
- 放弃方案：仅 MAVLink Logging — 需地面站主动发起会话才能消除 PreArm
