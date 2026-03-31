# Decision Log

## 2026-03-30: `RAW_IMU` 使用 AHRS 主 IMU 索引，而非硬编码实例 0
- 原因：多 IMU 飞机上主传感器可能不是 `instance 0`，固定发 0 会导致 GCS/测试看到加速度长期为 0，而 `ATTITUDE`/EKF 仍正常
- 决定：`send_raw_imu()` 使用 `AP::ahrs().get_primary_accel_index()`，越界则回退 `0`
- 放弃方案：为通过测试在脚本侧改判 `SCALED_IMU2`，掩盖与 MAVLink 语义不一致

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

## 2026-03-28: SPI LLD (Low-Level DMA) 驱动替代 HAL busy-wait
- 原因：STM32 HAL 的 SPI DMA ISR 中存在 busy-wait（`while (FTLVL != 0)` + `while (BSY)`），在高频 SPI 传输时导致 ISR 长时间占用 CPU
- 决定：为 STM32F7 SPI1 实现 LL DMA 驱动 (`drv_spi_lld.c/h`)，在 DMA RX 完成中断中仅清标志 + `rt_completion_done`，BSY 等待推迟到线程上下文
- 集成方式：在 `drv_spi.c` 中检查 `spi_bus_obj.lld` 指针，有则走 LLD 路径，否则保留 HAL 路径
- 放弃方案：(1) 修改 HAL 源码 — 影响面大，升级困难；(2) 全部异步化 — 改动量过大

## 2026-03-28: DTCM 堆修复 — HEAP_BEGIN 从 DTCM 移到 SRAM1
- 原因：STM32F767 的 DTCM (0x20000000-0x2001FFFF, 128KB) 仅 CPU 可访问，DMA 无法读写。rt_malloc 分配的 DMA buffer 落在 DTCM 导致 DMA 传输静默失败或 HardFault
- 决定：`board.h` 中 `HEAP_BEGIN` 从 `&__bss_end`（DTCM, 0x2001BA04）改为 `0x20020000`（SRAM1 起始），浪费 ~18KB DTCM 尾部但确保所有堆分配 DMA 可访问
- 放弃方案：(1) 双堆（DTCM 堆 + SRAM 堆）— RT-Thread 默认 memheap 不支持按属性选择；(2) 每次 DMA 分配手动指定地址 — 侵入性太强

## 2026-03-28: newlib polyfill 策略（asprintf/vasprintf/memmem）
- 原因：ARM newlib bare-metal 缺少 GNU 扩展函数，ArduPilot GPS/Filesystem 代码使用了 asprintf/memmem
- 决定：在 BSP `board/rtt_libc_compat.c` 中提供 C 实现，在 `hwdef.h` 中添加 `extern "C"` 声明（通过 ap_config.h force include 传播到所有 ArduPilot 源文件）
- 放弃方案：(1) 禁用使用这些函数的驱动 — 会丢失 GPS 支持；(2) 切换到 picolibc — 侵入性太大

## 2026-03-28: 干净 clone 编译支持 — .gitmodules + 自动 packages
- 原因：之前需要手动指定 rt-thread fork URL、手动下载 packages、手动运行 waf configure，新环境无法一步编译
- 决定：(1) `.gitmodules` rt-thread URL 改为 pogo fork HTTPS；(2) `rtt_bsp_deploy.py` copytree 后自动调用 `pkgs_update_manual.sh`；(3) `SConscript` 自动创建 `ap_config.h` 并复制 `hwdef.h`
- 效果：`git clone --recursive -b staging/pogo-rtt && cd pogo-apm && python3 -m SCons --target=cuav-v5 -j16` 一步完成
- 放弃方案：维持手动步骤 — 换台电脑无法编译

## 2026-03-28: boot_stub 替代陈旧 bootloader
- 原因：0x08000000 处残留的旧固件向量表将 Reset_Handler 指向新固件的 HAL_Init 中间位置，导致启动时跳到错误代码
- 决定：自制 36 字节最小 boot_stub（向量表+跳转），烧录到 sector 0；app 保持在 0x08008000
- 放弃方案：直接把 app 链接到 0x08000000（失去 bootloader OTA 能力）

## 2026-03-28: MPU SRAM 区域 Non-Shareable (S=0)
- 原因：STM32F7 AXI 总线全局独占监视器对 SRAM 的 ldrex/strex 返回 PRECISERR
- 决定：MPU Region 0 设 S=0，强制使用本地独占监视器
- 依据：Cortex-M7 本地监视器对单核系统完全足够；ChibiOS 在 STM32F7 上也使用 Non-Shareable SRAM
- 放弃方案：禁用 MPU（可行但损失 D-Cache 性能和 DMA 保护）

## 2026-03-28: HEAP_BEGIN = max(_ebss, SRAM1_START) 替代硬编码
- 原因：BSS 段 136KB 超过 DTCM 128KB 容量，溢出到 SRAM1；硬编码 HEAP_BEGIN=0x20020000 与 BSS 尾部重叠
- 决定：HEAP_BEGIN 取 _ebss 和 SRAM1_START 的较大值
- 放弃方案：把 BSS 限制在 DTCM 内（需减少全局变量，不现实）

## 2026-03-28: HAL_Init() 替换为直接寄存器操作
- 原因：HAL_Init 内部调用 HAL_InitTick 在 HSI 16MHz 下配置 SysTick，后续 SystemClock_Config 切换到 216MHz 但不重新配置 SysTick，导致 millis() 快 13.5x
- 决定：移除 HAL_Init()，替换为 FLASH ART + NVIC priority grouping 直接寄存器操作；在 SystemClock_Config 后显式调用 rt_hw_systick_init()
- 放弃方案：保留 HAL_Init 然后再调 rt_hw_systick_init（冗余且留 HAL 依赖）

## 2026-03-30: RTT POSIX `stat()` 采用 C wrapper，而不是继续赌 C++ 头文件对齐
- 原因：尽管 `AP_Filesystem.h` 试图让 RTT 的 C++ 编译单元包含 RT-Thread `sys/stat.h`，但实测 `sizeof(struct stat)` 仍为 60，而 DFS/ELM-FAT 的 C 侧 `struct stat` 为 88。`log_io` 在线程里调用 `AP::FS().stat()` 时，会沿 `AP_Filesystem_Posix::stat() -> ::stat() -> f_stat()/get_fileinfo()` 把 88B 结果写进 C++ 的 60B 栈对象，最终表现为 `UNALIGNED/FORCED HardFault`、坏异常帧以及延时崩溃。
- 决定：新增 RTT 专用 C 编译单元 `ap_rtt_posix_stat()`，由它使用 RT-Thread 原生 `struct stat` 调 `stat()`，再在 `AP_Filesystem_Posix::stat()` 中把通用字段拷回 C++ 侧 `struct stat`。这样所有 RTT 本地文件 `stat()` 调用点共享同一条安全路径，不再依赖脆弱的头文件 ABI 对齐技巧。
- 放弃方案：继续依赖 C++ 侧 include/pragma 强行覆盖 `struct stat`；或者只在 `AP_Logger_File`/`GCS_FTP` 等单个调用点各自绕开 `stat()`

## 2026-03-30: RTT Copter 的低频问题先补“运行时默认流率”，不强改持久化 `SR0_*`
- 原因：首轮基线显示 CPU 空闲仍约 99%、主循环周期约 2.3ms，但默认 `ATTITUDE/RAW_IMU/SYS_STATUS` 只有约 `0.07/0.33/0.33 Hz`；而显式发送 `MAV_CMD_SET_MESSAGE_INTERVAL` 后三者立刻升到约 `10Hz`，说明瓶颈在默认消息流率/请求链而非底层链路。仅修改 `GCS_MAVLink_Parameters.cpp` 的编译默认值不足以覆盖板上已保存的旧 `SR0_* = 0` 参数。
- 决定：保留 RTT/Copter 的非 0 编译默认流率给新参数集使用，同时在 `GCS_Common.cpp::initialise_message_intervals_from_streamrates()` 中增加 RTT/Copter 运行时 fallback：若启动时检测到 `streamRates[]` 整组仍为 0，则只在内存中填入保守默认值（`RAW_SENS=4`, `EXT_STAT=2`, `RC_CHAN=2`, `POSITION=2`, `EXTRA1=10`, `EXTRA2=4`, `EXTRA3=2`），再初始化 message intervals，不写回持久化参数。
- 放弃方案：直接 `set_and_save()` 强制改写用户现有的 `SR0_*`；或者继续假设所有地面站都会主动发送 `REQUEST_DATA_STREAM/SET_MESSAGE_INTERVAL`
