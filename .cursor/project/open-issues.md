# Open Issues

## Issue: SPI LLD 仅覆盖 SPI1，SPI2/SPI4 仍走 HAL 路径
- 级别：低
- 现象：SPI1 (IMU) 已使用 LLD，但 SPI4 (Baro) 等仍走 HAL DMA 路径
- 当前判断：SPI4 传输频率较低，HAL busy-wait 影响可忽略
- 下一步：如需进一步降 CPU 负载，可为其他总线也注册 LLD 上下文

## Issue: SD 卡挂载需实际插卡验证
- 级别：高
- 现象：`sd0` Block Device 已在 RT-Thread 注册，`mmcsd_detect` 线程运行中，但 `ls /sd` 显示 `No such directory`
- 当前判断：SD 卡设备已识别但挂载可能因卡未插入或格式不对而失败；`sd_card_mount()` 已通过 `INIT_ENV_EXPORT` 注册
- 下一步：实际插入 FAT32 格式 SD 卡后复测 `ls /sd/APM/LOGS`

## Issue: mmcsd_detect 线程栈使用率 90%
- 级别：中
- 现象：`list thread` 显示 `mmcsd_detect` 栈使用 90%（1KB 栈）
- 当前判断：接近栈溢出边界，RT-Thread SD 卡检测线程默认栈偏小
- 下一步：考虑在 rtconfig.h 中增大 MMCSD 线程栈，或监测是否实际溢出

## Issue: RCOutput 缺 DShot / 安全开关 / IOMCU
- 级别：高
- 现象：仅有基础 PWM 输出；`force_safety_on()` 恒返回 false
- 当前判断：CUAV V5 硬件有 IOMCU 但 RTT 未实现；DShot 协议未实现
- 下一步：先确保基础 PWM 可控电调后再评估 DShot 优先级

## Issue: RCInput 无脉冲捕获路径
- 级别：高
- 现象：仅依赖 `AP::RC().update()` 处理串口协议 RC（如 SBUS）
- 当前判断：PPM/脉冲管脚输入未实现
- 下一步：评估 CUAV V5 实际 RC 接口类型，SBUS 路径可能已足够

## Issue: Util 覆盖面不足
- 级别：中
- 现象：缺 toneAlarm、safety_switch_state、watchdog_reset、flash_bootloader、random 等
- 当前判断：影响通知蜂鸣器、安全开关状态读取、看门狗历史
- 下一步：按 bring-up 优先级逐步补充

## Issue: HAL/BSP/hwdef 仍有硬编码耦合
- 级别：中
- 现象：部分板级事实散落在 HAL、BSP、构建逻辑中
- 当前判断：当前结构未完全达到 ChibiOS 式 hwdef 驱动
- 下一步：在不破坏 CUAV V5 基线前提下推动数据驱动化

## Issue: USB CDC 快速重连在 usbipd/WSL2 下不稳定
- 级别：中
- 现象：2s 间隔重连 5/5 稳定；1.5s 间隔 + 高流量数据流时 4/10
- 当前判断：usbipd USB Full Speed bulk 吞吐瓶颈（实测 ~742 bytes/s vs 理论 1MB/s），非固件 bug
- 验证：纯心跳重连 13/15 OK（1.5s 间隔），有数据流时退化
- 下一步：在真实物理 USB（非 usbipd）或 Windows COM 端口验证是否复现

## Issue: EKF3 内存压力
- 级别：低
- 现象：实机链路偶现 `EKF3 not enough memory`，当前允许回退到 DCM
- 当前判断：与 `HAL_WITH_EKF_DOUBLE` 和堆裕量相关
- 下一步：统计 RAM 占用来源

## Issue: CH343 USB-TTL 在 WSL2 无驱动
- 级别：低（不影响调试）
- 现象：CH343（VID:PID 1A86:55D3）attach 到 WSL2 后不出现 `/dev/ttyUSB*`
- 当前判断：WSL2 内核 ch341.ko 只匹配 1A86:7523（CH340/CH341），不匹配 CH343 的 55D3
- 解决方案：从 Windows 端直接用 COM33 连接 UART7 即可
- 下一步：无需修复，已有 Windows 端替代方案

---

## 已关闭问题

### ~~Issue: 校准时地面站冻结~~ [已关闭 2026-03-24]
- **结论**：两个根因：(1) `BinarySemaphore::wait(0)` 语义反转（RTT 实现为永久阻塞而非非阻塞），(2) `Scheduler::delay()` 缺少 `persistent_data.scheduler_task = -4` 赋值。修复信号量语义 + 对齐 Scheduler 后校准正常。

### ~~Issue: PreArm: Logging failed~~ [已关闭 2026-03-24]
- **结论**：实现 SD 卡文件系统 + Filesystem Logging 后端后解决。`HAL_LOGGING_FILESYSTEM_ENABLED=1` + `AP_FILESYSTEM_POSIX_ENABLED=1`。

### ~~Issue: AnalogIn `_timer_tick()` 未被 Scheduler 调用~~ [已关闭 2026-03-24]
- **结论**：已在 `Scheduler::_run_timers()` 末尾添加 `((AnalogIn*)hal.analogin)->_timer_tick()` 调用。

### ~~Issue: 主循环 400Hz / CPU 爆红~~ [已关闭 2026-03-24]
- **结论**：三个根因修复（D-Cache、编译优化、rt_kprintf 移除）+ check_called_boost 对齐。CPU 从 95-100% 降至 6.5%。

### ~~Issue: 参数持久化~~ [已关闭 2026-03-23]
- **结论**：Flash 后端已验证，`Storage` 通过 on-chip Flash（page 10-11）持久化参数。

### ~~Issue: 磁力计不出现~~ [已关闭 2026-03-24]
- **结论**：`rt_components_board_init()` 未被调用，I2C 驱动未初始化。修复后 IST8310 正常识别。

### ~~Issue: MAVLink 卡死 / 日志请求冻结~~ [已关闭 2026-03-24]
- **结论**：`HAL_LOGGING_ENABLED=0` 时 `LOG_REQUEST_LIST` 被静默丢弃；添加空响应后解决。

### ~~Issue: WSL2 USB CDC 结果容易误导~~ [已关闭]
- **结论**：WSL2 usbipd 下 USB CDC 连接偶尔不稳定；Windows 侧 MAVLink 验证更可信。

### ~~Issue: GPIO usb_connected() 恒 true~~ [已关闭 2026-03-24]
- **结论**：修改为调用 `usb_device_is_configured(0)` 检测真实 USB configured 状态。

### ~~Issue: USB CDC 断开重连失败~~ [已关闭 2026-03-24]
- **结论**：CherryUSB DTR 回调中未正确清理 TX 状态。修复：DTR set/clear 时调用 `usbd_ep_recover_stuck()` + `rt_ringbuffer_reset(&tx_rb)` + `tx_active = 0`。DISCONNECTED 事件中调用 `usbd_serial_reset_tx()`。UARTDriver 中不使用 DTR 控制数据流，始终 drain writebuf。

### ~~Issue: SPI DMA ISR busy-wait 导致 CPU 爆红~~ [已关闭 2026-03-28]
- **结论**：实现 SPI LLD (Low-Level DMA) 驱动，将 BSY 等待从 ISR 移到线程上下文。SPI1 已启用 LLD，其他总线保留 HAL 路径。

### ~~Issue: 运行约 30 秒后 HardFault (BFSR.STKERR)~~ [已关闭 2026-03-28]
- **结论**：根因是 DTCM 不可被 DMA 访问。堆起始从 DTCM 移到 SRAM1 (0x20020000) 后解决。dcb 线程栈从 4KB 增到 8KB。

### ~~Issue: 干净 clone 无法编译~~ [已关闭 2026-03-28]
- **结论**：修复 .gitmodules URL、自动下载 packages、自动生成 ap_config.h、newlib polyfill。已在 /tmp 干净 clone 验证通过。
