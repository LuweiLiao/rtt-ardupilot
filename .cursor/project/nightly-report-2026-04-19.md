# Nightly Progress Report — 2026-04-19

## Summary

- **Branch:** `staging/pogo-rtt`
- **Base commit:** `712e46ec99`
- **Commits:** 30 (since base)
- **Files changed:** 25 (+1514 / -30 lines)
- **Binary size:** 1.28 MiB (`rtthread.bin`)
- **ROM:** 1,308,128 bytes | **RAM:** 193,452 bytes (data + bss)

### Key Achievements

1. **Flash filesystem** — 实现了 RT-Thread DFS elmfat 挂载到内部 Flash block device，带 dirty block 跟踪的擦写策略
2. **参数持久化** — 修复 FRAM SPI bus 错误，参数跨重启保存验证通过
3. **MAVROS 2 连通** — 921600 baud 下全功能验证通过（参数读写、心跳、传感器数据）
4. **hwdef 对齐** — 完成 RTT vs ChibiOS 的全面审计，补齐缺失引脚定义
5. **IOMCU 禁用** — 消除 50s 启动延迟（CUAV V5 无 IOMCU 硬件）

---

## Fixed Issues

| Issue | Commit | Description |
|-------|--------|-------------|
| SPI bus 硬编码 SPI1 | `cfc596840c` | 按实际 bus 选择 SPI 外设 |
| SPI register poll 超时 | `f8c02d2f9b` | 扩展 poll workaround 到所有 SPI bus |
| FRAM 读写崩溃 | `0167ae22da` | 增加 FRAM 读写验证防止 crash loop |
| Flash erase 策略错误 | `bb00d4445f` | dirty block tracking，避免全擦 |
| hwdef 缺失引脚 | `f60c8fcd6d` | CAN1_SILENT, TIM9 alarm, SWD debug pins |
| IOMCU 启动延迟 50s | `beece1ac7a` | 禁用 IOMCU（无硬件） |
| MAVLink param 异步竞争 | `5f03126f68` | ObjectBuffer_TS 线程安全队列 |
| 参数请求回复调度 | `b41c62dd91` | MSG_NEXT_PARAM 异步调度 |

---

## Remaining Issues

### PARAM_REQUEST_READ via USB CDC — ❌ 未解决

- **现象：** 通过 USB CDC 发送 `PARAM_REQUEST_READ`，飞控不回复
- **已尝试：**
  - v1: 直接 send (`b41c62dd91`) — 失败
  - v2: txspace drain + direct send (`d8ea9515bc`) — 失败，已 revert
- **根因分析（见 `usb-cdc-tx-analysis.md`）：** USB CDC TX 路径中 txspace 信号与 MAVLink 发送存在竞争，UART 路径正常但 USB CDC 的 DMA/中断上下文不同
- **影响：** Mission Planner 通过 USB 连接时参数读取失败，MAVROS 通过串口不受影响
- **优先级：** 中（有串口 workaround）

---

## Verified Working

- ✅ MAVROS 2 连接（921600 baud，UART4）
- ✅ 参数持久化（跨重启）
- ✅ MAVLink 心跳、流数据（STREAMRATE）
- ✅ 传感器健康（RC/EKF/GPS/Baro 报告正常）
- ✅ BOARD_ID 正确
- ✅ MAVLink logging（内存后端）
- ✅ Flash 文件系统挂载和读写
- ✅ FRAM 参数存储

---

## Not Working / Needs Hardware

- ⏳ **USB CDC PARAM_REQUEST_READ** — 需要深入 USB CDC TX 驱动调试
- ⏳ **DShot 输出** — 引脚映射已对齐，需硬件示波器验证
- ⏳ **CAN 驱动** — 可行性分析完成，代码未实现
- ⏳ **SDIO SD 卡** — 可行性分析完成，代码未实现
- ⏳ **PWM 输出** — 引脚映射已对齐，需硬件验证

---

## Next Steps (Prioritized)

1. **USB CDC TX bug** — 深入调试 CDC ACM 驱动的 txspace 机制，解决 PARAM_REQUEST_READ 不回复问题
2. **PWM/DShot 硬件验证** — 连接示波器/电机，确认输出信号正确
3. **CAN 驱动实现** — 基于 `can-driver-report.md` 的可行性分析开始编码
4. **SD 卡支持** — 基于 `sdio-feasibility.md` 实现 SDIO DFS 驱动
5. **飞行测试** — 基本传感器 + MAVROS 验证通过后，进行绑桨测试
