# AP_HAL_RTT 当前状态

> 基线：`CUAV v5` / `STM32F767` / `ArduCopter V4.7.0-dev on RT-Thread`

## 当前稳定成立的事实

- `bootloader -> RTT app -> scheduler -> main -> hal.run()` 主链路已跑通
- `CUAV v5` 上的 USB CDC / MAVLink 已在 Windows 主机侧验证到可用
- SPI 传感器链已带起，至少 `ICM20689` 与 `MS5611` 形成有效数据路径
- 已观测到 `HEARTBEAT`、`ATTITUDE`、`RAW_IMU`、`SCALED_PRESSURE`、`STATUSTEXT`、`TIMESYNC`
- MAVLink 参数下载：**944/943 全部完成**（25s 内，MAVLink2 格式）
- 主循环频率：**~410Hz 稳定运行**（GDB 实测 4094 iters/10s，连续 60s 无衰减）
- Setup 时间：~10s 完成

## 当前可信的验证边界

- Windows 地面站或 Windows 串口侧的验证结果，优先级高于 WSL2 `usbipd` 下的 CDC 结果；该判据的设计取舍以 `decision-log.md` 中对应条目为准
- `docs/AP_HAL_RTT_STATUS.md` 中的 CUAV v5 运行状态，可视为当前实现基线
- `docs/AP_HAL_RTT_ARCHITECTURE.md` 中"`hwdef.dat` 为唯一板级真相源"的方向，视为当前结构目标

## 当前已知限制

- `EKF3` 仍存在内存压力，当前基线允许回退到 `DCM active`
- `Storage` 仍非可靠持久化后端，`HAL_WITH_RAMTRON` 维持关闭基线
- `RCOutput`、`RCInput`、`AnalogIn`、部分 `GPIO/I2C/Flash` 仍未完成或未形成实体飞行级能力
- `WSL2 usbipd` 下的 USB CDC 双向通信不作为唯一正确性判据

## 当前结构性判断

- 现有代码功能上已越过最初 bring-up 阶段，主循环和 MAVLink 链路均达可用水平
- 当前主要矛盾已从"能否跑起来"转向"如何整理成可持续维护、可多板迁移的体系"
- 后续整理应优先保护 `CUAV v5` 已验证基线，不为追求结构完美而打断稳定链路
- 逐驱动验证层已进入治理系统规划，用于把后续问题拆成 driver / bus / subsystem 门禁
- `delay_microseconds` 必须使用 `rt_thread_delay()` 而非 busy-wait — 这是 RT-Thread 同优先级 round-robin 调度下的硬约束（根因见 `agent-trace.md` 2026-03-23 条目）
