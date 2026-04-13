# AP_HAL_RTT 当前状态

> 基线：`CUAV v5` / `STM32F767` / `ArduCopter V4.7.0-dev on RT-Thread 5.3.0`
> 最后更新：2026-04-08（SPI DMA SPE bit 修复 + 栈溢出修复 + 主循环慢调查）

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
- **MAVFTP 综合回归通过**：`tests/test_mavftp.py` 在 Ubuntu 物理机 `/dev/ttyACM1` 上 **6/6 PASS**（根目录列举、`@PARAM/param.pck`、真实文件 Create/Write/OpenRO/Read/Delete、ResetSessions、稳定性）
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
- **分层模块测试体系**（`tests/`）：7 个独立测试固件（L0 boot, L1 gpio, L2 uart/spi/flash, L3 integration, L5 imu），通过 `scons --target=cuav_v5 --test=<name>` 构建，烧录到 0x08000000，GDB 可读全局结果变量
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

- ~~**SD 卡 SDMMC1 已验证通过**~~：rtt_sd_mount_stage=10, rtt_sd_mount_result=0（成功挂载）
- **RGB LED（PH10/11/12）**：**已修复** 2026-04-04。三个问题：(1) `HAL_GPIO_LED_ON=1` 应为 `0`（active-low open-drain，ChibiOS 默认=0）；(2) OTYPER 未设 open-drain（ChibiOS hwdef 用 OPENDRAIN）；(3) rt_pin_write 不可靠 → 改用 BSRR 直写。修复后 ODR 在 0xFFFF↔0xF3FF 间切换，黄色闪烁（pre-arm failing）已确认。LED 是 GPIO 驱动，不是 IS31FL3195 I2C。
- **RCInput SBUS 验证**：SBUS 串口协议路径已实现但未实机验证
- **Servo 输出验证**：PWM TIM1(50Hz)/TIM4(100Hz) 已运行，CCR=0（未解锁状态正常），需通过 GCS 命令实际驱动电调/舵机验证
- **IOMCU ✅ 已实机验证**（2026-04-12）：ROMFS pipeline 完成（`rtt_hwdef.py` → `embed.py` → `ap_romfs_embedded.h`），`io_firmware.bin` 成功嵌入固件。烧录后 MAVLink 验证：SYS_STATUS `MOTOR_OUTPUTS` present + healthy，RC_CHANNELS 19 条消息（chancount=0 = 无RC接收器正常）。`HAL_WITH_IO_MCU=1` 已启用。详见 open-issues.md

## 调试方法论

- **ChibiOS 对比法**：当遇到无法仅通过软件调试解决的硬件问题时，刷 ChibiOS CUAV V5 固件做对比测试。ChibiOS 是 ArduPilot 在 STM32 上的参考实现，若 ChibiOS 下硬件同样不工作则可排除固件问题
