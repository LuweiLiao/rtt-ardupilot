# AP_HAL_RTT ↔ ChibiOS 对齐状态追踪

> 最后更新: 2026-03-23 (D-Cache + MPU 修复后)
> 目标板: CUAV V5 (STM32F767)
> 分支: staging/pogo-rtt

## 总体进度: 🟡 80% 对齐

---

## 一、内核调度层

| # | 对齐项 | ChibiOS 行为 | RTT 当前状态 | 状态 |
|---|--------|-------------|-------------|------|
| 1.1 | 系统 tick 频率 | 10kHz (CH_CFG_ST_FREQUENCY) | 10kHz (RT_TICK_PER_SECOND) | ✅ 完成 |
| 1.2 | delay_microseconds | chThdSleep, 始终 yield | ≤400µs busy-wait, >400µs yield | 🟡 可优化 |
| 1.3 | delay_microseconds_boost | boost 到最高优先级(182) | boost 到 prio 9, 始终 yield | ✅ 完成 |
| 1.4 | boost_end | 恢复到 prio 180 | 恢复到 prio 10 | ✅ 完成 |
| 1.5 | 线程优先级体系 | MAIN=180, TIMER=181, SPI=181 | MAIN=10, TIMER=8, DeviceBus=10 | 🟡 接近 |
| 1.6 | 主循环频率 | ~400Hz | 待重测(D-Cache 提升后) | 🟡 待测 |
| 1.7 | expect_delay_ms | 看门狗抑制 | 空实现 | 🟡 需验证 |

## 二、SPI/I2C 驱动层

| # | 对齐项 | ChibiOS 行为 | RTT 当前状态 | 状态 |
|---|--------|-------------|-------------|------|
| 2.1 | SPI DMA 传输 | 全部 DMA | SPI1/2/4 DMA 已启用 | ✅ 完成 |
| 2.2 | DMA 流/通道配置 | hwdef.dat 自动分配 | board.h 手动配置 | ✅ 完成 |
| 2.3 | SPI 时钟频率 | 由 hwdef 配置 | 使用 HAL 默认 | 🟡 需验证 |
| 2.4 | DMA cache 管理 | DTCM + bouncebuffer (bypass D-Cache) | drv_spi.c flush/invalidate 已修复 + MPU WT | ✅ 完成 |
| 2.5 | 总线互斥锁 | 优先级继承 mutex | RT-Thread mutex (有优先级继承) | ✅ 完成 |
| 2.6 | DeviceBus 周期精度 | chThdSleep (µs 级) | rt_thread_mdelay(ms 级) | ❌ 精度不足 |

## 三、USB/串口通信层

| # | 对齐项 | ChibiOS 行为 | RTT 当前状态 | 状态 |
|---|--------|-------------|-------------|------|
| 3.1 | USB CDC 驱动 | ChibiOS USB stack | CherryUSB | ✅ 工作 |
| 3.2 | USB TX 缓冲 | 2048 bytes | 2048 bytes | ✅ 对齐 |
| 3.3 | USB RX 缓冲 | 1024 bytes | 1024 bytes | ✅ 对齐 |
| 3.4 | CDC ZLP 处理 | 不发流式 ZLP | 已修复不发 ZLP | ✅ 完成 |
| 3.5 | UART _timer_tick | 1kHz 在 timer 线程 | 1kHz 在 timer 线程 | ✅ 对齐 |
| 3.6 | have_flow_control | USB 报告有流控 | ✅ _flow_control=ENABLE for USB | ✅ 完成 |
| 3.7 | USB D-Cache 管理 | 不需要(DWC2 内部) | CONFIG_USB_DCACHE_ENABLE=1 | ✅ 完成 |

## 四、MAVLink/GCS 层

| # | 对齐项 | ChibiOS 行为 | RTT 当前状态 | 状态 |
|---|--------|-------------|-------------|------|
| 4.1 | 参数下载速度 | ~19s / 1011 params | ~15.5s / 943 params (待重测) | 🟡 接近 |
| 4.2 | 心跳频率 | 1Hz 稳定 | 1Hz 稳定 | ✅ 完成 |
| 4.3 | EKF 状态更新 | 实时 | 略慢 | 🟡 需验证 |

## 五、硬件加速层 (新增)

| # | 对齐项 | ChibiOS 行为 | RTT 当前状态 | 状态 |
|---|--------|-------------|-------------|------|
| 5.1 | I-Cache | 启用 | 启用 | ✅ 完成 |
| 5.2 | D-Cache | 启用 | **启用 (Write-Through via MPU)** | ✅ 完成 |
| 5.3 | Flash ART Accelerator | 启用 (ACR=0x307) | 启用 (ACR=0x307) | ✅ 完成 |
| 5.4 | Flash Prefetch | 启用 | 启用 | ✅ 完成 |
| 5.5 | MPU 配置 | 未启用 | SRAM Write-Through + Peripheral Device | ✅ 完成 |
| 5.6 | 系统时钟 | 216MHz PLL | 216MHz PLL (RCC_CR=0x3038883) | ✅ 完成 |

---

## ChibiOS vs RTT 硬件寄存器对比（GDB 实测 2026-03-23, D-Cache 修复后）

| 寄存器 | ChibiOS 值 | RTT 值 | 状态 |
|--------|-----------|--------|------|
| **RCC_CR** | `0x3038883` | `0x3038883` | ✅ 完全一致 |
| **Flash ACR** | `0x0307` | `0x0307` | ✅ 完全一致 |
| **SCB CCR** | `0x70200` | `0x70200` | ✅ **完全一致！D-Cache+I-Cache** |
| **MPU CTRL** | `0x0` | `0x5` (WT模式) | ✅ RTT 额外保护 |
| **SPI1 SR** | `0x2` (正常) | 待确认 | 🟡 待验证 |

### D-Cache 安全策略对比

| 方面 | ChibiOS | RTT (修复后) |
|------|---------|-------------|
| D-Cache 模式 | Write-Back (默认) | **Write-Through (MPU)** |
| DMA 缓冲区 | DTCM + bouncebuffer | 堆分配 + cache flush/invalidate |
| Cache 管理 | 不需要(DTCM bypass) | drv_spi.c 显式管理 |
| 外设空间 | 默认 Device | MPU Region 1: Device |
| 稳定性 | ✅ | ✅ (MPU WT 解决了 invalidate 误伤问题) |

---

## 当前优先级

### P0: ✅ D-Cache + MPU + Flash ART = 硬件加速完全对齐
- I-Cache ✅
- D-Cache ✅ (Write-Through via MPU, 避免 invalidate 误伤栈数据)
- Flash ART Accelerator ✅
- Flash Prefetch ✅
- SPI DMA cache bug 已修复 (flush 参数 + RX invalidate)

### P1: 主循环频率 → 400Hz
- D-Cache 启用后需重新测量
- SPI DMA 状态管理需验证

### P1: USB 连接稳定性
- usbipd detach/attach 后经常丢失连接
- 需要调查 usbd_serial_open() 阻塞行为

### P2: 参数下载速度最终验证
- 之前 15.5s/943 params, ChibiOS 19s/1011 params
- D-Cache 启用后需重新测量

### P3: AHRS 姿态响应延迟
- 可能与主循环频率有关，先解决 P1

---

## 修改历史

| 日期 | 修改 | 效果 |
|------|------|------|
| 2026-03-20 | delay_microseconds: busy-wait ≤100µs | 主循环 25Hz→78Hz |
| 2026-03-21 | RT_TICK_PER_SECOND 1000→10000 | 主循环 78Hz→191Hz |
| 2026-03-21 | USB CDC ZLP 修复 | USB 不再死锁 |
| 2026-03-21 | delay_microseconds 混合策略 (≤400µs busy-wait) | 参数 47s |
| 2026-03-21 | SPI DMA 启用 (SPI1/2/4) | SPI 传输走 DMA |
| 2026-03-22 | boost prio 9 + DeviceBus prio 10 | 主循环 53Hz→163Hz |
| 2026-03-22 | USB RESET/CONFIGURED tx_active 复位 | usbipd 切换后 TX 修复 |
| 2026-03-22 | USB CONFIGURED OUT 端点重新 arm | usbipd 切换后 RX 修复 |
| 2026-03-22 | USB flow_control = ENABLE | 参数 37.4s→36.1s |
| 2026-03-22 | _writebuf 8192B for USB | 初始速率 45→60 p/s |
| 2026-03-23 | Flash PREFETCH_ENABLE=1 + SCB_EnableICache() | I-Cache 启用 |
| 2026-03-23 | drv_spi.c polling loop: 50ms timeout + yield | SPI polling 不卡死 |
| 2026-03-23 | ChibiOS debug 编译+GDB 对比分析 | 发现 D-Cache 是关键差异 |
| **2026-03-23** | **drv_spi.c cache bug 修复** | **flush 用 p_txrx_buffer, RX invalidate 补全** |
| **2026-03-23** | **MPU Region 0: SRAM Write-Through** | **D-Cache 安全启用, 无 HardFault** |
| **2026-03-23** | **SCB_EnableDCache()** | **硬件加速完全对齐 ChibiOS** |

## 失败尝试记录

| 尝试 | 问题 | 根因 |
|------|------|------|
| DeviceBus prio 9 (高于 main 10) | init 时主循环卡死(0 iterations) | DeviceBus 在 init 完成前抢占 main |
| boost prio 7 (高于 timer 8) | 同上 | 需要 _hal_initialized 门控 |
| DeviceBus next_usec 周期跟踪 | 主循环卡死(106 iterations) | 回调耗时>周期时 tight loop |
| tx_pkt 64→512B | USB TX 永久卡死(bulkin=0) | DWC2 不支持 >64B 单次传输 |
| tx_rb 2048→4096B | USB 几乎不工作 | 可能 DWC2 FIFO/内存问题 |
| _write() 内直接 drain | 只收到 7 params | timer+main 竞争 drain 的数据竞争 |
| 直接 SCB_EnableDCache() 无 MPU | 239 次循环后 HardFault | D-Cache invalidate 误伤栈数据 |
| ChibiOS 修改版 bootloader | ChibiOS 固件 HardFault | bootloader 时钟配置不兼容 |
