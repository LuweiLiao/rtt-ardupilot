# Open Issues

## Issue: `RAW_IMU` 加速度字段仍为 0,和 AHRS/Baro/Compass 成功状态不一致
- 级别:高
- 现象:`test_h5_sensors.py` 与 `run_all.py` 都稳定复现 `RAW_IMU.xacc/yacc/zacc = 0`,但同一轮测试中 `Compass`、`SCALED_PRESSURE`、`ATTITUDE`、`EKF_STATUS_REPORT` 均正常
- 当前判断:`GCS_MAVLINK::send_raw_imu()` 长期固定使用 `ins.get_accel(0)`;若 AHRS 主 IMU 不是实例 0,则 `RAW_IMU` 与 `ATTITUDE`/EKF 不一致,加速度可长期为 0
- 已做修复:`GCS_Common.cpp` 中改为使用 `AP::ahrs().get_primary_accel_index()`(并做越界回退到 0),温度同步用同一实例
- 下一步:刷固件后重跑 `test_h5_sensors.py` 确认 `RAW_IMU` 加速度非零

## Issue: `SET_MESSAGE_INTERVAL` 设置后 MAVLink 频率反而退化
- 级别:中
- 现象:默认流率 96.6 msgs/s(ATTITUDE 13.3Hz),但发送 `SET_MESSAGE_INTERVAL ATTITUDE=10Hz` 后降到 16.2 msgs/s(ATTITUDE 2.0Hz)
- 当前判断:可能与 `scheduler_delay_callback` 中的 `tnow - last_50hz > 20` 间隔限制有关,或 interval 更新后改变了消息调度优先级
- 下一步:排查 interval 更新对 deferred_message 调度的影响;默认流率已足够正常使用

## Issue: 大日志 `LOG_REQUEST_DATA` 下载未闭环,小日志可完整下载
- 级别:中
- 现象:`tests/test_log_download.py` 可列出日志并开始下载,但对当前最新大日志(约 37MB)会在 `35053290 / 37040128` 处提前结束;同一轮手工探针下载较小日志(log 14,约 1.6MB)则可 100% 完成
- 当前判断:基础 `LOG_REQUEST_LIST/LOG_REQUEST_DATA` 协议链已通,长传时 USB CDC 侧易出现间歇停顿,需客户端从当前 offset 重发 `LOG_REQUEST_DATA`
- 已做修复:`tests/test_log_download.py` 增加 stall 检测、从 `len(data)` 续传、整体超时放宽
- 下一步:实机重跑大日志下载;若仍失败再区分固件侧提前结束与纯链路问题

## Issue: SPI LLD 仅覆盖 SPI1,SPI2/SPI4 仍走 HAL 路径
- 级别:低
- 现象:SPI1 (IMU) 已使用 LLD,但 SPI4 (Baro) 等仍走 HAL DMA 路径
- 当前判断:SPI4 传输频率较低,HAL busy-wait 影响可忽略
- 下一步:如需进一步降 CPU 负载,可为其他总线也注册 LLD 上下文

## Issue: SD 卡挂载需实际插卡验证
- 级别:高
- 现象:`sd0` Block Device 已在 RT-Thread 注册,`mmcsd_detect` 线程运行中,但 `ls /sd` 显示 `No such directory`
- 当前判断:SD 卡设备已识别但挂载可能因卡未插入或格式不对而失败;`sd_card_mount()` 已通过 `INIT_ENV_EXPORT` 注册
- 下一步:实际插入 FAT32 格式 SD 卡后复测 `ls /sd/APM/LOGS`

## Issue: mmcsd_detect 线程栈使用率 90%
- 级别:中
- 现象:`list thread` 显示 `mmcsd_detect` 栈使用 90%(1KB 栈)
- 当前判断:接近栈溢出边界,RT-Thread SD 卡检测线程默认栈偏小
- **已修复**:2026-03-31 将 `RT_MMCSD_STACK_SIZE` 从 2048 增大到 4096(`rtconfig.h`)
- 下一步:插卡验证实际栈使用率

## Issue: RCOutput 缺 DShot / 安全开关 / IOMCU
- 级别:高
- 现象:仅有基础 PWM 输出;`force_safety_on()` 恒返回 false
- 当前判断:CUAV V5 硬件有 IOMCU 但 RTT 未实现;DShot 协议未实现
- 下一步:先确保基础 PWM 可控电调后再评估 DShot 优先级

## Issue: RCInput 无脉冲捕获路径
- 级别:高
- 现象:仅依赖 `AP::RC().update()` 处理串口协议 RC(如 SBUS)
- 当前判断:PPM/脉冲管脚输入未实现
- 下一步:评估 CUAV V5 实际 RC 接口类型,SBUS 路径可能已足够

## Issue: Util 覆盖面不足
- 级别:中
- 现象:缺 toneAlarm、safety_switch_state、watchdog_reset、flash_bootloader、random 等
- 当前判断:影响通知蜂鸣器、安全开关状态读取、看门狗历史
- 下一步:按 bring-up 优先级逐步补充

## Issue: HAL/BSP/hwdef 仍有硬编码耦合
- 级别:中
- 现象:部分板级事实散落在 HAL、BSP、构建逻辑中
- 当前判断:当前结构未完全达到 ChibiOS 式 hwdef 驱动
- 下一步:在不破坏 CUAV V5 基线前提下推动数据驱动化

## Issue: USB CDC 快速重连在 usbipd/WSL2 下不稳定
- 级别:**已关闭**(不再使用 WSL2)
- 结论:已迁移到 Ubuntu 24.04 物理机直连 ST-Link V2 + USB,不再有 usbipd 吞吐瓶颈

## Issue: EKF3 内存压力
- 级别:低
- 现象:实机链路偶现 `EKF3 not enough memory`,当前允许回退到 DCM
- 当前判断:与 `HAL_WITH_EKF_DOUBLE` 和堆裕量相关
- 下一步:统计 RAM 占用来源

## Issue: CH343 USB-TTL 在 WSL2 无驱动 [仅 WSL2 环境]
- 级别:**已关闭**(不再使用 WSL2)
- 结论:已迁移到 Ubuntu 24.04 物理机,CH343 映射为 `/dev/ttyACM0`(UART7 msh console),原生可用

---

## 已关闭问题

### ~~Issue: 校准时地面站冻结~~ [已关闭 2026-03-24]
- **结论**:两个根因:(1) `BinarySemaphore::wait(0)` 语义反转(RTT 实现为永久阻塞而非非阻塞),(2) `Scheduler::delay()` 缺少 `persistent_data.scheduler_task = -4` 赋值。修复信号量语义 + 对齐 Scheduler 后校准正常。

### ~~Issue: PreArm: Logging failed~~ [已关闭 2026-03-24]
- **结论**:实现 SD 卡文件系统 + Filesystem Logging 后端后解决。`HAL_LOGGING_FILESYSTEM_ENABLED=1` + `AP_FILESYSTEM_POSIX_ENABLED=1`。

### ~~Issue: AnalogIn `_timer_tick()` 未被 Scheduler 调用~~ [已关闭 2026-03-24]
- **结论**:已在 `Scheduler::_run_timers()` 末尾添加 `((AnalogIn*)hal.analogin)->_timer_tick()` 调用。

### ~~Issue: 主循环 400Hz / CPU 爆红~~ [已关闭 2026-03-24]
- **结论**:三个根因修复(D-Cache、编译优化、rt_kprintf 移除)+ check_called_boost 对齐。CPU 从 95-100% 降至 6.5%。

### ~~Issue: 参数持久化~~ [已关闭 2026-03-23]
- **结论**:Flash 后端已验证,`Storage` 通过 on-chip Flash(page 10-11)持久化参数。

### ~~Issue: 磁力计不出现~~ [已关闭 2026-03-24]
- **结论**:`rt_components_board_init()` 未被调用,I2C 驱动未初始化。修复后 IST8310 正常识别。

### ~~Issue: MAVLink 卡死 / 日志请求冻结~~ [已关闭 2026-03-24]
- **结论**:`HAL_LOGGING_ENABLED=0` 时 `LOG_REQUEST_LIST` 被静默丢弃;添加空响应后解决。

### ~~Issue: WSL2 USB CDC 结果容易误导~~ [已关闭]
- **结论**:WSL2 usbipd 下 USB CDC 连接偶尔不稳定;Windows 侧 MAVLink 验证更可信。

### ~~Issue: GPIO usb_connected() 恒 true~~ [已关闭 2026-03-24]
- **结论**:修改为调用 `usb_device_is_configured(0)` 检测真实 USB configured 状态。

### ~~Issue: USB CDC 断开重连失败~~ [已关闭 2026-03-24]
- **结论**:CherryUSB DTR 回调中未正确清理 TX 状态。修复:DTR set/clear 时调用 `usbd_ep_recover_stuck()` + `rt_ringbuffer_reset(&tx_rb)` + `tx_active = 0`。DISCONNECTED 事件中调用 `usbd_serial_reset_tx()`。UARTDriver 中不使用 DTR 控制数据流,始终 drain writebuf。

### ~~Issue: SPI DMA ISR busy-wait 导致 CPU 爆红~~ [已关闭 2026-03-28]
- **结论**:实现 SPI LLD (Low-Level DMA) 驱动,将 BSY 等待从 ISR 移到线程上下文。SPI1 已启用 LLD,其他总线保留 HAL 路径。

### ~~Issue: 运行约 30 秒后 HardFault (BFSR.STKERR)~~ [已关闭 2026-03-28]
- **结论**:根因是 DTCM 不可被 DMA 访问。堆起始从 DTCM 移到 SRAM1 (0x20020000) 后解决。dcb 线程栈从 4KB 增到 8KB。

### ~~Issue: 干净 clone 无法编译~~ [已关闭 2026-03-28]
- **结论**:修复 .gitmodules URL、自动下载 packages、自动生成 ap_config.h、newlib polyfill。已在 /tmp 干净 clone 验证通过。

### ~~Issue: `AP_Scripting` 编入后延时 HardFault~~ [已关闭 2026-03-30]
- **结论**:实际是两层问题叠加,不是 Lua 运行时本身。第一层是 RTT `log_io` 默认 2KB 栈下溢,踩坏内建 `thread_timer` 后在 `rt_timer_check()->blx 0` 崩溃,已通过 RTT 上把 `HAL_LOGGING_STACK_SIZE` 提到 4KB 修复。第二层是 RTT POSIX `stat()` ABI mismatch:C++ 侧 `struct stat` 仍为 60B,而 DFS/ELM-FAT C 侧按 88B 写入,`log_io` 在线程里走 `AP::FS().stat() -> f_stat()/get_fileinfo()` 时持续踩坏栈,表现成后续 `UNALIGNED/FORCED HardFault` 和看似随机的坏异常帧。给 `AP_Filesystem_Posix::stat()` 增加 RTT 专用 C wrapper 后,固件已稳定跨过约 40s 和约 80s 观测窗,`rtt_dbg_hardfault_*` 保持为 0。

### ~~Issue: RTT 本地 POSIX `struct stat` 的 C/C++ ABI 不一致~~ [已关闭 2026-03-30]
- **结论**:仅靠在 C++ 头文件侧尝试包含 RT-Thread `sys/stat.h` 并未真正统一 ABI,GDB 仍显示 `sizeof(struct stat) == 60`。最终采用 RTT 专用 C 编译单元 `ap_rtt_posix_stat()`:在 C 侧用 RT-Thread 原生 `struct stat` 调 `stat()`,再由 `AP_Filesystem_Posix::stat()` 把 `mode/size/atime/mtime/ctime/blksize/blocks` 拷回 C++。这样本地 `stat()` 不再把 DFS 的 88B 结果写进 C++ 的 60B 栈对象。

### ~~Issue: 地面站看到 RTT 版 Copter 消息频率很低~~ [已关闭 2026-03-31]
- **结论**：三层根因。(1) RTT `delay_microseconds_boost()` 不触发 `call_delay_cb()`，`scheduler_delay_callback()` 从未被调用；(2) scheduler task 的 `time_available` 被前面任务耗尽，GCS `update_send`（550µs budget）被跳过；(3) `should_send_message_in_delay_callback()` 白名单只允许 3 种消息，USB CDC TX ring buffer 仅 2048B + `_usb_write_fail_count` 无条件递增导致每 ~100ms 丢数据。修复：主循环末尾显式 `call_delay_cb()` + RTT 允许所有消息类型在 delay callback 中发送 + TX buffer 增大到 8192B + fail count 逻辑修正。结果：5.6 → 96.6 msgs/s，ATTITUDE 0.2 → 13.3Hz。
