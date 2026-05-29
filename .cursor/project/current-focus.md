# Current Focus

## 当前阶段
**USB 栈已收口**：CherryUSB 于 2026-05-29 设为 SERIAL0 生产默认并提交 milestone `85c4f83b3e`（仅 staged clean set；工作区其余大改未纳入）。受控全量回归已 PASS（双 backend 构建 IRQ 唯一 + L0 gate exit 0 + MAVFTP 6/6 + Mission PASS + 600s soak fault=0/无 IWDG + 软重连 + 多轮参数 904×3，详见 `status.md`）。`native` 仍可 `RTT_USB_BACKEND=native` 显式回退。

**MAVFTP 回归已修复并本地提交（2026-05-29，commit `28e8537fe4`，未 push）**：milestone 后未提交改动引入「关中断 USB poll 阻塞 CDC TX 完成 ISR + 删背压泄放」→ MAVFTP 高吞吐突发抖动 2-4/6。#1 裸 poll（Scheduler `_poll_usb_if_active`）+ #2 恢复 `>500 fail 清 _writebuf` 已修复，单轮 `tests/test_mavftp.py` **恢复 6/6**（= milestone 单轮同等）、`clears>0`、无 HardFault。该 commit **仅含 #1+#2 + 本轮记忆**，工作区其余 ~172 项改动（FRAM/GPS/compass/测试树等）**未提交**，按 commit 拆分计划后续分批。详见 `open-issues.md`「MAVFTP 回归根因与 #1+#2 修复」。

**未覆盖/暂缓**：MAVFTP **背靠背 N 轮** session/EOF 恢复残留（超出 milestone 基线，含测试重连争用 + 固件残留两因，暂缓）；RCOut/RCIn/S_rc_chain（用户暂缓）；整机飞行链；物理 USB 拔插；其余工作区分批提交；是否 push/合入 CI（用户已选**仅本地 commit**，push/CI 待后续明确）。

## 重大突破（2026-04-12）
- **App 自由运行验证通过**：使用正确 bootloader（ArduPilot CUAVv5_bl.bin），等 12 秒后 app 完全运行
- **主循环 11279 次迭代（60 秒内）**，VTOR=0x08008000，PC 在 app 代码区
- **之前"回到 bootloader"是假象**：bootloader 正常上电等 5 秒才跳转，测试只等了 1~3 秒
- **构建/烧录链路已完整**：scons → openocd program bootloader(0x08000000) + app(0x08008000)

## 当前主线目标
1. **半合入清理 + CherryUSB clean 合入主仓**（`rtt_usb_backend`、vendor、去误拷贝目录）
2. **主仓树 Cherry L0 重验**（OpenOCD + ACM1 MAVLink，边界同 status.md）
3. **全功能回归清单**（MAVFTP、Mission、USB 重连等 — 在 Cherry 默认化之后）
4. RCInput / RCOutput / 其余硬件项（与 open-issues 一致）

## 当前优先级
1. **串口/MAVLink 验证**：USB CDC 或 UART 确认 GCS 连接
2. **RCInput SBUS 验证**：接 SBUS 接收机验证 RC 通道
3. **Servo 输出验证**：通过 GCS 命令驱动电调/舵机
4. 完整系统级回归测试

## 推荐起手动作
1. 连接 USB 到 PC，等 12 秒后检查 `/dev/ttyACM*` 是否出现
2. MAVLink 测试：`python3 -c "from pymavlink import mavutil; m=mavutil.mavlink_connection('/dev/ttyACM1'); print(m.wait_heartbeat())"`
3. 编译：`python3 -m SCons --target=cuav-v5 -j16`
4. 烧录 bootloader：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program Tools/bootloaders/CUAVv5_bl.bin 0x08000000 verify reset exit"`
5. 烧录 app：`openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 verify reset exit"`
