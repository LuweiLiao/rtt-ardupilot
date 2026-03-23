# RTT-ChibiOS 对齐 — 问题分析与失败记录

## 当前阻塞问题（按优先级排序）

### P0: USB IN 端点在 usbipd detach/attach 后不工作
- **症状**: 第一次 attach 后 USB TX 正常（收到 207B 心跳）；后续 detach/attach 后 0 字节
- **GDB 诊断数据**:
  - `DIEPCTL1 = 0x8000` → USBAEP=1 但 MPS=0, EPENA=0
  - `tx_kick = 1`, `tx_kick_fail = 0`, `bulkin_cnt = 0`
  - `usb_event_reset_count = 12`, `usb_event_configured_count = 3`
- **根因分析**: USB RESET 清零了 DWC2 IN 端点的 DIEPCTL（包括 MPS）。虽然 CONFIGURED 事件触发了 CherryUSB 重新配置端点，但可能最终的一次 RESET 发生在最后一次 CONFIGURED 之后（usbipd 产生多次 RESET）
- **已尝试**:
  1. `usbd_serial_reset_tx()` 清除 tx_active + rearm RX → 部分有效
  2. 分离 `usbd_serial_rearm_rx()` → 无改善
  3. 移除 `usb_device_is_configured` 检查 → 无改善
- **待尝试**: 在 `kick_tx` 中检测 `usbd_ep_start_write` 返回 -2 并重试/等待 CONFIGURED

### P0: 主循环极慢（20s 一次迭代 vs ChibiOS 400Hz）
- **症状**: `rtt_dbg_main_loop_iterations` 在 20s 内只增加 1-2 次
- **根因分析**: SPI DeviceBus 线程使用轮询式 SPI 传输，持续占据 CPU
- **已尝试**:
  1. `boost_end()` 用 `RT_MAIN_THREAD_PRIORITY(10)` → 饿死（main prio 10 < SPI prio 8）→ 修复为 `RTT_PRIO_MAIN(8)`
  2. `_main_loop_entry` 中提升 main 到 `RTT_PRIO_MAIN(8)` → 仍然慢
  3. SPI bus 降到 `RTT_PRIO_SPI_BUS(9)` → 主循环从 0 变为有，但仍极慢
  4. SPI bus 恢复到 `RTT_PRIO_MAIN(8)` 同优先级 → 待测试
- **关键发现**:
  - ChibiOS SPI 用 DMA，SPI 传输期间线程 sleep
  - RT-Thread SPI 虽然配置了 DMA（`rt_completion_wait`），但 `HAL_SPI_Transmit` 用的是轮询模式
  - 轮询 SPI 传输会持续占据 CPU 直到传输完成

### P1: AHRS 姿态响应延迟
- **症状**: 地面站显示 AHRS 更新缓慢
- **根因**: 与主循环频率直接相关。主循环慢 → AHRS 更新慢

### P1: 参数下载速度
- **最佳成绩**: 15.5s（通过修改 ArduPilot 核心代码，已回退）
- **当前基线**: ~70-130s（仅 RTOS 层修改）
- **根因**: 主循环极慢导致 queued_param_send 调用频率低

## 失败尝试记录

| # | 尝试内容 | 结果 | 原因 |
|---|---------|------|------|
| 1 | tx_pkt 从 64B 增到 512B | HardFault | DWC2 TX FIFO 只有 64B |
| 2 | DeviceBus prio 9 + tick 级周期补偿 | 主循环饿死 | SPI 高频唤醒抢占 main |
| 3 | boost_end 恢复到 RT_MAIN_THREAD_PRIORITY(10) | 主循环饿死 | prio 10 < SPI bus prio 8 |
| 4 | GCS_Param.cpp 增大 CPU budget | 有效但已回退 | 用户约束：不改 ArduPilot 核心 |
| 5 | GCS_Common.cpp 减小 param interval | 有效但已回退 | 用户约束：不改 ArduPilot 核心 |
| 6 | UART TX 独立线程 | 性能反而下降 | 数据竞争 + 优先级不当 |
| 7 | usbd_serial_write 阻塞等待 | 无改善 | 阻塞在高优先级线程浪费时间片 |
| 8 | flash 到 0x08000000 | HardFault | 覆盖了 bootloader |
| 9 | 移除 usb_device_is_configured 检查 | 无改善 | 问题不在这里 |

## 架构对比 (ChibiOS vs RTT)

### SPI 数据路径
```
ChibiOS: DeviceBus thread → spiStartSend(DMA) → chSysLock → sleep → DMA IRQ → wake
RTT:     DeviceBus thread → rt_spi_send_then_recv → HAL_SPI_Transmit(POLLING) → busy-wait
```
ChibiOS SPI 传输期间线程 sleep，RTT SPI 传输期间线程 busy-wait。这是主循环被饿死的根因。

### USB TX 数据路径
```
ChibiOS: _write→writebuf → timer_tick→drain→SDU→ep_start_write → bulk_in IRQ→chain
RTT:     _write→writebuf+event → TX thread→drain→rt_device_write→usbd_serial_write→kick_tx→ep_start_write → bulk_in IRQ→chain
```

### 主线程优先级生命周期
```
boot:    RT_MAIN_THREAD_PRIORITY (10)
_main_loop_entry: → RTT_PRIO_MAIN (8)  [新增]
wait_for_sample boost: → RTT_PRIO_BOOST (6)
boost_end: → RTT_PRIO_MAIN (8)  [修复: 之前是 10]
```
