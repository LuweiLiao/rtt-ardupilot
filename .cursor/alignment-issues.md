# RTT-ChibiOS 对齐 — 问题分析与失败记录

> 最后更新: 2026-03-24
> 注意: 此文件为专项补充档案，主问题入口为 `project/open-issues.md`

## 已关闭问题

### USB IN 端点在 usbipd detach/attach 后不工作 [已关闭]
- 结论：WSL2 usbipd 环境问题，非固件缺陷

### 主循环频率不达标 / CPU 爆红 [已关闭]
- 结论：四个根因均已修复：D-Cache 启用、编译优化 -O2、rt_kprintf 移除、check_called_boost 对齐
- CPU 从 95-100% 降至 6.5%

### 参数不持久化 [已关闭]
- 结论：Flash 后端验证闭环

### 磁力计不出现 [已关闭]
- 结论：rt_components_board_init() 未被调用，I2C 驱动未初始化

### MAVLink 卡死 / 日志请求冻结 [已关闭]
- 结论：HAL_LOGGING_ENABLED=0 时 LOG_REQUEST_LIST 被静默丢弃；添加空响应后解决

### 校准时地面站冻结 [已关闭 2026-03-24]
- 结论：BinarySemaphore::wait(0) 语义反转（实现为永久阻塞而非非阻塞）导致死锁
- 修复：take(0)/wait(0) 改为非阻塞，新增 take_blocking()/wait_blocking()

### PreArm: Logging failed [已关闭 2026-03-24]
- 结论：实现 SD 卡 + Filesystem Logging 后端后解决

### AnalogIn 未被定时调用 [已关闭 2026-03-24]
- 结论：在 Scheduler::_run_timers() 中添加 _timer_tick() 调用

### SD 卡编译失败 — drv_sdmmc.c 用于 F7 [已关闭 2026-03-24]
- 结论：RT-Thread SConscript 错误地将 F7 导向 H7 的 drv_sdmmc.c；修改为使用 drv_sdio.c

### SD 卡编译失败 — RT_USING_BLK 未定义 [已关闭 2026-03-24]
- 结论：block device layer 宏缺失；在 rtconfig.h 添加 #define RT_USING_BLK

### UART7 编译失败 — UART7_CONFIG 未定义 [已关闭 2026-03-24]
- 结论：F7 的 uart_config.h 只定义到 UART6；补充 UART7_CONFIG 和 UART8_CONFIG

## 当前活跃问题

详见 `project/open-issues.md`
