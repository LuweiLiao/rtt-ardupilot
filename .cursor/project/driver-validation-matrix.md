# Driver Validation Matrix

本文件记录“当前哪些驱动已有门禁、状态如何”；完整方法论和分层原则以 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` 为准，入口约束以 `rtt-driver-validation` Skill 为准。

| Driver | 验证层级 | 建议 example/test | 当前状态 | 通过判据 | 适用板卡 | 备注 |
|---|---|---|---|---|---|---|
| `Scheduler` / `DeviceBus` | Board Example | `scheduler-smoke` | 待规划 | 周期回调与调度行为稳定可观测 | `cuav_v5` 优先 | 与主循环频率问题强相关 |
| `UARTDriver` | Board Example | `uart-smoke` | 待规划 | 初始化成功，TX/RX 路径可观测 | `cuav_v5` 优先 | 可优先复用 `AP_HAL/examples/UART_test` 风格 |
| `USB CDC` | Board Example | `usb-cdc-smoke` | 待规划 | Windows 侧枚举与基础收发成立 | `cuav_v5` 优先 | Windows 结果优先于 WSL2 |
| `SPI` | Board Example | `spi-ms5611-smoke` | 待规划 | 设备找到、传输成立、PROM CRC 正确 | `cuav_v5` 优先 | 检查设备表、CS、锁语义 |
| `IMU` | Board Example | `imu-whoami-smoke` | 待规划 | 至少一个目标 IMU 返回合法 ID | `cuav_v5` 优先 | 与 `SPI` 验证层相邻 |
| `Storage` | Board Example | `storage-smoke` | 待规划 | 写读一致；若持久化后端则重启后仍成立 | `cuav_v5` 优先 | 当前基线仍以 RAM stub 为主 |
| `RCOutput` | Board Example | `pwm-output-smoke` | 待规划 | 至少一路 PWM 输出成立 | 后续 | 当前能力仍未完整 |
| `AnalogIn` | Board Example | `analogin-smoke` | 待规划 | ADC 值可读，采样节拍成立 | 后续 | 当前能力仍未完整 |
| `hwdef` 解析 | Host Test | `test_rtt_hwdef_parse` | 待规划 | 设备表 / probe 列表生成符合预期 | 所有板 | 适合后续 CI |
| `SPI device table` | Host Test | `test_rtt_spi_table` | 待规划 | 设备名、总线、devid 映射正确 | 所有板 | 可防回归 |

## 使用规则

- 该矩阵记录“该驱动应如何独立验证、当前是否已有门禁”，不是当前系统运行结果本身
- 当前稳定事实仍以 `status.md` 为准
- 当前未关闭问题仍以 `open-issues.md` 为准
- 某 example/test 真正落地后，再把状态从“待规划”更新为“已规划 / 已实现 / 已通过 / 阻塞”
