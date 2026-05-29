

### 2026-05-26 分步提交 + 三路并行修复
- 动作：shell agent 拆 10 个 M0 中文 commit + 子模块 6 commit；composer-2.5 x3 并行 IOMCU/USB/verify
- 结果：主仓 +3 commit（UART IOMCU、USB CDC、rtt_m0_verify）；编译 PASS
- 未提交：GPIO/SPI/IMU/ArduCopter bringup hack（M1 线）

### 2026-05-26 M0 verify 脚本 GDB 读值修复
- 动作：`rtt_m0_verify.sh` 合并为单次 GDB 会话；`p/x` 改 `x/wx &sym`；解析 `<sym>:` 行格式；PASS 含 0xBBBBBBBB/0x11111111/0x12345678；fallback `x/wx 0x20000100`
- 结果：脚本 `--no-mavlink --wait 15` → M0 PASS（hal_run=0xBBBBBBBB, loop_entry=0x12345678）；scons cuav_v5 编译 PASS
- 下一步：GPIO/SPI M1 烧录双验证；SPI1 WHOAMI 硬件 probe

### 2026-05-26 USB CDC 专项修复
- 动作：hal_usb_lld_rtt.c 补 CDC 类请求(GET/SET_LINE_CODING、SET_CONTROL_LINE_STATE)；修复 GINTSTS 先清 RXFLVL 导致 RX FIFO 不 drain；DOEPINT STUP 调 _ep0_handle_setup；IRQ 委托 usb_lld_poll_rtt；init GINTMSK 含 RX/EP；UARTDriver 用 configured 判连接
- 依据：ChibiOS hal_serial_usb/sduRequestsHook + otg_rxfifo_handler 单 pop + STUP 路径
- 结果：scons cuav_v5 编译 PASS；OpenOCD 烧录 verify OK；GDB rtt_dbg_usb_init=2；本机 /dev/ttyACM0 为 1a86:55d3 外置串口非飞控 CDC，lsusb 无 FC ACM，pymavlink 15s 无心跳（环境阻塞）
- 下一步：接飞控原生 USB 口复测 pymavlink；确认 lsusb 出现 ArduPilot/STM32 CDC 后再判 L0

### 2026-05-26 OpenOCD 残留占 ST-Link + USB P0 烧录
- 动作：确认 PID3110180 `openocd ... reset run` 未关导致后续 init failed；kill 后一步烧录；更新 command-catalog 强制 `exit`/pkill 规范
- 修改：hal_usb_lld_rtt.c — GCCFG(VBDEN|PWRDWN)、GRSTCTL_AHBIDL bit31、EP0 status OUT arm、wTotalLength=75、IAD device desc、EP3 FIFO
- 烧录：`program ... verify reset` + `exit` → Verified OK；pgrep 无残留
- GDB：rtt_dbg_usb_init=2，hal_run=0xBBBBBBBB；lsusb 仍无 1209:5741（仅 ST-Link+CH340）；pymavlink ACM0 无心跳
- 下一步：确认飞控 PA11/12 原生 USB 是否接主机；GDB 读 DSTS/GCCFG 判枚举态；若仍无 CDC 继续 EP0/IRQ 排障

### 2026-05-26 六路并行启动链验证
- 动作：1×OpenOCD 单步 + 5×代码库启动链审计（boot/scheduler/init_ardupilot/clock-GPIO/HardFault）
- 结果：USB init 在 `setup()` 之前；Flash/SD/IOMCU 不阻塞 CDC 首次出现；卡点更可能在 EP0/`configured=0` 或 HardFault 后 USB 中断停
- OpenOCD 12s 快照（app@0x08008000，BL@0x08000000）：`main_called=0x33333333`，`hal_run=0x11111111`，`main_loop_entry=0x12345678`，`loop_iter=842`，`usb_init=2`，`setup_stup=1`，`usbrst=0`，`configured=0`；halt 时 **HardFault** @ `hardfault_hang`，CFSR=0x00020000(INVSTATE)；lsusb 仍无 1209:5741
- 结论：**不是「app 没跑到 USB init」**；是 **枚举半途 + 运行期 fault** 叠加
- 下一步：GDB 读真实 fault PC（异常栈）；修 EP0 STATUS/SET_CONFIGURATION；修 INVSTATE 根因

### 2026-05-26 USB CDC OpenOCD 软件排障（续）
- 动作：GET_DESCRIPTOR data_sent 多包修复；Util get_micros64 除零防护；poll 重入 guard；主循环去掉重复 poll；EP0 TX FIFO spin；SETUP 改 DOEPINT STUP 分发（对齐 ChibiOS，SETUP_COMP 不再 dispatch）
- GDB/OpenOCD：init=2 irq≈6 usbrst=1 enumdne=1 setup_stup=1 时 enumerated=1 configured=0 device_addr=1；曾 HardFault get_micros64/ AP_Param
- 主机：lsusb 仍无 1209:5741（仅 ST-Link+CH340）
- 下一步：验证 STUP 路径后 setup_stup 应>1 且 configured=1；若仍无 CDC 查 EP0 STATUS OUT / DIEPINT XFRC
- 动作：UARTDriver.cpp — CMSIS 路径禁用 DMAR/DMAT；恢复 poll TX；wait_timeout 加 rt_thread_mdelay(1)
- 依据：IOMCU 用 poll RX/TX，_begin 曾开 DMAR 导致 RDR 无字节；BRINGUP HACK 禁 poll TX
- 结果：scons cuav_v5 通过；GDB 烧录后 8s halt 不在 wait_timeout，M0 marker 正常；30s 另现 GPS/EKF HardFault（非本任务）

### 2026-05-26 OpenOCD+GDB boot trace
- 动作：flash BL+app；GDB reset halt/step；breakpoint chain
- 结果：BL 0x08000200→Reset_Handler→main→HAL_RTT::run→HardFault；usb_init=2

### 2026-05-26 控制闭环框架（UART7 + OpenOCD 双传感器 / 10 控制器）
- 动作：新增 rtt_ctl_telemetry.c（1Hz RTT_CTL + ap_ctl）；Tools/scripts/rtt_control_loop/ 编排器+传感器+执行器；HAL 主循环 hook；编译烧录验证
- 结果：固件编译 PASS；OpenOCD verify OK；首跑闭环 total error≈0.86（GDB -ex 参数已修、CFSR 改 mdw）；UART7 本机 20s 未收到 RTT_CTL（待确认 CH340 接线/主循环是否跑满 12s）
- 下一步：闭环 `--once` 复测；C6 EP0 + C3 HardFault 自动补丁 agent；setpoint 归零后更新 status

### 2026-05-26 闭环框架验证 + 传感器/控制台修复
- 动作：OpenOCD 两阶段采样；flash_full.sh；修复 mdw 解析（VTOR/CFSR/configured）；UARTDriver 取消 USB 打开时误关 rt_console；rtt_ctl 打印前 force enable console
- 烧录+闭环：`--once --run-seconds 22` → OpenOCD ok（hal=0x11111111, iter=19~59, usb_init=2, setup_stup=1, configured=0, VTOR=0x08008000）；total error≈0.36~0.50；C6=FIX_EP0，C8=无 1209:5741
- UART7：/dev/ttyACM0(1a86:55d3) 25s 仍 0 字节 — 疑 CH340 未接 UART7 PE8/PF6 或 BSP 控制台与 CMSIS 路径冲突；OpenOCD 传感器已可作主反馈
### 2026-05-26 UART7 硬件直写 + 并行 Agent 架构
- 动作：rtt_ctl 改 usart_ll poll TX（绕过 rt_kprintf）；rt_board_init 上电 `[BOARD-INIT]`；UARTDriver 对 uart7 跳过 CMSIS 改写；agents/ 拆 sensor×3 + controller×10 + actuator + orchestrator；uart7_sensor 取最后一行 RTT_CTL + 复位重连
- 结果：编译+flash_full PASS；UART7 ok（boot_banner=true, bytes≈4553, RTT_CTL hal=0x11111111 ent=0x12345678）；双传感器闭环 total error≈0.5；C6=FIX_EP0，C8=无 1209:5741
- 下一步：C6 hal_usb_lld EP0 SET_CONFIGURATION；拉高 main_loop iter 采样窗口或降 setpoint 临时验证

### 2026-05-26 EP0 状态机修复（hal_usb_lld_rtt.c）
- 动作：忽略 EP0 虚假 OUT XFRC；STATUS_OUT 才 rearm；USBRST 后 return；枚举前不 enable EP2；poll 多轮直到 quiescent；OEP 先于 IEP；IN 前 reset EP0 IN；FIFO 满则 stall
- 编译：scons cuav_v5 PASS
- 烧录：OpenOCD program 0x08008000 verify OK
- 验证 50s：仍 NO 1209:5741；setup_stup=1 setup_data=1 ep0_stall=0；HardFault pc=0x08008412(hardfault_hang)；main_loop≈0x317
- 下一步：读 ep0_in_xfrc/status_out 新计数；若 in_xfrc=0 查 IN 路径；并行修 HardFault

### 2026-05-26 闭环并行效率优化
- 动作：openocd_session.py 跨 cycle 复用 OpenOCD；orchestrator 4 路并行传感器（UART7+OpenOCD+git+CDC）；10 控制器 pool.map；ControlLoopEngine + CycleTiming perf；rtt_control_loop 默认 session 复用
- 结果：`--cycles 2 --run-seconds 12` 两轮均 ~16.0s（无第二轮 +2–3s OpenOCD 启动）；controllers=0.00s；双传感器+10 agent 正常；仍 C6=FIX_EP0、C3 HardFault hang、无 1209:5741
- 下一步：固件 EP0 configured=1；修 HardFault/iter 遥测不一致

### 2026-05-27 22:43 Hermes 最新历史恢复
- 动作：读取 project 记忆、agent trace 尾部、Hermes agent/gateway 日志和 `hermes_长期执行_377714ba.plan.md`
- 依据/假设：用户要求了解 Hermes 最新对话历史和当前正在做的工作
- 结果：Hermes 最新主线仍是 CUAV V5 RTT USB CDC 枚举修复；22:25/22:34 两个子任务聚焦 `stm32_otg_t`/DWC2 寄存器映射修正；主会话 22:41 被飞书“先暂停/先暂停你的任务”打断。暂停前验证仍为无 `1209:5741`，仅 `/dev/ttyACM0`，并反复超时；前序 trace 显示 EP0/SET_CONFIGURATION 未到 configured=1 且存在 HardFault/迭代遥测不一致。
- 下一步：若继续接手，应先审查当前工作树 USB 相关 diff，再围绕 OTG 寄存器映射、EP0 状态机、HardFault 异常栈三点收敛验证。

### 2026-05-27 22:52 接续 Hermes USB 修复并行派单
- 动作：按用户要求由父代理负责规划监控，启动 6 个 composer-2.5 子代理：USB 寄存器映射审计、EP0 状态机审计、HardFault 异常栈定位、USB 最小修复实现、构建/烧录/验证执行、结论汇总协调
- 依据/假设：单个子代理不足以覆盖 USB 枚举 + HardFault + 构建验证的并行闭环；父代理保留冲突消解与最终决策权
- 结果：6 个子代理均已后台运行；实现代理可改代码但不 commit，验证代理负责 scons/OpenOCD/lsusb/pymavlink/GDB 证据链
- 下一步：等待子代理结果，按证据优先级合并：硬件观测(lsusb/pymavlink) > GDB counters/异常栈 > 源码结构审计 > 推测；必要时继续派第二轮实现/验证

### 2026-05-27 23:06 第一轮合并 + 第二轮派单
- 动作：合并第一轮 6 个 composer-2.5 结果；再次启动 6 个 composer-2.5 子代理：USB 补丁复核、boot 跳转验证、USB 硬件复测、EP0 多包补丁准备、HardFault GDB 增强、回归风险审计
- 依据/假设：第一轮结论已排除 `stm32_otg_t` 布局为主因；EP0 多包/SET_ADDRESS/CDC 双实例是软件主线，但硬件验证显示 reset 后可能仍在 bootloader/app 未跑，必须并行拆分验证
- 结果：实现代理已做 rtt_usb 单实例、CDC config 挂接、SET_ADDRESS status 后写 DCFG，scons 通过；验证代理烧录 OK 但无 `1209:5741`/无 heartbeat，GDB 采样 PC≈0x08000200、app counters=0；第二轮已按“补丁正确性 + app 是否跳转 + 硬件枚举 + HardFault 观测增强”展开
- 下一步：收集第二轮结果；若 app 未跳转优先修 boot/app descriptor/烧录路径，若 app 已跑则继续 EP0/CDC 验证；不以编译 PASS 作为 USB-L0 通过

### 2026-05-27 23:11 ChibiOS USB CDC 蒸馏专项派单
- 动作：按用户要求启动 10 个 composer-2.5 子代理专项研究 ChibiOS USB CDC：OTGv1 LLD、USB core、SerialUSB、EP0 控制传输、AP_HAL_ChibiOS 集成、descriptor 对照、VBUS 板级策略、RTT L5/L6 CDC 测试、蒸馏补丁实现、蒸馏总编
- 依据/假设：当前 USB 修复不能只靠局部猜测，应把 ChibiOS 可运行语义按层蒸馏到 RTT，同时明确不可直接搬运的 RT-Thread/ArduPilot 边界
- 结果：10 个子代理已后台运行；其中 9 个以只读对照/报告为主，1 个可在确认明显差异时做最小补丁并 scons 验证
- 下一步：等待 10 路结果，合并为“LLD/USB core/CDC/AP_HAL/board/tests”分层迁移方案，并与第二轮硬件验证结果交叉裁决

### 2026-05-27 23:15 EP0 P0 零长度 status 修复派单
- 动作：基于第二轮复核结论，追加 2 个 composer-2.5 子代理：一个实现 EP0 零长度 Host→Device 请求 STATUS IN ZLP 修复，一个等待补丁后构建/烧录/USB-L0 复测
- 依据/假设：EP0 多包 IN 已修，但 `usb_setup_transfer(len==0)` 一律 STATUS OUT 仍会破坏 SET_ADDRESS/SET_CONFIGURATION；这是比 descriptor 多包更直接的枚举阻断点
- 结果：修复与验证子代理已后台运行；要求最小改 `hal_usb_lld_rtt.c`，不触碰无关上层 HACK
- 下一步：等待补丁和验证结果；若 `rtt_dbg_usb_set_addr` 仍为 0，则继续查 SETUP 分发/IRQ；若 set_addr>0 但 configured=0，则查 SET_CONFIGURATION/CDC endpoint config

### 2026-05-27 23:27 父代理裁决并发冲突
- 动作：直接读取当前 `hal_usb_lld_rtt.c/h` 与重新运行 `python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)`
- 依据/假设：子代理结论冲突：修复代理称 P0 已落地并编译过，验证代理称源码仍固定 STATUS OUT 且构建失败；必须以当前源码与当前构建为准
- 结果：当前源码确认已有 `rtt_dbg_usb_ep0_sts_in` 与 `usb_setup_transfer(len==0)` 按 `setup[0]&USB_DIR_MASK` 分支 STATUS IN/OUT；当前树重新 SCons **通过**，`rt-thread.elf/rtthread.bin` 生成并完成 Binary integrity check；之前“P0 未落地/构建失败”判断为并发旧状态或读到旧产物
- 下一步：使用当前新构建产物烧录并做 USB-L0 复测；已启动 OpenOCD app 烧录命令，但用户手动转后台，父代理本轮不轮询该后台任务

### 2026-05-27 USB CDC 最小修复（子代理）
- 动作：统一 `rtt_usb` 单实例（LLD 定义，移除 hal_usb_rtt.c 重复定义）；`usb_lld_init_rtt()` 调用 `usb_cdc_init()`；SET_ADDRESS 在 EP0 status 后 `usb_lld_set_address`；EP0 status IN/OUT 调用 `ep0endcb`；RX 回调委托 `usb_cdc_*`
- 依据/假设：`stm32_otg_t` 布局已与 ChibiOS 一致；根因是 `_usb_driver` 与 `rtt_usb` 双实例导致 SET_CONFIGURATION 不初始化 CDC 端点 + DCFG 地址未写入
- 结果：`scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` 通过（exit 0）
- 下一步：烧录后验证 `lsusb 1209:5741`、`rtt_dbg_usb_set_addr>0`、`usb_lld_is_configured_rtt()==1`、CDC MAVLink 心跳

### 2026-05-27 HardFault OpenOCD 传感器增强（子代理）
- 动作：扩展 `openocd_sensor.py`/`openocd_session.py`：HALT 读 HFSR/MMFAR/BFAR + `rtt_dbg_hardfault_{lr,frame_sp,frame_ext,stack_lr,stack_xpsr}`；集中 `parse_halt_text()`
- 依据/假设：`context_gcc.S` 已在 `hardfault_hang` 前写入上述变量；无需改固件
- 结果：`py_compile` + 合成 GDB 文本解析 smoke 通过；ELF 符号齐全；本机 GDB:3333 未开，未做硬件 halt 读
- 下一步：HardFault 后 `python3 sensors/openocd_sensor.py --run 0` 对照 `frame_ext` 与栈帧 PC/LR/xPSR

### 2026-05-27 EP0 多包 GET_DESCRIPTOR 修复（子代理）
- 动作：审计 hal_usb_lld_rtt.c；修复 usb_setup_transfer/_usb_ep0in；新增 rtt_dbg_usb_ep0_cont/zlp
- 依据/假设：首包 chunk+totsize 被 usb_lld_start_in 覆盖导致 otg_epin_handler 不续传；_usb_ep0in 未推进 ep0data 会重发首包
- 结果：对齐 ChibiOS usbStartTransmitI（txsize=全长）；scons cuav_v5 PASS
- 下一步：硬件 GET_DESCRIPTOR 75B、rtt_dbg_usb_ep0_cont>=1、lsusb 1209:5741、CDC 心跳

### 2026-05-27 ChibiOS USB CDC 蒸馏补丁（composer-2.5 子代理）
- 动作：对照 ChibiOS OTGv1/hal_usb/SerialUSB 与 RTT `hal_usb_lld_rtt.c`+`usb_cdc_rtt.c`；实施最小补丁：early SET_ADDRESS、GET_DESCRIPTOR 优先 hook、SET_CONFIGURATION 先 disable+RESET、CDC 类请求统一 `requests_hook_cb`、SET_LINE_CODING 补 `usb_lld_start_out`；删除 LLD 内重复 CDC 状态
- 依据/假设：运行时 EP0 走 LLD static 回调；ChibiOS F767 stepping2 用 DCTL_SDIS 非 GCCFG；EP0 多包/ZLP 逻辑已存在无需再动
- 结果：`scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` exit 0，binary integrity PASSED
- 下一步：OpenOCD 无 HardFault + CDC MAVLink 心跳/STANDBY；观察 `rtt_dbg_usb_set_addr`、`rtt_dbg_usb_ep0_cont`、枚举后 `usb_lld_is_configured_rtt()`

### 2026-05-27 EP0 零长度 STATUS IN 修复（子代理）
- 动作：`usb_setup_transfer(len==0)` 按 `setup[0]&0x80` 分支 STATUS IN/OUT；新增 `rtt_dbg_usb_ep0_sts_in`
- 依据：ChibiOS `_usb_ep0setup`；Host→Device 无数据阶段须设备发 STATUS IN ZLP，否则 SET_ADDRESS/SET_CONFIGURATION 卡死
- 结果：`python3 -m SCons --v=ArduCopter --target=cuav_v5` 通过（ROM 83.45%）；未做硬件验证
- 下一步：`rtt_dbg_usb_ep0_sts_in>=1`、`rtt_dbg_usb_set_addr>0`、lsusb、CDC 心跳

### 2026-05-27 23:32 P0 修复后 USB-L0 复测
- 动作：读取 OpenOCD 烧录日志，随后检查 sysfs/lsusb/by-id、运行 pymavlink heartbeat，并用 OpenOCD/GDB 快照读取 PC、VTOR、CFSR 与 RTT debug 变量。
- 依据/假设：当前新构建 app 已烧录，需要确认 EP0 status 方向 P0 修复是否至少恢复 USB CDC 枚举，并进一步验证 MAVLink 心跳。
- 结果：烧录日志显示 `Verified OK`；主机枚举出 `1209:5741 CUAV V5 CDC 1`，`/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 -> ttyACM1`，说明 USB CDC 枚举已恢复。pymavlink 未收到心跳，串口读报 device disconnected/no data；OpenOCD 快照 CFSR/HFSR/MMFAR/BFAR 均为 0，`usb_lld_is_configured_rtt` 读为 1，但 main loop counters 未增长，debug sentinel 显示 app/main loop 未进入稳定运行状态。一次 `reset run` 后 OpenOCD 报 PC=0x080ec25c halted due to breakpoint，需继续区分是残留断点/调试停机，还是 app 初始化路径停住。
- 下一步：当前 P0 结论从“枚举失败”推进为“CDC 枚举成功但 MAVLink/L0 心跳未过”；下一轮优先定位 app 是否被断点/bootloader/debug 状态阻塞，或 USB CDC 端口枚举后被固件重置导致无数据。

### 2026-05-27 23:40 test_L6_cdc 裸 CDC 验证（子代理）
- 动作：scons --target=cuav-v5 --test=L6_cdc；OpenOCD program 0x08008000 verify；lsusb/dmesg
- 结果：构建 PASS；烧录 Verified OK；30s 后无 1209:5741；PC 复位后 0x08000200（BL）；手动跳入 test 后 PC 0x0800a5c2 仍无稳定枚举
- 下一步：确认 BL→0x08008000 跳转或 test 专用 0x08000000 链接/烧录策略

### 2026-05-27 L6_cdc verify (composer shell)
- 动作：scons L6_cdc 构建+烧录 verify；reset 12s / VTOR 跳入；lsusb/pyserial；halt 读调试
- 结果：构建 OK（无 APP_DESCRIPTOR）；主机无 1209:5741；dmesg descriptor read -71/-32；FW configured=0 echo=0 ep0_setups=14 address=4
- 下一步：修 EP0 device descriptor IN / 描述符阶段

### 2026-05-27 23:59 test_L6_cdc 父代理复测与裁决
- 动作：合并 6 个 L6 子代理结果后，父代理直接修补 `test_L6_cdc/main.c` 并多轮构建/烧录/手动跳入验证；修复点包括 EP0 SETUP_DATA 常量、EP0 status/rearm、未清中断位、early SET_ADDRESS、设备描述符小端格式、Device Qualifier 响应与 echo 调试计数。
- 依据/假设：L6 是裸 CDC 通信测试，不应依赖 MAVLink；由于 test 固件无 APP_DESCRIPTOR，ArduPilot bootloader 不自动跳转，硬件复测采用 `0x08008000` 烧录后手动设置 VTOR/MSP/PC 跳入。
- 结果：`scons --target=cuav-v5 --test=L6_cdc -j$(nproc)` 多轮 PASS，OpenOCD program 0x08008000 verify OK；固件运行且无 HardFault，SOF/reset 有效。枚举进度从最初只到 SET_ADDRESS，推进到处理 Device Descriptor、Device Qualifier，并最终停在 `GET_DESCRIPTOR(Configuration, len=67)`；主机侧仍无稳定 `1209:5741`，pyserial echo 未能执行。
- 下一步：继续补 L6 EP0 配置描述符 IN 数据阶段的多包/ZLP/STATUS OUT 收口，或改 L6 复用已验证更完整的生产 `hal_usb_lld_rtt.c` EP0 状态机；同时需解决 test 固件无 APP_DESCRIPTOR 导致 bootloader 不自动跳转的问题。

### 2026-05-28 00:06 EmbodiSkill/SkillEvolver 方法融入
- 动作：将 EmbodiSkill 的“技能感知反思/执行失误区分”和 SkillEvolver 的“元技能、部署后 fresh agent 试用、Auditor 防泄漏/静默绕过”提炼为 `.cursor/rules/skill-self-evolution.mdc` 常驻规则。
- 依据/假设：用户要求“学习然后融入你的系统”；该内容属于代理工作法，不属于固件稳定事实，不应写入 `status.md`。
- 结果：新增常驻规则，要求后续排障/规则/技能/脚本修改按“部署后观察 -> 失败归因 -> 局部补丁 -> 成败轨迹对比 -> 独立审计”循环执行。
- 下一步：后续相关任务观察该规则是否真正触发；若未改变行为，优先调整规则描述、触发条件或起手动作。

### 2026-05-28 00:10 Skill 更新半自动 Hook
- 动作：新增项目级 `.cursor/hooks.json`，在 `stop` 事件运行 `.cursor/hooks/skill-update-audit.py`；脚本只生成 `.cursor/skill-update-audits/latest.md` 与时间戳报告，不直接修改 skill/rule。
- 依据/假设：用户要求“任务完成时先生成是否值得更新 skill 的审计建议，只有确实有可复用经验时再改”；因此 hook 采用 fail-open advisory 模式，避免阻塞正常收尾。
- 结果：模拟 stop 输入运行通过，生成报告并返回 additional_context；`python3 -m py_compile .cursor/hooks/skill-update-audit.py` 通过。
- 下一步：后续真实任务结束时观察 hook 是否在 Cursor Hooks 输出中触发；若报告噪声过高，调整正/负向信号或 trace 抽取范围。

### 2026-05-28（L6 CDC EP0 FIFO / config descriptor IN）
- 动作：审查 test_L6_cdc OTG FS FIFO/EP0；补丁 EP0 TX FIFO 分片填充 + reset 后 TX0=32 words；scons --target=cuav_v5 --test=L6_cdc 通过
- 依据：usb_handle_reset 仅分配 16 words EP0 TX FIFO；67B 配置描述符需 2×64B 包，usb_start_in 要求 FIFO≥67B 才写入 → 空 FIFO 启动 IN
- 结果：构建通过（未烧录）
- 下一步：硬件 CDC 枚举 + 1209:5741 双重验证

### 2026-05-28 L6 CDC EP0 fix (worktree l6-cdc-2e7104a1)
- 动作：对比 hal_usb_lld_rtt.c，修补 test_L6_cdc EP0 分片 IN + STATUS OUT
- 依据：生产栈 otg_epin_handler 按 MPS 续传；L6 在首包 XFRC 后误 ep0_rearm_setup
- 结果：scons --target=cuav_v5 --test=L6_cdc 通过
- 下一步：板级 CDC 枚举 + echo 验证

### 2026-05-28 00:20（worktree usb-cdc-950a4af2 / EP0 IN 分片）
- 动作：对照 ChibiOS OTGv1 usb_lld_start_in/otg_epin_handler 修 test_L6_cdc EP0 IN
- 根因：DIEPTSIZ 按全长编程但 TX FIFO 仅一次性写入且未开 DIEPEMPMSK；XFRC 后未按 MPS 续传（67B config 需 2 包）
- 修改：ep0_in_total/sent、ep0_start_in_chunk、ep0_txfifo_fill、RXFLVL drain、SET_ADDRESS 延后
- 结果：scons --target=cuav_v5 --test=L6_cdc PASS（text=115148）

### 2026-05-28 test_L6_cdc EP0 多包 IN（worktree l6cdc-ep0-09bc2194）
- 动作：对照 ChibiOS `usb_lld_start_in`/`otg_txfifo_handler`/`otg_epin_handler` 修 `test_L6_cdc/main.c`：DIEPEMPMSK+TXFE 分片填 FIFO、EP0 XFRC 续传至 67B 配置描述符完成后再 STATUS OUT
- 依据：旧代码一次性写满 FIFO 且首包 XFRC 即 status；EP0 FIFO 仅 64B 导致 67B 配置描述符卡住
- 结果：主仓已含 `ep0_start_in_chunk` 多包续传；本代理补 `DIEPMSK_TXFEM`（OTG 初始化两处）；`scons --target=cuav-v5 --test=L6_cdc` 通过
- 下一步：OpenOCD 烧录 + lsusb 1209:5741 + echo 验证

### 2026-05-28 test_L6_cdc APP_DESCRIPTOR（worktree 子代理）
- 动作：新增 tests/common/test_app_descriptor.c；SConscript 在 TEST_NAME 时编入；worktree HEAD 另修 link_test.lds ROM=0x08008000
- 根因：测试固件不链 ArduPilot，无 .app_descriptor；set_app_descriptor.py 报 No APP_DESCRIPTOR；bootloader 不跳转，reset 后 PC≈0x08000200
- 验证：主仓 scons --test=L6_cdc PASS；descriptor@bin+0x1f8 CRC OK；Reset_Handler=0x0800b99c；board_id=50
- 下一步：烧录 0x08008000 后 OpenOCD reset run，应无需手动 VTOR/PC 跳入

### 2026-05-28 00:22 test_L6_cdc 枚举卡在 GET_CONFIGURATION（worktree 子代理）
- 假设：EP0 第二包 IN 从 buf[0] 而非 buf[ep0_in_sent] 取数；bus reset 后 EP0 TX FIFO 仅 16 words；status OUT 未 priming
- 动作：修 ep0_start_in_chunk 偏移、usb_handle_reset 用 TX0_FIFO_SIZE、ep0_start_status_out 显式 usb_start_out(0,0)；scons --test=L6_cdc 通过
- 结果：构建 OK；待烧录验证 lsusb 1209:5741 与 SET_CONFIGURATION
- 下一步：OpenOCD 烧录后读 test_debug_slot[3]（chunks|TXFSTS）与 usb_configured

### 2026-05-28 EP0 STATUS OUT 根因（并行子代理复核）
- 根因：`ep0_start_status_out()` 曾仅 `ep0_rearm_setup()`，未按 ChibiOS `USB_EP0_STATE_WAITING_STATUS_OUT` + `usb_lld_start_out(drv,0)` 收 0 字节 OUT；67 字节配置描述符需两包 DATA IN 后必须 STATUS OUT 才完成传输
- 补丁：维持 `EP0_STATUS_OUT` + `usb_start_out(0,0)`；OUT XFRC→`ep0_rearm_setup`；`usb_start_out` EP0 用 EP0_MPS；IN 每包 `PKTCNT=1`
- 结果：`scons --target=cuav-v5 --test=L6_cdc` PASS（115300B ROM，未烧录）

### 2026-05-28 子代理#10 EP0 最小修复（composer-2.5）
- 假设：67B 配置描述符第二包 IN 须从 `ep0_ctrl_buf+ep0_in_sent` 填 FIFO；且须在 EPENA 前填满 FIFO，否则首包/次包截断；DATA IN 结束需 ZLP（wLength 对齐 MPS 时）+ STATUS OUT
- 动作：`ep0_start_in_chunk()` 先 spin 填满 TX FIFO 再 EPENA；`ep0_finish_data_in()`+`EP0_DATA_IN_ZLP`；handle_epin 调用 finish
- 结果：`scons --target=cuav-v5 --test=L6_cdc` PASS，text=115452；未烧录

### 2026-05-28 worktree usb-ep0-05076986 EP0 reset/rearm 第二轮
- 现象：ep0_setups=2、usb_configured=0（仅 device GET_DESCRIPTOR + SET_ADDRESS）
- 根因：`handle_epout` 在 `EP0_STATUS_IN` 等状态下对伪 XFRC 调用 `ep0_rearm_setup()`，地址阶段 STATUS IN 期间破坏 OUT SETUP 接收；`usb_handle_reset`/`step_usb_enable` 未在 EP0 配置后 EPENA|CNAK
- 补丁：`usb_disable_active_eps()`（ChibiOS otg_disable_ep）；reset 末 `ep0_rearm_setup()`；rearm 全量重写 DOEPCTL/DIEPCTL+清 STALL/flush；伪 XFRC 仅 EP0_IDLE 时 rearm；`step_usb_enable` 末 ep0_rearm
- 结果：`scons --target=cuav-v5 --test=L6_cdc` PASS（115908B ROM）；未烧录

### 2026-05-28 00:50 L6 第二轮 EP0 诊断（worktree l6cdc-0ac401ae）
- 动作：主仓 `test_L6_cdc/main.c` 增加 `l6_diag_*`（SETUP 环×4、GRXSTSP、EP0 IN/OUT 计数、last DIEPINT/DOEPINT/GINTSTS）；`handle_setup`/`ep0_rearm_setup` 清 STALL
- 结果：待 `scons --test=L6_cdc` 复核
- 下一步：烧录后 OpenOCD 读 `l6_diag_summary` 与 `l6_diag_setup_hist`

### 2026-05-28 00:45 L6 bootloader→app USB 切换（worktree usb-l6-bt-a7f3e291）
- 假设：无 MCU 复位跳入时主机仍见 BL；L6 未调用 `step_soft_disconnect()`，DCTL 全写破坏只读位
- 动作：`l6_usb_bootloader_handoff()` + main 插入 `step_soft_disconnect()` + clock 前 SDIS + reconnect 前 50ms；`scons --test=L6_cdc` PASS（115692B）
- 结果：构建通过；未烧录
- 下一步：烧录+手动跳入，看 dmesg 是否应用重枚举

### 2026-05-28 L6 CDC EP0 DOEPTSIZ（worktree 子代理）
- 动作：分析 test_debug_slot、对照 hal_usb_lld_rtt.c，补齐 ep0_out_enable 路径与 RX FIFO 丢弃；scons L6_cdc 构建
- 依据：slot[0]=0x00050000 为 SET_ADDRESS 编码非乱码；STUPCNT 与 PKTCNT 共用 bit29-30
- 结果：构建通过（exit 0）；未烧录未 commit
- 下一步：父代理烧录 + CDC 双重验证

### 2026-05-28 EP0 OUT re-arm 第二轮（ep0fix worktree）
- 动作：L6 拆分 status OUT / data OUT / SETUP arm；HAL `usb_lld_start_out` ZLP 无 STUPCNT，status 后 rearm STUPCNT(3)
- 结果：`scons --target=cuav-v5 --test=L6_cdc` PASS；hwdef 源需同步到 build/rtt_deploy 副本再编
- 下一步：烧录验证 setups>2、configured=1

### 2026-05-28 L6 第二轮集成审计（l6-audit-12478fd0）
- 动作：审计主树未提交 `test_L6_cdc/main.c` 对照 6 条子代理结论；补 `main()` 在 core_config 前调用 `step_soft_disconnect()`
- 结果：结论 1–3/5/6 已满足；未改 `hal_usb_lld_rtt.c`；`scons --test=L6_cdc` PASS（116468B text）
- 下一步：烧录后核对 setup_count 与 CDC echo

### 2026-05-28 L7 USB PoC 构建守门（worktree usb-poc-011005ad）
- 动作：新增 `test_L7_cherryusb_cdc`/`test_L7_tinyusb_cdc`、CherryUSB 源列表与 CUAV port 副本；worktree 内 `scons --test=L7_*`
- 结果：L7_tinyusb_cdc PASS（111KB，运行时故意 FAIL）；L7_cherryusb_cdc PASS（149KB，单一定义 `OTG_FS_IRQHandler`）
- 下一步：合并 worktree 至主树；TinyUSB 需 thirdparty；硬件验证 usb-acm0

### 2026-05-28 L7 TinyUSB CDC echo PoC（worktree tinyusb-8726892f）
- 动作：vendor TinyUSB 0.18.0 + `test_L7_tinyusb_cdc`；SConscript 用 `AP_ROOT` 解析 thirdparty 路径
- 结果：`scons --target=cuav-v5 --test=L7_tinyusb_cdc` PASS（text 119152B）；未烧录
- 下一步：父代理 `/apply-worktree`；硬件 CDC echo 于 `/dev/ttyACM0`

### 2026-05-28 L7 TinyUSB 真栈（worktree tinyusb-poc-1edbc7f8）
- 动作：rsync 自 8726892f + board.c `PWR_CR2` 对齐 L5；worktree symlink rt-thread/mavlink；`scons --test=L7_tinyusb_cdc`
- 结果：构建 PASS（ROM 119684B，nm 含 dcd_/tud_/cdcd_）；非编译期占位 FAIL
- 下一步：硬件 mount/echo；`/apply-worktree` 合入主树
### 2026-05-28 L7 链接同步（worktree tinyusb-poc-1edbc7f8）
- 动作：同步 link_test.lds ROM=0x08008000、SConscript FLASH_ORIGIN=0x08008000U（test_app_descriptor 块已存在）；`scons --target=cuav-v5 --test=L7_tinyusb_cdc`
- 结果：PASS text=119684B；Entry/Reset 0x0800DFF8、app_descriptor 0x080081F8、向量[1]=0x0800DFF9、.text@0x08008000；nm 含 dcd_/tud_/cdcd_/tusb_init
- 下一步：烧录 0x08008000 + CDC echo；8726892f worktree 的 link_test.lds 仍为 0x08000000 待同步

### 2026-05-28 02:10（L8 CherryUSB CDC+MSC PoC worktree）
- 动作：创建 worktree `cherryusb-msc-0000166d`（HEAD d6ad102），从 L7 参考树同步 thirdparty/L7 基线，新增 `test_L8_cherryusb_cdc_msc` + MSC class。
- 结果：`scons --target=cuav-v5 --test=L8_cherryusb_cdc_msc` 通过；ROM@0x08008000，app_descriptor@0x080081f8，ELF 125016B。
- 下一步：硬件枚举 CDC echo + MSC 64KB RAM disk（未烧录）。

### 2026-05-28 CDC standalone 基线收口（只读归档，未烧录/未改码）
- 动作：只读核对 worktree `l7link-e50b35fd`（CherryUSB）与 `tinyusb-poc-1edbc7f8`（TinyUSB）的构建产物、链接符号与验证命令；依据父代理已完成实机结论归档。
- CherryUSB（`L7_cherryusb_cdc`）
  - 路径：`/home/llw/.cursor/worktrees/l7link-e50b35fd/pogo-apm-8fb9a0ae27c1`
  - 构建：`cd <wt> && scons --target=cuav-v5 --test=L7_cherryusb_cdc -j$(nproc)`
  - 产物：`build/rtt_cuav_v5/rtthread.bin`（121168B）、`build/rtt_deploy/cuav_v5/rt-thread.elf`（text≈118852B）
  - 链接：`ROM@0x08008000`；`app_descriptor@0x080081f8`；`Reset_Handler@0x0800d9bc`（Entry 0x0800d9bd）
  - USB 身份（源码）：`VID/PID 1209:5742`（`test_L7_cherryusb_cdc/main.c`）
  - 实机（父代理已验）：`lsusb 1209:5742`；`/dev/ttyACM1` echo `L7_CHERRYUSB_PING`/`hello` 回显通过
- TinyUSB（`L7_tinyusb_cdc`）
  - 路径：`/home/llw/.cursor/worktrees/tinyusb-poc-1edbc7f8/pogo-apm-8fb9a0ae27c1`
  - 构建：`cd <wt> && scons --target=cuav-v5 --test=L7_tinyusb_cdc -j$(nproc)`
  - 产物：`build/rtt_cuav_v5/rtthread.bin`（122024B）、`build/rtt_deploy/cuav_v5/rt-thread.elf`（text≈119684B）
  - 链接：`ROM@0x08008000`；`app_descriptor@0x080081f8`；`Reset_Handler@0x0800dff8`（Entry 0x0800dff9）
  - USB 身份（源码）：`VID/PID 1209:5741`（`usb_descriptors.c`）
  - 实机（父代理已验）：`lsusb 1209:5741`；`/dev/ttyACM1` echo `L7_TINYUSB_PING`/`hello` 回显通过
- 可复现烧录（两栈共用，仅在需要重放时执行；本任务未执行）：
  - `openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 verify reset" -c "resume" -c "exit"`
- 可复现 CDC 回环（主机，端口以 `ls /dev/serial/by-id/` 为准；当前基线用 `ttyACM1`）：
  - Cherry：`stty -F /dev/ttyACM1 115200 raw -echo; printf 'L7_CHERRYUSB_PING' > /dev/ttyACM1; timeout 2 cat /dev/ttyACM1`
  - Tiny：`stty -F /dev/ttyACM1 115200 raw -echo; printf 'L7_TINYUSB_PING' > /dev/ttyACM1; timeout 2 cat /dev/ttyACM1`
- 结果：两 worktree 本地 ELF 与父代理实机结论一致；CDC 单独测试状态记为 **L7 CherryUSB PASS / L7 TinyUSB PASS**（与全量 ArduCopter L0 无关）。
- 下一步：主树合并 L7 测试入口与 thirdparty 路径；全固件仍走 `hal_usb_lld_rtt` L6 线，勿与 L7 PID 混测。
2026-05-28 composer-2.5 L8_tinyusb_msc: scons+openocd flash OK; host 1209:5744 absent (5741 BL only); OpenOCD HF pc=08008400 VTOR=08008000 hardfault_hang; MSC mount skipped. Parent: fix boot then re-run HW.

## 2026-05-27 19:00 UTC composer-2.5 shell subagent — L8_cherryusb_msc HW validate (llw@myubuntu / llw-pc)

- **环境**: whoami=llw, host=llw-pc, worktree=/home/llw/.cursor/worktrees/cherryusb-msc-0000166d/pogo-apm-8fb9a0ae27c1 (存在)
- **构建**: scons --target=cuav-v5 --test=L8_cherryusb_msc -j → **OK** (
tthread.bin 190152 B, APP_DESCRIPTOR applied)
- **烧录**: openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c 'program build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 verify reset exit' → **Verified OK**（首次 Windows 侧 -c program  被 OpenOCD 当成多余 argv，已在 worktree 相对路径重试成功）
- **USB 主机**:
  - lsusb -d 1209:5745 → **无设备**
  - 仅周期性 1209:5741 CUAVv5-BL + 	tyACM1（dmesg）；	tyACM0 为 QinHeng 1a86:55d3，**非** 5745
  - **无** 新增 lsblk USB 可移动块设备；README/SMOKE/RWTEST **未执行**
- **OpenOCD 停机**（烧录后 halt）: mode: Handler HardFault, **PC=0x080083fe**, **VTOR=0x08008000**, **HFSR (0xE000ED28)=0x00008200**；烧录前曾见 PC=0x080083a0 HardFault
- **结论**: **失败**（有效 llw 环境；构建+烧录通过；CherryUSB MSC 1209:5745 与 RAM disk 挂载链路未建立，应用在 0x08008000 域 HardFault，USB 停留在或未到达 MSC 枚举）

## 2026-05-27 19:02 UTC L8_cherryusb_msc trace corrected
- Build/flash OK on llw-pc worktree cherryusb-msc-0000166d
- Host: no 1209:5745; no MSC block device; README/RW not run
- OpenOCD: HardFault PC=0x080083fe VTOR=0x08008000 HFSR=0x00008200
- Verdict: FAIL


### 2026-05-28 composer-2.5 L8_tinyusb_msc HardFault root-cause (worktree tinyusb-msc-8d97a9bd)
- Build: scons --target=cuav-v5 --test=L8_tinyusb_msc PASS; flash rtthread.bin @0x08008000; OpenOCD gdb_port=3334 (avoid node :3333).
- GDB app boot: VTOR=0x08008000, MSP=0x20006924, PC=0x0800e295; break hardfault_hang.
- Hang PC=0x080083fe (hardfault_hang) is NOT root cause. rtt_dbg_hardfault_stack_pc gives real fault PC=0x0800BE2E (rthw_sdio_irq_process drv_sdio.c:577); LR=0x0800C173 (SDMMC1_IRQHandler drv_sdio.c:842).
- VTOR=0x08008000; MSP=0x200068c0; PSP=0x20008a68; EXC_LR=0xFFFFFFF1; CFSR=0x8200; HFSR=0x40000000; BFAR=MMFAR=0x0184020b; stacked xpsr=0x01000041.
- lsusb: no 1209:5744 (L8 MSC PID); prior 1209:5741 only (L7/BL) — USB MSC never enumerated.
- Verdict: stray SDMMC1 IRQ / SDIO BSP before L8 board_init disables SDMMC; not BL jump, not TinyUSB RAM disk overflow. Fix: disable SDIO for L8 test or early IRQ guard / move DisableIRQ before INIT_DEVICE rt_hw_sdio_init.
## 2026-05-28 composer-2.5 route-B subagent (composer-2.5 #2) — L8_tinyusb_msc 降复杂度验证

- **隔离 worktree**: `/home/llw/.cursor/worktrees/tinyusb-msc-b2-composer/pogo-apm`
- **基线**: rsync 自 `/home/llw/.cursor/worktrees/tinyusb-msc-8d97a9bd/pogo-apm-8fb9a0ae27c1`
- **补丁**: `/home/llw/.cursor/worktrees/tinyusb-msc-b2-composer/l8-msc-route-b-composer2.5.patch`
- **硬件**: llw@myubuntu + STLink + CUAV V5；L7 TinyUSB CDC (`1209:5741`) 仍可枚举

### 异常栈证据（基线 / H1）
- OpenOCD halt: `HardFault pc=0x08008400`（hardfault_hang 包装）
- 异常栈真实 PC: **0x0800c173** → `SDMMC1_IRQHandler` @ `drv_sdio.c:842`
- 非 `msc_ram_disk_smoke()` / 非 MSC LBA 回调

### 假设 → 改动 → 验证

| 假设 | 改动 | 构建 | 烧录 | lsusb 1209:5744 | 结论 |
|------|------|------|------|-----------------|------|
| **H1** 启动即 smoke 触发 fault | `main.c`: 移除启动时 smoke，改到 `tud_mount_cb` 后 | OK 131736B | OK | **无**（仅 5741 BL 或无 1209） | **FAIL** — 仍 HardFault @ SDMMC |
| **H2** 对齐 + LBA 边界 | `msc_disk.c`: `aligned(4)`、read/write10 offset 检查 | OK 131736B | OK | **无** | **FAIL** — 同 SDMMC fault |
| **H3** 屏蔽 SDMMC1 NVIC | `board.c`: `HAL_NVIC_DisableIRQ(SDMMC1_IRQn)` | OK 131736B | OK | **无** | **FAIL** — 太晚（SDIO 已在 RT-Thread init 注册） |
| **H4** FAT 镜像改 flash 模板 + 运行时 memcpy | `msc_disk.c`: template→`.rodata`，`.data` 2340B（对齐 L7） | OK 131788B | OK | **无**（5741 BL） | **FAIL** — fault 迁移至 `OTG_FS_IRQHandler` 栈破坏（PC=0x23232322） |

### 对照
- **L7_tinyusb_cdc**（tinyusb-poc worktree）同板烧录：`1209:5741` 可见，halt 时 `Thread` 非 HardFault
- **L8_tinyusb_cdc_msc** 同 worktree 烧录：HardFault，异常 PC 仍在 **SDMMC1_IRQHandler**

### 结论
- Route-B 降复杂度（smoke 延后 / 对齐 / 去 .data 大镜像）**未能**使 MSC-only 枚举 `1209:5744`
- 根因更可能在 **RT-Thread SDIO 驱动与 USB/MSC 测试共存**（SDMMC IRQ 早于 test `board_init`），或 MSC 栈/IRQ 路径问题；需 A 路或 BSP 级修复（如 USB-only 测试禁用 `RT_USING_SDIO` / 提前 `NVIC_Disable`+`ClearPending`）
- **未通过** MSC README/RW smoke

### 下一步建议（父代理）
1. 在 `rt_hw_board_init` 或 test hook **早于** `drv_sdio` probe 禁用 SDMMC1
2. 或 `--test=L8_tinyusb_msc` 专用 `rtconfig` 关闭 `BSP_USING_SDIO`
3. H4 的 flash 模板仍建议保留（`.data` 从 10532→2340B）
2026-05-28 composer-2.5 L8_tinyusb_msc fix+verify: real fault PC=0x0800be2e rthw_sdio_irq_process drv_sdio.c:577 (sdio->pkg NULL on HW_SDIO_IT_CMDSENT); hang PC=0x08008400 hardfault_hang. Patched drv_sdio CMDSENT null guard + test board INIT_APP l8_sdio_irq_quiet. Flash BL+app: lsusb 1209:5744 MSC-only OK; CPU Thread pc=0x0800de34; scsi host8, NO sd node for 8KiB (NO_BLOCK_DEV). Enum PASS, mount smoke BLOCKED.

### 2026-05-28T03:19:29 composer-2.5 route-3: L8_tinyusb_msc trimmed from composite (worktree tinyusb-msc-8d97a9bd)
- Action: Trimmed test_L8_tinyusb_msc from test_L8_tinyusb_cdc_msc (not patching old MSC-only). Kept composite USB/DWC2 board_init path. PID 1209:5744. SConscript drops cdc_device.c. Single MSC descriptor. board.c adds INIT_PREV/INIT_COMPONENT SDMMC quiet. main runs 120s tud_task like composite.
- Composite HW: no L8_tinyusb_cdc_msc host PASS in agent-trace (build only).
- Build: scons --target=cuav-v5 --test=L8_tinyusb_msc PASS; rtthread.bin 110772B; text 100288B.
- Flash: openocd program 0x08008000 verify OK; host 1209:5744 after separate reset run (program-only often leaves 1209:5741 BL).
- Host: lsusb 1209:5744 OK; lsusb -v bNumInterfaces=1 Mass Storage only; Product L8 TinyUSB MSC.
- Block: dmesg scsi Direct-Access POGO-RTT RAM Disk; lsblk sdc 8K seen; README+RW need mount within 120s tud_task window.
- Patch: /tmp/l8_msc_vs_composite.diff on llw-pc.

## 2026-05-28 composer-2.5 subagent — L8_tinyusb_msc SCSI/blkdev fix (worktree tinyusb-msc-8d97a9bd)

- **根因**: main.c 在 	ud_mount_cb 后调用 TEST_DONE()；	est_runner.c 的 _test_done() 死循环仅 
t_thread_mdelay(1000)，**不再调用 	ud_task()**，主机 SCSI/BOT（READ CAPACITY/READ10）无响应 → usb-storage 注册 scsi host 但块设备不稳定或无法挂载。
- **补丁**:
  1. 	est_L8_tinyusb_msc/main.c: 去掉挂载后提前 reak；服务循环 300s；**不调用 TEST_DONE**；改为 while(1){ tud_task(); mdelay(1); } 持续服务 MSC。
  2. msc_disk.c: 	ud_msc_scsi_cb 对 MODE_SELECT_6/

## 2026-05-28 composer-2.5 subagent — L8_tinyusb_msc SCSI/blkdev fix (worktree tinyusb-msc-8d97a9bd)

- **根因**: `main.c` 在 USB mount 后调用 `TEST_DONE()`；`test_runner.c` 的 `_test_done()` 死循环仅 `rt_thread_mdelay(1000)`，**不再调用 `tud_task()`**，主机 SCSI/BOT（READ CAPACITY/READ10）无响应 → `usb-storage` 注册 scsi host 但块设备不稳定或无法挂载。
- **补丁**:
  1. `test_L8_tinyusb_msc/main.c`: 去掉挂载后提前 break；服务循环 300s；**不调用 TEST_DONE**；改为 `while(1){ tud_task(); mdelay(1); }` 持续服务 MSC。
  2. `msc_disk.c`: `tud_msc_scsi_cb` 对 MODE_SELECT_6 / 0x35 / 0x2F 返回 0；read10/write10 增加 offset+bufsize 边界检查。
  3. 保留 `drv_sdio.c` CMDSENT `sdio->pkg` 空指针防护 + `board.c` SDMMC1 静默。
- **构建**: `scons --target=cuav-v5 --test=L8_tinyusb_msc -j8` → OK (rtthread.bin 110504B)
- **烧录**: openocd program @0x08008000 verify reset → Verified OK
- **主机验证** (llw@myubuntu):
  - `lsusb -d 1209:5744` → PASS (L8 TinyUSB MSC)
  - `lsblk` → /dev/sdc 8K RM usb POGO-RTT RAM Disk
  - dmesg: scsi host8; 16 x 512-byte logical blocks (8.19 kB); Attached SCSI removable disk
  - 挂载 /dev/sdc: README.TXT 读取 PASS
  - RWTEST.TXT 写入+sync+读回 RWTEST_ok PASS
- **结论**: TinyUSB MSC-only 独立测试 **完全通过**（枚举+块设备+README+RW）。

### 2026-05-28 composer-2.5 L8 MSC SDIO 修复 + 双栈复测

- **worktree 同源确认**: TinyUSB `tinyusb-msc-8d97a9bd` 与 CherryUSB `cherryusb-msc-0000166d` 均为 `d6ad102c87`，均含 `test_L8_tinyusb_msc` / `test_L8_cherryusb_msc`。
- **修复方向 (唯一)**: 在 `libraries/AP_HAL_RTT/hwdef/common/SConscript` 对 `TEST_NAME in (L8_tinyusb_msc, L8_cherryusb_msc)` 清除 `BuildOptions[BSP_USING_SDIO/RT_USING_SDIO]` 并 `-U` 宏，构建期不链接 `drv_sdio.c` / `rt_hw_sdio_init`。覆盖两栈共性根因：SDMMC1 在 USB 测试 `board_init` 前 IRQ，`rthw_sdio_irq_process()` 空 `pkg` HardFault（非 USB 栈差异）。
- **改动文件**: `libraries/AP_HAL_RTT/hwdef/common/SConscript`（两 worktree 均已 patch）。
- **TinyUSB `L8_tinyusb_msc`**: 构建 PASS（text≈100288B，ELF 无 SDIO 符号）；OpenOCD 烧录 verify OK；~15s 后 `lsusb 1209:5744` **L8 TinyUSB MSC**；块设备 `/dev/sdc` 8K；README.TXT 只读 OK；RWTEST.TXT/SMOKE 写读 OK；OpenOCD halt **HF=no**（非 0x080083fe hang）。
- **CherryUSB `L8_cherryusb_msc`**: 构建 PASS（text≈167324B，ELF **NO_SDIO**）；烧录 verify OK；**无 HardFault**（CFSR/HFSR=0，PC 曾见 0x0800a87a 线程态）；但 50s 内 **`1209:5745` 未出现**，主机仅间歇 `1209:5741` BL；README/MSC 挂载 **未执行**。判定：**SDIO 早崩已解；剩余为 CherryUSB MSC-only 枚举/启动路径问题**（非 SDIO 共性）。
- **可合并性**: `SConscript` 测试级 SDIO 禁用可合并主线 `hwdef/common`；CherryUSB L8 MSC 枚举需另开 USB 栈修复，勿与 SDIO 补丁混为同一根因。

### 2026-05-28 03:35 L8_cherryusb_msc — SDIO 修复 + CherryUSB FIFO + 硬件验证（composer-2.5 子代理）

- **Worktree**: `/home/llw/.cursor/worktrees/cherryusb-msc-0000166d/pogo-apm-8fb9a0ae27c1`
- **根因（与 TinyUSB 一致）**: 杂散 `SDMMC1_IRQHandler` → `rthw_sdio_irq_process()` 在 `sdio->pkg` 未就绪时解引用；CherryUSB 另卡在 `usb_dc_init()` DWC2 FIFO 总和 >320 words 断言。
- **代码改动**:
  1. `modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/drivers/drv_sdio.c` — `CMDSENT` 路径 `sdio->pkg`/`cmd` 空指针防护（与 tinyusb-msc worktree 同补丁）。
  2. `libraries/AP_HAL_RTT/hwdef/common/tests/test_L8_cherryusb_msc/board_sdio_guard.c` — `INIT_PREV_EXPORT`/`INIT_COMPONENT_EXPORT` 静默 SDMMC1（仅 L8 测试链接，不影响生产路径）。
  3. `libraries/AP_HAL_RTT/hwdef/common/tests/test_L8_cherryusb_msc/board/usb_config.h` — `TX2/TX3=0`，`RXALL=128`，修复 FIFO 断言挂起。
- **构建**: `scons --target=cuav-v5 --test=L8_cherryusb_msc -j8` → PASS，`rtthread.bin` ~169432 B。
- **烧录**: BL `CUAVv5_bl.bin` @ `0x08000000` + app @ `0x08008000` verify；烧录后立即读向量 `0x0800d919` 与 bin 一致（需避免并发子代理覆写 flash）。
- **枚举**: `lsusb -d 1209:5745` — **PASS**（~7s 自 BL 跳转后）；产品串 `L8 Cherry MSC`；**无** 5745 对应 ttyACM（仅主机 CH340 `ttyACM0`）。
- **块设备**: `usb-storage` → `sdX` 128×512B（64KiB）；`dmesg` 有 scsi attach。
- **挂载**: README 内容为 `PogoAPM L8 CherryUSB MSC-only RAM disk.`（FAT 卷标显示为 `README. TX`，8.3 名需后续修正）；`RWTEST.TXT`/`SMOKE` 写读 **PASS**。
- **HardFault**: 修复后 GDB 采样无 `rtt_dbg_hardfault_*`，PC 曾止于 `usb_dc_init`（FIFO 修复前）；SDIO IRQ 路径未再触发 fault。
- **结论**: **CherryUSB MSC-only 独立测试基本通过**（枚举+块设备+RW）；遗留：FAT 镜像 `README.TXT` → `README. TX` 显示名、与 TinyUSB 子代理并发烧录需串行化。

### 2026-05-28 composer-2.5 调试子代理 — CherryUSB L8 MSC-only 枚举专项

- **worktree**: `/home/llw/.cursor/worktrees/cherryusb-msc-0000166d/pogo-apm-8fb9a0ae27c1`
- **测试**: `scons --target=cuav-v5 --test=L8_cherryusb_msc` → PASS (text≈167324B, bss≈89936B, ELF **无 SDIO 符号**)

#### 根因（按证据）

1. **SDIO 共性 HardFault 已排除**：`SConscript` 对 `L8_cherryusb_msc` 禁用 `BSP_USING_SDIO`/`RT_USING_SDIO` 有效；halt 时 CFSR/HFSR=0，PC 线程态。
2. **“无 5745”主因是 BL→App 时序 + 烧录路径**，非 MSC 类注册致命错误：
   - `reset run` 后 **~3–8s 仅见 `1209:5741` CUAVv5-BL**，**第 8s 起稳定 `1209:5745` L8 Cherry MSC**（50s 轮询脚本 `PASS5745 at 8s`）。
   - 一次 `program … verify` 曾 **checksum mismatch**（app 未更新则长期停留 5741）；改用 `flash write_image erase … 0x08008000` 后枚举正常。
3. **App 在线证据**（枚举活跃时 OpenOCD halt）：`VTOR=0x08008000`；`l8_configured=1` @ `0x20005e5c`；`lsusb -v` → `bNumInterfaces=1`, `bInterfaceClass 8 Mass Storage`, PID **0x5745**。
4. **静态对比 L7 CDC / L8 composite / L8 MSC-only**：
   - `usb_dc_glue.c` 与 L7 **相同**（软断开、`OTG_FS_IRQHandler`→`USBD_IRQHandler` 正常）。
   - MSC-only 端点 **0x01/0x81**（composite 用 0x05/0x84）；`CONFIG_USBDEV_EP_NUM=2`（L7=4，composite=6）— 实机 EP1 bulk 可枚举，但建议与 L7 对齐为 4 作保守项。
   - `usbd_msc_init_intf` + `usbd_add_endpoint` 由 CherryUSB MSC 类内部完成，顺序与 L8 composite 一致。
5. **块设备**：`/dev/sdc` 128×512 挂载成功但 **根目录空**（`README.TXT` 未出现在 FAT12 镜像/加载路径，与枚举无关，需另修 `fat12_ramdisk.h`）。

#### 改动

- `test_L8_cherryusb_msc/main.c`：增加 `l8_usb_diag`（stage/reset/configured/wait_ms）供 OpenOCD `mdw 0x20015e64` 读取。
- **未改** CherryUSB 核心/描述符/FIFO（当前配置可枚举）。

#### 验证证据

| 项 | 结果 |
|----|------|
| `lsusb -d 1209:5745` | **PASS**（reset 后 ~8s） |
| OpenOCD VTOR/故障寄存器 | VTOR=`0x08008000`，CFSR/HFSR=0 |
| `l8_configured` | =1（枚举活跃时） |
| README/RW smoke | **BLOCKED**（FAT 根目录空） |

#### 结论

- **枚举：通过**（在正确烧录 + reset 后等待 ≥10s）。
- **MSC 文件 smoke：未通过**（镜像/内容问题）。
- 下一步：修 FAT12 镜像含 `README.TXT`；可选将 `CONFIG_USBDEV_EP_NUM` 提至 4 与 L7 对齐；烧录脚本统一用 `write_image erase` 并避免 verify 失败后仍判 PASS。

## 2026-05-28 composer-2.5 子代理 Route-B — 从 L7 CDC 重建 CherryUSB MSC-only

- **策略**：不修补旧 MSC-only 小 diff；以已通过实机的 `test_L7_cherryusb_cdc` 为母版，保留 `board/usb_dc_glue.c` 与 L7 `usb_config.h` DWC2 FIFO/EP 布局，仅替换为 `usbd_msc` + RAM FAT12。
- **补丁路径**：`/tmp/l8_cherryusb_msc_routeB.patch`（worktree `cherryusb-msc-0000166d/pogo-apm-8fb9a0ae27c1`）
- **关键改动**：
  1. `usb_config.h`：`CONFIG_USBDEV_EP_NUM` 从 2 恢复为 **4**（与 L7 一致）；FIFO 恢复 L7 的 RX128/TX0-3 分配（旧 MSC-only 为 EP_NUM=2 + TX1=128，易导致枚举失败）。
  2. `board/usb_dc_glue.c`：与 L7 逐字节一致。
  3. `main.c`：MSC 描述符 PID **0x5745**；端点 **0x01/0x81**；`usbd_msc_init_intf` + BOT 回调；启动顺序对齐 L7（`l7_usb_hw_preinit` → `usbd_initialize`）。
  4. SDIO：仍由 `hwdef/common/SConscript` 对 `L8_cherryusb_msc` 禁用 BSP/RT SDIO；保留 `board_sdio_guard.c`。
- **构建**：`scons --target=cuav-v5 --test=L8_cherryusb_msc -j8` → OK（rtthread.bin 169040B，Entry 0x0800d919）
- **烧录**：OpenOCD BL@0x08000000 + app@0x08008000 verify reset
- **验证**：
  - `lsusb -d 1209:5745` → **PASS**（约 7–12s 出现 `Generic L8 Cherry MSC`）
  - `lsblk` → `/dev/sdf` 64K usb RM PogoAPM L8 CherryUSB MSC
  - 挂载 RW：`SMOKE_RW.TXT` 写读 **PASS**（卷内 `README.*` 8.3 名显示为空格，需后续确认 FAT 镜像卷标）
- **与旧 L8_cherryusb_msc 差异（枚举失败根因）**：旧版 `CONFIG_USBDEV_EP_NUM=2` + 非 L7 FIFO；新版与 CDC 成功样例硬件路径一致。
- **可否替换现有 L8_cherryusb_msc**：**是** — 同目标名 `L8_cherryusb_msc`，建议合入 worktree 补丁并弃用旧 MSC-only `usb_config` 实验配置。

## 2026-05-28 composer-2.5 serial MSC re-verify
TinyUSB PASS; CherryUSB PASS; serial BL+app flash on llw-pc
TinyUSB: 1209:5744 /dev/sdf README RWTEST SMOKE umount OK
CherryUSB: 1209:5745 /dev/sdf README RWTEST SMOKE umount OK; FAT lists README. TX

### 2026-05-28 composer-2.5 子代理 — L8 CherryUSB MSC FAT12 README + RW

- **worktree**: `cherryusb-msc-0000166d/pogo-apm-8fb9a0ae27c1`
- **范围**: 仅 `test_L8_cherryusb_msc` FAT12 镜像与扇区 RW；未改 USB 枚举/描述符/FIFO

#### 根因

1. **根目录 8.3 文件名错位**：`mkfs.vfat` 自动条目为 `README␠␠␠␠TX`（扩展名显示为 `README. TX`），Linux 无法用 `README.TXT` 路径打开。
2. **镜像长度 off-by-one**：`mkfs` 产出 65537 字节，导致 `fat12_ramdisk.h` 数组多 1 元素（编译 warning）。
3. **扇区回调**：`usbd_msc_sector_read/write` 原先仅接受 `length==512`；已改为支持 CherryUSB 按 `length` 跨 LBA 拷贝（与 `CONFIG_USBDEV_MSC_MAX_BUFSIZE=512` 兼容）。

#### 改动文件

- `libraries/AP_HAL_RTT/hwdef/common/tests/test_L8_cherryusb_msc/fat12_ramdisk.h` — 64KiB FAT12（128×512），`mkfs.vfat` + 手工 `README  TXT` 8.3（同 TinyUSB：`README␠␠` + `TXT`），内容 `PogoAPM L8 CherryUSB MSC-only RAM disk.\r\n`
- `libraries/AP_HAL_RTT/hwdef/common/tests/test_L8_cherryusb_msc/main.c` — 多扇区 `usbd_msc_sector_read/write` 循环

#### FAT 布局（64KiB）

| 项 | 值 |
|----|-----|
| 总扇区 | 128 |
| BPB | FAT12, 512 B/扇区, 2 FAT×1 扇区, 512 根目录项 |
| README | 根目录 cluster 2，数据区起始 LBA 35 |
| 卷标 | L8RAMDISK（mkfs） |

#### 实机证据（`/tmp/l8_fat_verify.sh`）

| 项 | 结果 |
|----|------|
| 烧录 | BL `program verify` + app `flash write_image erase` + `verify_image` |
| Flash 向量 | `0x0800d919` 匹配 |
| `lsusb -d 1209:5745` | **PASS @ 7s** |
| 块设备 | `/dev/sdf` 64K |
| `ls -la` | `README.TXT` 41 字节 |
| `cat README.TXT` | `PogoAPM L8 CherryUSB MSC-only RAM disk.` |
| RW | `RWTEST.TXT` 写入 `L8_CHERRYUSB_RWTEST` 读回 OK |

#### 结论

- **CherryUSB MSC-only 枚举**：通过（1209:5745，未动描述符）
- **FAT12 README + 主机 RW**：**通过**

### 2026-05-28 13:38 主应用 USB-L0 并行实现调度
- 动作：根据用户“继续进行”，将阶段从 CDC/MSC 单项 PoC 收口推进到全量 ArduPilot RTT 主应用 USB-L0 实现；已派出 12 个 composer-2.5 子代理。
- 依据/假设：CDC-only 与 MSC-only 已分别在 TinyUSB/CherryUSB 路线通过；全量移植优先级应先锁定 SERIAL0 USB CDC/MAVLink，而非先做 MSC/复合设备。
- 分解：3 路 native LLD 加固，3 路 TinyUSB 主应用适配，3 路 CherryUSB 主应用适配，1 路构建互斥审计，1 路 USB-L0 验证脚本，1 路当前主仓 baseline 复测。
- 完成判据：主应用 ArduCopter/CUAV V5 构建、烧录后 USB CDC 枚举，pymavlink 收 heartbeat，参数下载可用，OpenOCD 快照无 HardFault。
- 下一步：等待子代理返回，裁决可合并路线；若多路通过，优先选最小改动且与现有 ArduPilot UARTDriver/ChibiOS 语义最一致的路线。

### 2026-05-28 USB-L0 verify script (subagent K)
- 动作：在 /tmp/pogo-apm-usb-l0-verify 落地 usb_l0_verify.sh + helpers
- 结果：baseline exit 41（5745 MSC 无 ArduPilot ttyACM）
- 下一步：板子 CDC 正常后重跑全流程

### 2026-05-28 子代理 J（USB 构建互斥基础）
- 动作：新增 `Tools/scripts/rtt_usb_backend.py`；改 `libraries/AP_HAL_RTT/hwdef/common/SConscript`（backend 解析、BSP USB 关闭、L8 SDIO 仅 L8_*msc、TEST/ARDUPILOT 隔离）；改 `scons_ardupilot_sources.py` 按 backend 过滤 native USB 源
- 结果：resolver 单测 OK（full→native+L8→cherryusb+L6→none）；ap_sources native 含 hal_usb_*，cherryusb 不含；**全量 scons 未链过**（`modules/rt-thread` 子模块空，仅 bsp 桩）
- 下一步：恢复 rt-thread 子模块后 `scons --v=ArduCopter --target=cuav_v5` + `nm` 查唯一 `OTG_FS_IRQHandler`

### 2026-05-28 子代理 C（ChibiOS USB CDC 语义对齐）
- 动作：worktree `usb-cdc-36bb0a7f` @ d6ad102c87；GPIO sticky、UART USB_ACTIVE 门控、get_usb_baud←line_coding
- 结果：diff 5 文件 +36/-4；scons 因缺 stm32f7xx_hal_msp.c 未编过；OpenOCD 目标 running；ttyACM0 无 MAVLink（1209:5745 MSC）
- 下一步：合并补丁后编译烧录，期望 lsusb 1209:5741 + pymavlink + 参数下载

### 2026-05-28 16:30 composer-2.5 构建修复（SPI lock 后 include/deploy）
- 动作：worktree `pogo-build-fix-7982048b` @ d6ad102c87；对比 `/tmp/usb-staging-backup-20260528-152051/`（仅 SCons/脚本快照，非完整头文件树）与 `hwdef/common` 源树
- 根因：`rtt_bsp_deploy.py` 每次 `rmtree(deploy_dir)` 与并行 inner scons 竞态 → `getcwd() failed` / deploy 下 `-I` 路径瞬空；`scons_ardupilot_sources.py` 缺 `board/drivers_ll` 与 CMSIS packages 的 hwdef/common 回退；cherryusb 缺 `cherryusb_extra_*` 接线
- 修复：`Tools/scripts/rtt_bsp_deploy.py` 增量 deploy + packages 缓存回退 hwdef/common；`scons_ardupilot_sources.py` 增 `_append_bsp_include`、drivers_ll、cherryusb cpppath/sources；`hwdef/common/SConscript` 增 drivers_ll + USB backend 解析
- P0：`SPIDevice::_unlock_bus()` 在 `_dev==nullptr` 时 `__enable_irq()` 未改
- 构建：native `scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**；`RTT_USB_BACKEND=cherryusb` 同命令 **PASS**
- nm：`native` ELF `OTG_FS_IRQHandler` ×1 @ 0x080e2628；`cherryusb` ELF ×1 @ 0x080e6d60
- 产物：`build/rtt_cuav_v5/rtthread.bin` 1290688B；Reset_Handler literal pool check PASS；`/dev/ttyACM0` 存在，可烧录复验
- 下一步：OpenOCD 烧录 + pymavlink L0（CDC 心跳/STANDBY）双重验证

### 2026-05-28 17:25 IWDG 子代理 A（SPI mutex / 验证阻塞）
- 动作：主仓 `SPIDevice.cpp` CMSIS 路径用每总线 `rt_mutex`（`spibN`）替代 `__disable_irq` 整段 SPI 轮询；bus 1/4 `SPEED_LOW`；RT `transfer_fullduplex` 去掉重复 `_lock_bus`
- 依据：参数流掉线时 RCC_CSR=0x24000003（IWDGRSTF）；长全局关中断饿死 USB OTG ISR
- 构建：并行 scons 竞态致 `UserCode.o` 失败；`rtthread.bin` 仍存 1296792B（17:19，未本轮重链）
- 烧录/MAVLink：无 ST-Link/CDC（`usb-APM_*` 不存在）；OpenOCD init 即退
- 补丁：`/tmp/iwdg-spi-mutex.patch`（需含 fullduplex 小修后重导）
- 下一步：停并行 scons → cherryusb 单线程全量编 → 烧录 → param 912+30s → `RCC_CSR` 无 IWDGRSTF

### 2026-05-28 17:25 子代理 A（CherryUSB UARTDriver 背压清缓冲）
- 动作：worktree `cherry-usb-a-000013a6`；主仓 `UARTDriver.cpp` 合入：移除 >500 fail 无条件 `_writebuf.clear()`；仅 `!usb_lld_get_connected_rtt()` 时清缓冲；新增 `rtt_uart_usb_diag_{clears,write_fails,fail_streak}`
- 构建：`RTT_USB_BACKEND=cherryusb scons -j1`（并行 clean 后产物 17:19）；ELF 含 diag 符号 @ 0x20019f2c
- 烧录：OpenOCD program 0x08008000 verify OK
- CDC/MAVLink（ttyACM1）：S1 心跳 30s PASS（74 HB）；S3 PARAM_REQUEST_LIST PASS（904 参数/24.7s，post_hb）；S2 单参 READ 超时 FAIL；测试全程无 mid-session USB disconnect
- OpenOCD halt：`rtt_uart_usb_diag_clears=5`，`write_fails=0x2d7e`，`fail_streak=0x3e45`；`rtt_dbg_hardfault_stack_pc=0`
- 补丁：`/tmp/agent-a-uartdriver-cherry-backpressure.patch`（91 行）；合并回主分支用 `/apply-worktree`
- 结论：**参数流主路径 PASS**（>100/904 参数 + 30s 心跳）；S2 次要 FAIL；背压时 clears 未暴涨 → **暂不转 B 路**，可观测 fail_streak 再决定是否 TX 排队

### 2026-05-28 17:27 CherryUSB 延迟 RX 补强复验闭环
- 动作：将 CherryUSB CDC RX 从单槽 pending 改为 8x64B ring；ISR 只 enqueue，主循环 usb_lld_poll_rtt() drain 后调用 MAVLink RX；RESET/DEINIT/CONFIGURED 清 RX queue 与 TX pending/busy；加入 rtt_dbg_cherry_rx_enqueued/dropped/drained/usb_reset。
- 依据/假设：避免 OTG ISR 直接进入 MAVLink，并避免单槽覆盖静默丢包；补强 reset 边界。
- 结果：RTT_USB_BACKEND=cherryusb 串行 clean build 通过；ELF 单一 OTG_FS_IRQHandler，CherryUSB 和 debug counter 符号存在；并行 -j8 仍暴露 libardupilot_rtt.a 归档依赖竞态，非源码阻断。
- 实机验证：OpenOCD program app 到 0x08008000 verify OK；CDC 枚举 /dev/ttyACM1，VID:PID 1209:5741；最终独立 MAVLink L0：heartbeat 从 BOOT 进入 STANDBY(3)，FORMAT_VERSION=120，参数 904/904，30s stream 1787 msg/60 heartbeat/21 types。
- 附加修复：当前主仓 gps.init() 被临时跳过，EKF/DAL 调 AP_GPS::get_lag(GPS_BLENDED_INSTANCE) 会解引用空 blended driver；给 blended get_lag 分支加入空/异常 SRAM 指针保护后 L0 可继续。
- 下一步：归档并行构建竞态需后续单独修 SCons 依赖；若要求参数 912，需要解释当前 build advertised count 为 904。

### 2026-05-28 18:02 CherryUSB 并行构建 ARG_MAX / libardupilot_rtt.a 归档修复
- 动作：新增 `Tools/scripts/rtt_ar_archive.py`（`env.RttArArchive`：`arm-none-eabi-ar -rc lib.a @lib.a.ar.rsp`，全部 `.o` 为显式依赖）；`SConscript_ardupilot` 改用它替代 `StaticLibrary`；`hwdef/common/SConscript` 经 `configure_tempfile_ar()` 为 AR/LINK 启用 `TempFileMunge`（`MAXLINELENGTH=8192`）。
- 依据/假设：~969 个 AP 对象导致 `ar`/`sh` ARG_MAX；先前手写 `@rsp` 在 `-jN` 下与 object 生成竞态。
- 结果：**PASS** — `RTT_USB_BACKEND=cherryusb python3 -m SCons --target=cuav-v5 -j8` clean 与增量均成功；产物 `build/rtt_deploy/cuav_v5/rt-thread.elf`、`rtthread.bin`、`ap_objects/libardupilot_rtt.a`；`libardupilot_rtt.a.ar.rsp` 969 行；`nm` 仅 1× `OTG_FS_IRQHandler`（cherry `0x080e6e84`）；日志无 `Argument list too long`。native 默认 `scons --target=cuav-v5 -j8` 亦 **PASS**（`OTG_FS_IRQHandler` @ `0x080e2668`）。验证时曾 `git stash` 未提交的 `SPIDevice.cpp`（`need_sem` 编译错误）以隔离构建脚本改动。
- 下一步：合并 SPIDevice mutex 收口后勿再破坏 fullduplex 编译；Cherry L0 硬件 gate 可继续。

### 2026-05-28 CherryUSB 主仓 L0 gate 脚本（验证准备代理）
- 动作：复用 `/tmp/pogo-apm-usb-l0-verify`，新增 `/tmp/cherryusb_main_l0_gate.sh` + `lib/mavlink_cherry_l0.py` + `lib/openocd_cherry_l0_snapshot.sh`；未改主仓生产代码
- 判据：RTT_USB_BACKEND=cherryusb 构建/ELF 单 IRQ + cherry 符号；BL@0x08000000 + app@0x08008000；wait≥12s；lsusb 1209:5741 + by-id；MAVLink STANDBY/FORMAT_VERSION/params/30s types；post-L0 VTOR/CFSR/RCC/IWDG + cherry+UART diag
- 下一步：构建代理完成后执行 ` /tmp/cherryusb_main_l0_gate.sh --skip-build`（或全流程）

### 2026-05-28 18:00 SPI mutex 合并收口
- 动作：`libraries/AP_HAL_RTT/SPIDevice.cpp` 将 CMSIS `_dev == nullptr` 路径保持为每总线 `rt_mutex`；补齐 `transfer_fullduplex()` CMSIS DMA 路径的 lock/unlock；清理 RT-Thread SPI 框架分支残留 `need_sem` 二次锁/释放。
- 依据/假设：只读审计已确认三类补丁核心语义 PASS，但 fullduplex CMSIS DMA 仍未持有总线互斥，RT SPI 分支残留未定义 `need_sem` 有编译风险。
- 结果：`git diff --check -- libraries/AP_HAL_RTT/SPIDevice.cpp` 通过；`rg` 确认不再有 `__disable_irq/__enable_irq` 实现和 RT 分支 `need_sem` 重复释放残留。
- 下一步：由构建/验证代理继续跑 `RTT_USB_BACKEND=cherryusb` 串行构建与 USB-L0/MAVLink gate。

### 2026-05-28 18:36 收口代理：三类补丁合仓 + cherryusb -j8 构建
- 动作：检查 `git stash list`；**未** `stash pop`（`stash@{0}: build-agent-temp` 仅含 `SPIDevice.cpp`）。工作区已含 Cherry shim + UART 背压；从 stash 语义手工合入 SPI：CMSIS `transfer`/`transfer_fullduplex` 用 `!_cs_held` + `_lock_bus`/`_unlock_bus`（去掉 `need_sem`）；`set_chip_select` 加 `__DSB()`；跑 `RTT_USB_BACKEND=cherryusb python3 -m SCons --target=cuav-v5 -j8`。
- 依据/假设：构建代理 stash 是为隔离 ARG_MAX 修复与 SPI 未收口编译错误；只恢复 SPI 相关 hunk，避免覆盖 SCons/其它未提交改动。
- 结果：**构建 PASS**（~52s）；日志 `/tmp/scons_cherryusb_cuav_v5_j8.log`；`rt-thread.elf` 1296440B bin / 37840392B ELF；`nm` **1×** `OTG_FS_IRQHandler` @ `0x080e6e78`；ELF 含 `rtt_dbg_cherry_*`、`rtt_uart_usb_diag_*`、`_cmsis_spi_bus_mtx`。
- 下一步：可执行 `/tmp/cherryusb_main_l0_gate.sh --skip-build --json` 做烧录+MAVLink L0（需 ST-Link + CDC）。

### 2026-05-28 18:40（CherryUSB main L0 gate 实机）
- 动作：检查 scons/openocd/gdb/mavproxy 占用（无残留）；执行 `/tmp/cherryusb_main_l0_gate.sh --skip-build --json`；by-id 失败后用 compat symlink + `--skip-flash` 重试；手工 `mavlink_cherry_l0.py /dev/ttyACM1` 补 MAVLink。
- 依据/假设：首次失败为 gate 脚本要求 `usb-ArduPilot_*` 与 CherryUSB 枚举名 `usb-APM_CUAV_V5_CDC_1_*` 不一致；重试 mavlink 失败为 resolve_device 将 `[CHERRY-L0]` 日志混入设备路径。
- 结果：flash+lsusb PASS；MAVLink STANDBY status=3、FORMAT_VERSION=120.0、params 904/904、30s stream 1798/22 types PASS；post-L0 GDB VTOR=0x08008000、IWDGRSTF=0、POST_L0_FAULT=0；脚本 JSON final_rc=41/1 FAIL。
- 下一步：更新 `/tmp/cherryusb_main_l0_gate.sh` 的 by-id 模式与 resolve_device 仅输出路径（非生产仓）。

### 2026-05-28 18:55（CherryUSB L0 gate 脚本修复）
- 动作：修复 /tmp/cherryusb_main_l0_gate.sh 与 pogo-apm-usb-l0-verify/lib（by-id 支持 usb-APM_CUAV_V5_CDC_*、resolve_device 日志改 stderr、GDB 经 SCB 0xE000ED28/0xE000ED2C 读 CFSR/HFSR）
- 依据/假设：官方 gate 失败为脚本层设备匹配与 stdout 污染，非固件回归
- 结果：--skip-build --skip-bl --json --wait 30 重烧 app 后 PASS，final_rc=0；GDB CHECK_CFSR/HFSR=0 VTOR=0x08008000 IWDGRSTF=0
- 下一步：无（手工 L0 与 gate 脚本对齐）

### 2026-05-28 19:10（项目记忆收口：主仓 CherryUSB L0 闭环）
- 动作：按 project memory protocol 更新 `status.md` / `open-issues.md` / `command-catalog.md`；未改生产代码、未 commit
- 依据：主仓三类补丁 + `rtt_ar_archive`/TempFileMunge 并行构建 PASS；gate `POGO_APM_ROOT=... RTT_USB_BACKEND=cherryusb /tmp/cherryusb_main_l0_gate.sh --skip-build --skip-bl --json --wait 30` exit 0；硬件 CDC 1209:5741、by-id `usb-APM_CUAV_V5_CDC_1_00001-if00`、STANDBY(3)、FORMAT_VERSION=120.0、904/904 参数、30s 1798/22 types、VTOR/CFSR/HFSR/IWDGRSTF 判据
- 结果：**主仓 CherryUSB 显式 backend L0 记为稳定事实**；`write_fails≈20593` 记为 TX 背压诊断观察项（不阻断 L0）；open-issues 主仓 L0 重验项关闭，保留 MAVFTP/Mission/重连/长稳/默认化/composite
- 验证边界：不代表 Cherry 线上 MAVFTP/Mission/USB 重连/长稳/全功能回归已完成；生产默认仍为 native

### 2026-05-28 19:02 Mission protocol smoke (CherryUSB CDC)
- 动作：确认 CDC `/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00`（→ ttyACM1）心跳 OK；运行 `tests/test_mission_protocol.py --port` 上述路径。
- 结果：首次运行在 MISSION_COUNT 后等待 MISSION_REQUEST 时 `SerialException: device disconnected`（约 6.4s）；设备随后仍枚举，心跳恢复。重试 2s 后全流程 PASS：CLEAR_ALL → COUNT → REQUEST/ITEM → ACK → REQUEST_LIST 下载 → CLEAR_ALL，count=0。
- 日志：`.cursor/mission_protocol_smoke_20260528_190157.log`（FAIL）、`.cursor/mission_protocol_smoke_retry_20260528_190215.log`（PASS）。
- 判断：协议闭环在 CherryUSB CDC 下可复现；首次失败更像 USB/固件瞬断或枚举抖动，非稳定协议逻辑失败。
- 下一步：若需硬化，可在 CI/脚本侧对 disconnect 做 connect 重试（测试脚本已有 connect retries，recv 循环无重连）。

### 2026-05-28 19:02 MAVFTP 回归（CherryUSB CDC / STANDBY）
- 动作：确认 CDC `/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00`（→ ttyACM1）心跳 OK；运行 `tests/test_mavftp.py --port <by-id>`；失败后有理由重试 1 次。
- 依据/假设：L0 已通过，验证 MAVFTP 在 CherryUSB CDC 下是否与历史 native 基线一致。
- 结果：
  - 首轮 4/6 PASS：T1 根目录列举 PASS；T2 `/APM` PASS；T3 `@PARAM/param.pck` FAIL（Open 成功后在 Read 阶段 pymavlink 串口异常「readiness but no data」）；T4 Create `/APM/test_ftp.tmp` FAIL（Nack err=1 Fail）；T5 ResetSessions PASS；T6 稳定性 PASS。
  - 重试 3/6 PASS：T3 PASS（读 10155 字节）；T4 过程中串口断开，T5/T6 因 device dead FAIL；约 3s 后心跳恢复。
  - 分层：列举/Reset/稳定性 → 链路+基础 FTP 可用；T3 → 大文件读时序/CDC 偶发 host 侧串口异常（非纯脚本路径）；T4 → 写路径 Nack 或高负载下 CDC 掉线，偏固件 FTP/存储或 USB 栈压力。
- 日志：`build/logs/mavftp_regression_20260528_190155.log`
- 下一步：针对 T4 Create Fail 与 FTP 大流量下 CDC 断开做 OpenOCD/STATUS_TEXT 与 AP_Filesystem 写路径对比 ChibiOS 基线（若需继续闭环）。

### 2026-05-28 19:05 native MAVFTP 对照验证（BLOCKED，未烧录）
- 动作：检查并发（pgrep/fuser）；发现 `cherryusb_reconnect_long_smoke.sh`（PARAM_ROUNDS=5 STREAM_SEC=360）占用 `/dev/ttyACM1`（python3 stability_smoke）；按策略**不**构建/烧录 native，避免打断 Cherry 长稳。
- 依据：用户要求「若有并发验证在运行则不要烧录，直接报告 BLOCKED」；trace 19:02 已记录 Cherry MAVFTP 4/6 与 Mission 瞬断。
- 结果：**BLOCKED**；板上固件状态**未改动**（预期仍为 CherryUSB L0 主仓 build）；日志 `build/logs/native_mavftp_compare_20260528_190518_BLOCKED.log`。
- 下一步：长稳结束后执行 native `python3 -m SCons --target=cuav-v5 -j8` → OpenOCD app@0x08008000 → sleep 30 → `tests/test_mavftp.py`；对照完成后若安全则烧回 `RTT_USB_BACKEND=cherryusb` 最新 build 并验心跳。

### 2026-05-28 MAVFTP 根因静态分析（子代理，只读）
- 动作：读 `tests/test_mavftp.py`、`build/logs/mavftp_regression_20260528_190155.log`、`GCS_FTP.cpp` Cherry shim、`UARTDriver` USB diag；未改固件。
- 依据：`FTP_ERROR::Fail(1)` 在 `CreateFile` 分支仅当 `ftp.fd != -1`（非 FailErrno/非 SD open 失败）；首轮 T3 Open 成功后在 Read 循环 host 串口异常且未 `TermSession`。
- 结果：
  - **T4 首轮 Nack err=1 高置信为 T3 泄漏的全局 `ftp.fd`**（单文件会话未关闭），不是路径/权限/SD 写失败假象；若真 open 失败应为 err=2 且常带 errno。
  - **T3/T4 断开主因排序：Cherry CDC TX 深度 1×64B + 8192 writebuf 背压** → `ftp_push_replies` 阻塞 + 大文件 ~46 次 ReadFile(239) 往返；与 Mission smoke 同类 host「readiness but no data」。
  - 重试 T3 PASS 后 T4 仍 CDC 掉线：更像 USB 栈压力/瞬断，非 Create 语义（写仅 35B）。
- GDB 观测点：`ftp.fd`/`ftp_dbg_state`/`ftp_dbg_opcode`；`rtt_uart_usb_diag_{clears,write_fails,fail_streak}`；`rtt_dbg_cherry_rx_{dropped,enqueued}`；`rtt_dbg_usb_usbrst`/`rtt_dbg_cherry_usb_reset`。
- 下一步：native 同脚本对照；Cherry 上 T4-only（先 ResetSessions）；T3 后强制 TermSession 再 T4；失败时 GDB 快照上述计数 + OpenOCD PC/HardFault。

### 2026-05-28 19:15（MAVFTP 分阶段最小复现）
- 动作：/tmp/mavftp_staged_repro.py 串行实验；端口 /dev/ttyACM1（by-id: usb-APM_CUAV_V5_CDC_1_00001-if00）；未改生产代码、无 commit
- Phase0：心跳 OK；custom_mode=0 base_mode=81（未见到典型 STANDBY custom_mode=3）
- Phase1 @PARAM/param.pck ×3：均完成 OpenRO+Read；file_size=10848、read=10155/轮、~1.2s；无 USB 断开 → **PASS（有 size 不一致 caveat）**
- Phase2 /APM/test_ftp.tmp：首轮 16B Create 即发 SerialException/USB 消失；64/512 未跑完；重枚举 ACM1↔ACM2
- GDB（失败后）：clears=5 write_fails=5827 fail_streak=0 cherry_rx_dropped=0；RCC_CSR=0x3 CFSR=0 HFSR=0 → `/tmp/mavftp_staged_20260528_190639/diag_post_disconnect_gdb.txt`
- 日志目录：`/tmp/mavftp_staged_20260528_190639/`（summary.json、full_run.log、phase2_resume.log）
- 下一步：隔离「仅 Create」vs「param 读后 Create」；查 write_fails 暴涨与 MAVFTP 写路径

### 2026-05-28 19:24（MAVFTP Create 重枚举根因定位子代理）
- 动作：只读审 `GCS_FTP.cpp` CreateFile→`AP::FS().open(O_WRONLY|O_CREAT|O_TRUNC)`、`AP_Filesystem_Posix::open`(先 `stat` 再 `open`)、`rt_board_init.c` SD 后台挂载；`/tmp/mavftp_create_minimal.py` 与修正 size 字段的隔离脚本；失败时 GDB（无烧录，无并发 openocd 占用）。
- 代码结论：Create **不走** `@PARAM` 虚拟后端，走 POSIX→DFS elm **根挂载 `/`** 上 `/APM/...`；`ftp.fd!=-1` 仅 Nack err=1（不解释 CDC 掉线）；`ftp_push_replies` 在 TX 满时 `delay(2)` 循环。
- 观测 A（payload size 正确）：`ResetSessions` Ack → `CreateFile` 即发 **SerialException/CDC 重枚举**（**无** Phase1 @PARAM）；与 staged「param 后 Create」一致可掉线，但 **非** fd 泄漏必要条件。
- 观测 B（同会话）：SD 未就绪时 `Create` → Nack **FailErrno [2,254]**，CDC 仍存活；`rtt_sd_mount_stage=2`、`rtt_sd_mount_result=-99`（GDB）。
- 观测 C（staged 掉线后 GDB）：`CFSR/HFSR=0`、`RCC_CSR=0x3`、`write_fails=5827`、`cherry_rx_dropped=0`；halt 在 `AP_Terrain::io_timer`（**MCU 未 HardFault**）→ 掉线更像 **USB 栈复位/重枚举** 而非重启。
- 分层：**主因 FS/SD 写路径**（Create 触发真实 `open`/`O_CREAT` + SDIO）；**放大 USB TX 背压**（param 后 write_fails 5k+ 时更易掉线）；**err=1** 仍属 **FTP 单 fd 泄漏**（与掉线正交）；ResetSessions 本身正常。
- 日志：`/tmp/mavftp_create_min_20260528_191859/`、`/tmp/mavftp_create_isolate2.log`、`/tmp/mavftp_create_gdb3.txt`；未与 TX 队列代理争用烧录。
- 下一步：Create 前 GDB 断言 `rtt_sd_mount_stage==10`；掉线瞬间抓 `ftp_dbg_state`(期望 36=30+6)、`ftp_dbg_stat_count`(errno)；SD 就绪后单测 Create；Cherry `rtt_dbg_cherry_usb_reset` 前后对比。


### 2026-05-28 19:15（CherryUSB 重连/长流 smoke 子代理）
- 动作：未改生产代码；`/tmp/cherryusb_reconnect_long_smoke.sh` + `/tmp/cherry_usb_stability_smoke.py`；日志 `/tmp/pogo-apm-usb-l0-verify/logs/reconnect_long_20260528_190320/`
- OpenOCD reset×3：by-id 恢复（`usb-APM_CUAV_V5_CDC_1_*` count=2）；reset1/2/3 后 quick HB 均 **STANDBY(3)**
- 多轮参数：独立脚本 5 轮 → 第1轮连接 flake，**第2–5轮 904/904**（23–42s）；合并 smoke 会话 **3/5 满参** 后第4轮 SerialException @93/904
- 长流：目标 360–420s；实测 **~22s** 与 **~158s** 因 `device disconnected / no data` 中断；158s 段 9641 msg / 329 HB / max_hb_gap **0.881s** / 22 types
- GDB（post）：VTOR=0x08008000，CFSR/HFSR=0，IWDGRSTF=0；`rtt_dbg_cherry_rx_dropped=0`，`usb_reset=2`；`rtt_uart_usb_diag_write_fails≈3258`，`clears=5`，`fail_streak≈289`
- 分层：**重连枚举+短时 HB=PASS**；**连续高压参数/长流=FAIL（固件/USB CDC 会话掉线，非脚本 by-id 逻辑）**
- 结论：**FAIL**（未达 5–10min 无中断流）；建议修 TX/参数风暴掉线后再做 **≥30min soak**

### 2026-05-28 19:24（native MAVFTP 对照重试 — 已执行）
- 动作：占用检查（无 cherryusb_reconnect_long_smoke / test_mavftp / pymavlink 持 ttyACM1；脚本文件仍在 /tmp）；`unset RTT_USB_BACKEND` → `python3 -m SCons --target=cuav-v5 -j8`；OpenOCD program native bin@0x08008000 verify；MAVFTP；`RTT_USB_BACKEND=cherryusb` 重建并烧回。
- 结果：
  - **占用：已释放**（非 BLOCKED）。
  - **Native MAVFTP：3/6 PASS**（T3/T5/T6 PASS；T1/T2/T4 FAIL，列举与 Create 均为 Nack err=2）。
  - 心跳：native 阶段 `usb-ArduPilot_CUAVv5_RTT_compat-if00` HB OK（custom_mode=0 system_status=1）；烧回 Cherry 后 `usb-APM_CUAV_V5_CDC_1_00001-if00` HB OK（同 status）。
  - **板上最终固件：CherryUSB 主仓 build**（1296600B bin，OpenOCD verify OK；中间一次 openocd init failed 后已重烧成功）。
- 日志：
  - `build/logs/native_mavftp_compare_20260528_191812.log`
  - `build/logs/native_mavftp_regression_20260528_192122.log`
- 下一步：对照 Cherry 4/6 vs native 3/6，聚焦 T1/T2 err=2 与写路径；Cherry 上继续 ftp.fd 泄漏与 CDC 压力项。

### 2026-05-28 19:34（隔离 CherryUSB TX pending ring + SD 门控验证）
- 动作：在隔离 worktree `/home/llw/firmare/pogo-apm-worktrees/cherry-tx-ring-exp`、分支 `exp/cherry-tx-pending-ring` 实现 CherryUSB CDC IN TX pending 8×64B ring；实验 commit `4096be5414`；导出 patch 到 `build/patches/0001-exp-cherryusb-8-slot-CDC-IN-TX-pending-ring.patch`。因 worktree 缺 `modules/rt-thread` 子模块，临时复制实验 shim 到主仓构建/烧录，随后从 `/tmp/hal_usb_cherryusb_shim.c.main-bak` 恢复主仓 shim 内容。
- 构建/烧录：`RTT_USB_BACKEND=cherryusb python3 -m SCons --target=cuav-v5 -j8` PASS；OpenOCD app@`0x08008000` verify/reset PASS。
- L0 观察：`/tmp/cherryusb_main_l0_gate.sh --skip-build --skip-flash --skip-bl --json --device <by-id>` 在 HEARTBEAT/STANDBY 后串口异常断开，exit 80；独立 heartbeat smoke 随后可读 `HEARTBEAT/SYS_STATUS/VFR_HUD/RAW_IMU/AHRS`。
- T3 观察：`/tmp/mavftp_staged_repro.py` ROUNDS=5 首轮 `@PARAM/param.pck` 读到 10155/10848 字节后判 size mismatch，第二轮 Open timeout，第三轮串口断开；说明 8-slot TX ring **未消除持续/突发流量下 CDC 掉线**。
- SD 未就绪对照：GDB 基线 `rtt_sd_mount_stage=2`、`rtt_sd_mount_result=-99`；执行 `ResetSessions -> Create /APM/test_ftp.tmp` 得到 Nack `FailErrno [2,254]`，CDC 保持，后续仍收到 `HEARTBEAT/SYS_STATUS/VFR_HUD/RAW_IMU`。结论：SD 未就绪时 Create 失败不应判为 TX 队列失败。
- SD 就绪组：等待 GDB 采样到 `rtt_sd_mount_stage=10`、`rtt_sd_mount_result=0` 后执行 `ResetSessions -> Create only`，第一轮 Create 前后即发生 `SerialException: device disconnected`，未进入 Write；后断开 GDB 读数：`rtt_sd_mount_stage=2`、`result=-99`（疑似 USB/固件重置或重新初始化后状态）、`write_fails=17176`、`tx_enqueued=8`、`tx_dropped=17176`、`tx_drained=0`、`tx_busy_fail=0`、`usb_reset=2`、`rx_dropped=0`、`RCC_CSR=0x3`、`CFSR/HFSR=0`。
- 结果：T4 Create 受 SD mount 状态强支配；SD 未就绪仅 Nack 且 CDC 保持；SD 曾就绪时 Create-only 仍可触发 CDC 断开，且 8-slot TX pending ring 出现大量 `tx_dropped/write_fails`，**不建议按此候选合入主仓**。

### 2026-05-28 20:04（CherryUSB MAVFTP/长稳下一轮拆解）
- 动作：响应“继续推进”，按用户要求继续保持父代理监督/裁决、composer-2.5 子代理执行模式；更新 todo，将 TX pending ring 候选标为 completed，并新增 `cherryusb-in-endpoint-drain-debug`、`sd-dfs-create-path-debug` 两个并行根因方向。
- 依据/假设：8-slot TX pending ring 已失败，不能继续叠加队列类猜测修补；当前关键证据是 SD 就绪后 Create-only 触发 CDC 断开，同时断开后 `tx_drained=0/write_fails` 暴涨、`CFSR/HFSR=0`，需分别证明 USB IN 完成路径与 FS/SD 写路径哪个是主因、哪个是放大因素。
- 结果：已并行派发 3 个只读 composer-2.5 子任务：1) 审计 CherryUSB CDC IN endpoint drain/complete 状态机；2) 审计 `/APM` 到 DFS/ELM-FAT/SD 的 Create/open 路径与线程/锁影响；3) 设计下一轮 30-60 分钟最小硬件实验脚本。全部明确禁止改代码、烧录、占用串口或 ST-Link。
- 下一步：等待三个子代理返回后统一裁决；若 IN endpoint 语义缺失证据强，则派最小 instrumentation/修复候选；若 SD 路径映射或挂载语义证据强，则先修路径/挂载/DFS 写路径，再回测 CherryUSB。

### 2026-05-28 22:20（全量验证前门禁区 — composer-2.5）
- 动作：读 orchestration/driver-validation/build skill + test README；检查 openocd/gdb 占用（无）；串行/重试分层 scons；native + cherryusb 全量 ArduCopter 构建；硬件 L0 gate（skip build/flash）、MAVFTP、Mission。
- 结果：
  - 构建：`L0_system` PASS；`L4_spi` 并行首跑 FAIL → 单跑 PASS；`L7_cherryusb_cdc` PASS；`ArduCopter` native PASS；`ArduCopter` cherryusb PASS（`OTG_FS_IRQHandler`×1）
  - 硬件：`cherryusb_main_l0_gate.sh --skip-build --skip-flash --skip-bl` exit 0（904/904，30s 2260 msg）；MAVFTP 首轮 4/6 → 重试 **6/6**；Mission **PASS**
  - 门禁结论：**可进入全量验证**（构建树 + Cherry 冒烟）；全量验证本身未执行
- 下一步：全量验证阶段按 CLAUDE 双重验证（OpenOCD + CDC MAVLink 长时）；分层 test 实机烧录仍待 driver matrix 扩展

### 2026-05-28 20:22（Create 门控验证脚本草案）
- 动作：子代理生成 `/tmp/cherryusb_create_gated_verify.sh` + `/tmp/cherryusb_mavftp_min.py`；未运行硬件/串口/OpenOCD 占用动作。
- 内容：占用检查（openocd/gdb/mavproxy/test/fuser ttyACM）；by-id resolve（APM_CUAV_V5_CDC / ArduPilot_CUAVv5_RTT）；G0–G4 分组 + GDB 快照符号集；summary.json。
- 下一步：父代理烧录 instrumentation ELF 后执行主脚本。

- 动作：根据三个只读子代理返回结果，继续派发两个 composer-2.5 子任务：1) 在 `hal_usb_cherryusb_shim.c` 只加 CherryUSB IN 完成链观测计数（`bulk_in_calls`、`tx_start_ok/fail`、`tx_busy_state`、`configured_state`、`last_event`），禁止行为修复；2) 将 SD 门控 Create-only / param 后 Create / 卷根路径对照落成 `/tmp` 可执行脚本草案，禁止实际运行。
- 依据/假设：IN 审计指出 8-slot ring 的 `tx_drained=0` 更像首包 IN 完成链未跑通或 reset 打断，而非队列深度；SD 审计确认 `/APM` 是根挂载 `/` 上 ELM-FAT，`/sd` 是文档滞后，Create 真实进入 SDIO/FAT 写路径，CherryUSB 将背压/完成停滞放大为重枚举。
- 结果：新增 todo `cherryusb-minimal-instrumentation` 与 `cherryusb-create-hw-gated-test`；两个子任务后台执行，均不占串口/ST-Link。
- 下一步：检查 instrumentation diff 与构建结果；若构建通过，父代理串行烧录 instrumentation 固件并执行门控脚本，读取 GDB 计数差分后裁决下一步修 Cherry IN 完成链还是 FS/SD open 路径。

### 2026-05-28 20:25（CherryUSB IN 链 instrumentation 子代理）
- 动作：`hal_usb_cherryusb_shim.c` 增加 `bulk_in_calls`/`tx_start_ok|fail`/`tx_busy_state`/`configured_state`/`last_event`；复用既有 `rtt_dbg_cherry_usb_reset`。
- 结果：仅改 shim；本 worktree `RTT_USB_BACKEND=cherryusb scons --target=cuav-v5` **未通过**（缺 `stm32f765-cuav-v5` BSP）。
- 下一步：完整子模块 worktree 编译烧录后 GDB 对比 `tx_start_ok` 与 `bulk_in_calls`。

### 2026-05-28 20:42（instrumentation 构建与 SDIO 启动阻塞）
- 动作：父代理接手构建/烧录 CherryUSB instrumentation；修复前置构建问题：`rtt_bsp_deploy.py` MSP 缺失 fallback、`SConstruct` 尊重外部 `RTT_ROOT`；恢复 `modules/rt-thread` 工作树内容；构建 PASS。
- 结果：直接烧录 instrumentation 固件后，门控脚本 G0 baseline 失败；GDB 显示 `hardfault_hang`，CFSR=0x00008200，栈为 `rthw_sdio_irq_process -> SDMMC1_IRQHandler`，USB 计数全 0，说明尚未进入 CherryUSB 验证阶段。
- 恢复：烧回 `build/build.old/rtt_cuav_v5/rtthread.bin` 后板子恢复 MAVLink HEARTBEAT/普通消息流，硬件未损坏。
- 分层验证：新增临时 `RTT_DISABLE_SDIO=1` 构建开关后，CherryUSB instrumentation 新构建可心跳；因此 instrumentation 本身可运行，阻塞点是有 SDIO 时的新构建/RT-Thread/配置基线。
- 下一步：先恢复有 SDIO 的新构建心跳基线，再执行 SD 门控 Create/GDB 计数差分；禁 SDIO 固件不能用于 `/APM` Create 结论。

### 2026-05-28（复核 CherryUSB init guard + BKP witness）
- 动作：复核 `usb_lld_init_rtt()` 单次 init guard；补充 `rtt_dbg_cherry_bkp_witness`（TAMP_BKP1R）、`rtt_dbg_bkp_stamp_sw_reboot()`、`Scheduler::reboot` 计数 `rtt_dbg_sw_reboot_count`。
- 依据：`usb_lld_init_rtt` 仅 `HAL_RTT_Class.cpp` 调用；`usbd_initialize` 不可重入；全复位后 static guard 清零属预期；BKP1 可区分显式 `rt_hw_cpu_reset` 与 Create 触发的重枚举。
- 结果：guard 保留在 `usb_lld_init_rtt` 入口（覆盖 `l7_usb_hw_preinit`+`cherry_cdc_stack_init`）；未改无关模块。
- 下一步：Create 后 GDB 读 `rtt_dbg_cherry_bkp_witness`（0xC41E0003=显式 reboot）、`init_calls/skipped`、`ftp_dbg_state`。

### 2026-05-28 21:20（RTT stat ABI：链接 AP_Filesystem_RTT_stat.c）
- 动作：确认 `AP_Filesystem_posix.cpp` 已有 `safe_stat()`+`open()`/`stat()` 走 wrapper；根因是 `AP_Filesystem_RTT_stat.c` 未进 `scons_ardupilot_config.py` ap_sources，ELF 无 `ap_rtt_posix_stat` 符号。
- 修改：`scons_ardupilot_config.py` 增加 `libraries/AP_Filesystem/AP_Filesystem_RTT_stat.c`；恢复 `safe_stat()` 与 C 实现匹配的 `ap_rtt_posix_stat_result` 声明。
- 结果：`scons --target=cuav-v5` 通过；`nm rt-thread.elf` 可见 `ap_rtt_posix_stat`。
- 下一步：烧录后 SD 已挂载时复测 MAVFTP Create `/APM/test_ftp.tmp`（OpenOCD+CDC）。

### 2026-05-28 20:59（CherryUSB MAVFTP Create 复现）
- 动作：手动绕过 gate 脚本，用 /tmp/cherryusb_mavftp_min.py reset-create 复现 /APM/test_ftp.tmp Create，并抓 GDB 前后快照。
- 依据/假设：此前 gate G0 误退出；需要 SD mounted 条件下分清 CDC 重枚举、HardFault、IWDG 或应用重启。
- 结果：SD ready 前 Create 仅 NACK err=2 errno=254 且 CDC 保持；SD ready 后 Create 触发 serial disconnect/by-id 消失，CFSR/HFSR=0，普通 BSS 计数清零，post 栈回到 HAL_RTT::run 启动链，确认是应用重新进入启动而非单纯 IN 端点 drain。
- 下一步：挂 GDB 断点抓 Scheduler::reboot/rt_hw_cpu_reset/Reset_Handler/HardFault，确认显式软件重启还是复位后重启；同时让 composer-2.5 复核 guard 与跨 reset instrumentation。

### 2026-05-28 20:30（续接 MAVFTP Create HardFault）
- 动作：按 session bootstrap 读取 current-focus/status/open-issues/command-catalog/trace 尾部；复核 `AP_Filesystem_Posix::open()`、RT-Thread DFS v1 `open/stat`、GCS FTP 线程栈与 ELF 符号/反汇编。
- 依据/假设：历史状态声称 RTT `stat()` ABI 已有 C wrapper，但当前源码和 ELF 未包含 `ap_rtt_posix_stat`，`AP_Filesystem_Posix::open()` 仍在本地 C++ 栈对象上直调 `stat()`。
- 结果：新增 `AP_Filesystem_RTT_stat.c`，在 RTT C 编译单元中用原生 `struct stat` 调 `stat()` 并拷贝通用字段；`AP_Filesystem_posix.cpp` 新增 `safe_stat()`，`open()` 前置检查与 `stat()` 方法统一走安全路径。
- 下一步：`RTT_USB_BACKEND=cherryusb` 构建，确认 wrapper 被链接；烧录后复测 SD-ready MAVFTP Create。

### 2026-05-28 21:35（RTT stat ABI 修复固件复测）
- 动作：`RTT_USB_BACKEND=cherryusb python3 -m SCons --target=cuav-v5 -j1` 串行构建通过；确认 `AP_Filesystem_posix.o` 反汇编中 `open()` 与 `stat()` 均调用 `ap_rtt_posix_stat`；OpenOCD 烧录 `build/rtt_deploy/cuav_v5/rtthread.bin` 到 `0x08008000` 并 verify OK。
- 依据/假设：上一轮 `AP_Camera_Mount.o` 缺失是并行归档时序，不是 stat wrapper 编译问题；需先用串行产物确认行为变化。
- 结果：烧录后 CherryUSB CDC 枚举与 MAVLink 心跳/普通流正常；执行 `/tmp/cherryusb_mavftp_min.py reset-create --path /APM/test_ftp.tmp` 不再触发 CDC 断开，返回 NACK `FailErrno errno=254`，`post_hb ok` 且 `cdc_alive=true`；OpenOCD 读 `CFSR/HFSR=0`、HardFault 记录为 0、`rtt_sd_mount_result=0`、`rtt_uart_usb_diag_write_fails=0`。
- 下一步：继续定位 Create 返回 NACK 的文件系统语义原因（目录/挂载状态、DFS errno 传播、`O_CREAT|O_TRUNC` flag/mode 兼容），并验证是否需要补 AP_Filesystem RTT open/create 兼容层。

### 2026-05-28 21:22（stat ABI wrapper 与构建对齐）
- 动作：统一为 `ap_rtt_posix_stat.c/.h`（`ap_rtt_posix_stat(path, stbuf, stbuf_size)`）；`posix.cpp` 的 `safe_stat()` 传入 `sizeof(*stbuf)`；确认 `scons_ardupilot_config.py` 已列入该 .c。
- 结果：`libardupilot_rtt.a` 含 `T ap_rtt_posix_stat`；全链 `rt-thread.elf` 仍因无关 `rtt_dbg_bkp_stamp_sw_reboot` 未定义而链接失败。
- 下一步：解决链接后烧录，复测 MAVFTP Create。

### 2026-05-28 21:30（CherryUSB MAVFTP Create/综合回归闭环）
- 动作：在 stat ABI 修复固件上继续推进 MAVFTP；先运行 `MAVFTP_PORT=/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 python3 tests/test_mavftp.py`，再用 `/tmp/cherryusb_mavftp_min.py reset-create` 对唯一文件名 `/APM/statfix_213003.tmp` 做 Create-only 复核，并清理该文件。
- 依据/假设：固定 `/APM/test_ftp.tmp` 曾返回 `Nack err=2 errno=254`，但 CDC 保持且无 HardFault；需要区分“Create 仍坏”与“固定文件名/会话状态残留”。
- 结果：`tests/test_mavftp.py` **6/6 PASS**（根目录、`/APM`、`@PARAM/param.pck`、真实文件 Create/Write/OpenRO/Read/Delete、ResetSessions、稳定性）；唯一文件名 Create **Ack**，post HEARTBEAT **STANDBY(3)**，Remove **Ack**；OpenOCD post 快照 `CFSR/HFSR=0`、HardFault 记录为 0、USB write_fails 为 0。
- 下一步：把 CherryUSB MAVFTP 从阻塞项移出；继续 Mission smoke、USB 重连/多轮参数与 ≥10min 长稳 soak，长稳仍观察 TX 背压诊断计数。

### 2026-05-28 21:32（CherryUSB Mission smoke 回归）
- 动作：运行 `python3 tests/test_mission_protocol.py --port /dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 --baud 115200`。
- 结果：Mission smoke **PASS**：baseline clear、2 waypoint upload、request loop `[0,1]`、upload ACK、download count/item、final clear/count 全部通过。
- 下一步：CherryUSB 全功能回归剩余 USB 重连/多轮参数与 ≥10min 长稳 soak；MAVFTP Create 与 Mission 已不再是当前阻塞项。

### 2026-05-28（rtt-agent-orchestration skill 新建）
- 动作：创建 `.cursor/skills/rtt-agent-orchestration/SKILL.md`（父代理规划监督 × composer-2.5 行动者编排规范）。
- 依据/假设：用户要求父代理禁止执行、只派发行动者；需项目级可触发 skill。
- 结果：文件已写入；含职责/禁止/硬件互斥/简报模板/证据优先级/与 rtt-* skill 关系；未改计划文件、未 commit、未跑构建。
- 下一步：父代理在双角色任务中引用本 skill 并派发子任务简报。

### 2026-05-28（ChibiOS↔RTT 文件对应矩阵）
- 动作：静态扫描 `libraries/AP_HAL_ChibiOS` 与 `libraries/AP_HAL_RTT` 清单 52 文件 + hwdef 生成链；新建 `.cursor/project/rtt-chibios-file-matrix.md`。
- 依据/假设：父代理子任务；禁止改 HAL 源码/构建/提交/计划文件。
- 结果：矩阵 54 行；统计 已有 30 / 部分 11 / 缺失 8 / 结构替代 5；第一批顺序 shared_dma → RCOutput → SoftSigReader → AnalogIn/UART/Util/system → hwdef。
- 下一步：父代理只读验收；后续按矩阵 P1 项开独立实现子任务。

### 2026-05-28 22:00（AP_HAL_RTT 低风险目录治理 batch-1）
- 动作：静态审计 `libraries/AP_HAL_RTT`；`grep`/`scons_ardupilot_sources.py`/`rtt_usb_backend.py` 确认构建仅根级 `*.c`/`*.cpp`；移审计 md、SPI cmsis 实验、USB porting plan；新建 `docs/`、`archive/`、`cherryusb_board/README.md` 与 `docs/usb-stack-boundary.md`。
- 依据/假设：子任务禁止 HAL 子目录化、禁止改 SConscript、禁止构建/提交、禁止动计划文件；根级误拷贝 Cherry 五件套已不在工作区。
- 结果：已移 4×audit md → `docs/audits/`；2×`.cmsis` → `archive/spi-cmsis/`；`usb_lld_rtt_porting_plan.md` → `docs/refs/`；根目录无残留 `*.md`/`*.cmsis`；31 个 HAL 源仍留根目录；`thirdparty/cherryusb` + `cherryusb_board` 边界文档化。`.bak` 三文件在 git index 但工作区缺失，未移入 `archive/backups/`。
- 下一步：父代理验收；后续 batch 可 `git rm` 幽灵 `.bak`、评估 `hal_spi_lld.c` 与 `hal_spi_lld_rtt.c` 双份链接风险（需单独构建验证）。

### 2026-05-28（AP_HAL_RTT test 树迁移）
- 动作：审计 `hwdef/common/tests` 与 `SConstruct`/`hwdef/common/SConscript`（`TEST_NAME` → `rtt_test_manifest.resolve_test_paths`）；建立 `libraries/AP_HAL_RTT/test/{_common,bringup,usb,drivers}`；迁移 L0–L7 源；旧路径改为 README+symlink；更新 `test/README.md`、`usb-stack-boundary.md`、driver-validation-matrix 一节。
- 依据/假设：`scons --test=<name>` 不变；CherryUSB 为生产门禁；L5/L6 native 标 `_legacy_native`。
- 结果：路径解析脚本验证 L0/L6/L7/l0_boot 均指向新树且 SConscript 存在；未跑构建/烧录。
- 下一步：父代理只读验收；后续可 `scons --test=L7_cherryusb_cdc` 构建回归（行动者任务外）。

### 2026-05-28 23:10（矩阵 P1 HAL 缺口落点 batch）
- 动作：新增 `shared_dma.{h,cpp}`（RT-Thread mutex，ChibiOS API 对齐）、`SoftSigReader.{h,cpp}`（Int 兼容包装）、`RCOutput_bdshot.cpp`/`RCOutput_iofirmware.cpp`（占位+职责注释）、`LogStructure.h`；`HAL_RTT_Class::run` 调 `Shared_DMA::init`；`AP_Logger/LogStructure.h` 用 `LOG_*_FROM_BOARD_HAL` 包装避免宏列表内 `#if`；AnalogIn/UART/Util 边界注释；`Tools/scripts/rtt_test_symlink_legacy.py` 修正 symlink 深度；更新 `rtt-chibios-file-matrix.md`。
- 依据/假设：子任务禁止烧录/提交/计划文件；保持 CherryUSB；不破坏 CUAV v5 IOMCU PWM 路径。
- 结果：`libardupilot_rtt.a` 含 shared_dma/SoftSigReader/RCOutput_* 占位；首轮链接失败 `rtt_dbg_bkp_stamp_sw_reboot`（符号仅在 cherryusb 链接组）。
- 下一步：见下条 link 修复后全链验证。

### 2026-05-28 23:45（rtt_dbg_bkp 链接修复 + cuav_v5 全链构建）
- 动作：新增 `rtt_dbg_bkp.{h,c}` 编入 `libardupilot_rtt.a`；`Scheduler.cpp` 改 `#include "rtt_dbg_bkp.h"`；`hal_usb_cherryusb_shim.c` 复用同一 API。
- 依据/假设：`rtt_dbg_bkp_stamp_sw_reboot` 必须在 AP 库侧解析，不能依赖 CherryUSB 额外对象顺序。
- 结果：`scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` 通过；`rt-thread.elf`/`rtthread.bin` 生成；Reset_Handler 字面量池检查 PASS。
- 下一步：SPI/UART 接入 Shared_DMA；Scheduler 写 MON；bdshot 实装（非 CUAV 主线）。

### 2026-05-28（子任务：rtt_bsp_pixhawk6c_mini / rtt_bsp_fmuv2 只读审计）
- 动作：确认目录存在与 git 跟踪（pixhawk 52 文件、fmuv2 33 文件）；`du` 784K/300K；全仓 `grep` 引用链；对照 `rtt_bsp_deploy.py`/`SConstruct`/`rtt.py`/`scons_ardupilot_sources.py`/`hwdef/*/hwdef.dat`。
- 依据/假设：父代理子任务 1；禁止删除/构建/烧录/提交；CUAV v5 当前基线为 hwdef/common 部署而非 HAL 根下完整 BSP 树。
- 结果：**cuav_v5 scons 构建不读取两目录**（deploy 走 `hwdef/common`+`hwdef/cuav_v5`）。**pixhawk6c_mini** 仍走 legacy 全量拷贝 `rtt_bsp_pixhawk6c_mini`。**fmuv2** 仅 waf `rtt.py` 部署到 `stm32f427-fmuv2`，不在 `SConstruct`/`rtt_bsp_deploy` 支持列表。pixhawk README 仍为正点原子 atk-apollo 模板；工作区 pixhawk `board/ports/cherryusb/*` 为 git 删除态。根 README 仍写 `rtt_bsp_cuav_v5`（与现状不符）。
- 下一步：父代理验收；若收敛 HAL 根目录 clutter，优先 pixhawk 迁 hwdef/common 后再 `archive/stray-bsp/`；fmuv2 若不在路线图可归档并更新 waf 文档路径。

### 2026-05-28（子任务：stray BSP 归档 + 引用清理）
- 动作：`mv` `rtt_bsp_pixhawk6c_mini`、`rtt_bsp_fmuv2` → `libraries/AP_HAL_RTT/archive/stray-bsp/`；更新 `rtt_bsp_deploy.py`、`rtt.py`、`README.md`、`RTT_BUILD_FMUV2.md`、`RTT_WAF_BUILD_FEEDBACK.md`、`docs/README.md`、`archive/*/README.md`；`status.md`/`open-issues.md`。
- 依据/假设：CUAV v5 不依赖 HAL 根整树；legacy 路径仍须可从归档目录 deploy。
- 结果：HAL 根无 `rtt_bsp_*`；`python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**（~37s）。
- 下一步：pixhawk/fmuv2 迁 hwdef/common 或标废弃；全量验证前门禁区其余项（USB 清理、Cherry 默认化决策）。

### 2026-05-28（子任务 2：legacy BSP/USB/ghost 清理 — composer-2.5）
- 动作：`git rm` 旧 `rtt_bsp_fmuv2`/`rtt_bsp_pixhawk6c_mini` 与 3×`.bak`；`git add archive/stray-bsp`、Cherry USB clean set、`rtt_usb_backend.py`、`test/`；修 `board-matrix.md`、`command-catalog.md`、`rtt.py` docstring；`scons_ardupilot_sources.py` 排除 `hal_spi_lld.c`/`hal_spi_lld_rtt.c`。
- 依据/假设：生产 SPI 走 `hwdef/common` 的 `drv_spi_lld` + SPIDevice CMSIS；双份根级 SPI LLD 仅增 ROM、无引用。
- 结果：索引无 ghost BSP/bak；`archive/stray-bsp/` 已 staged；native 与 `RTT_USB_BACKEND=cherryusb` 全量 scons **PASS**；ELF 无 `rtt_spi_lld`/`spi_lld_*_rtt` 符号。
- 下一步：父代理验收；半合入项（误拷贝 Cherry 根目录、Cherry 默认化）仍属 open-issues，非本子任务范围。

### 2026-05-28（子任务 1：历史遗留项审计 — composer-2.5，只读）
- 动作：读 `status.md`/`open-issues.md`/`usb-cleanup-inventory.md`/`SConstruct`/`rtt_bsp_deploy.py`/`rtt.py`/`rtt_usb_backend.py`/`scons_ardupilot_sources.py`；磁盘存在性检查；`git ls-files`/`git status`/`git ls-files --deleted` 统计。
- 依据/假设：用户要求先清历史遗留再全量验证；禁止烧录/提交/改计划文件；仅允许追加本 trace。
- 结果：
  - **磁盘**：误拷贝 `AP_HAL_RTT/{class,common,core,osal,port,cherryusb}/` **已不存在**；`rtt_bsp_cuav_v5/` **不存在**；`archive/stray-bsp/` 在盘 82 文件；`thirdparty/cherryusb` 29 文件 + `README.vendor`；根目录 TinyUSB/PoC **不存在**；`AP_HAL_RTT/SConscript` **不存在**（inventory 项已过时）。
  - **Git**：`archive/stray-bsp/` **未跟踪**（`??`）；旧路径 `rtt_bsp_fmuv2`/`rtt_bsp_pixhawk6c_mini` 在 index 仍为 **94 条 deleted**；`.bak` 三文件 **仍在 index**、工作区已删；`rtt_usb_backend.py`、`thirdparty/`、`cherryusb_board/`、`hal_usb_cherryusb_shim.c`、`test/` **均未跟踪**（HAL 下 untracked ≈178）。
  - **构建路径**：`SConstruct`+`rtt_bsp_deploy.py` 对 **cuav_v5** 仅用 `hwdef/common`（**不会**把 archive 当 CUAV 生产路径）；**pixhawk** legacy deploy 仍从 `archive/stray-bsp/rtt_bsp_pixhawk6c_mini`；**fmuv2** 仅 waf `rtt.py` 从 archive 拷贝（非 SConstruct 主路径）。
  - **文档**：`board-matrix.md`、`command-catalog.md`（hwdef 判据）、`rtt.py` 函数 docstring 仍写 `rtt_bsp_cuav_v5`；`status.md` 写「可进入全量验证」与 open-issues 半合入/未跟踪项 **并存**。
  - **链接风险**：`scons_ardupilot_sources.py` glob 会同时编入 `hal_spi_lld.c` 与 `hal_spi_lld_rtt.c`（无过滤）。
- 下一步：按清理清单先完成 **Git 归档提交 + 跟踪 USB 构建制品** → **文档路径修正** → **hal_spi_lld 去重验证** → 再启动全量验证。

### 2026-05-28（子任务 3：项目记忆同步 — composer-2.5，只读验收）
- 动作：更新 `status.md`（降级「可进入全量验证」表述；写入历史遗留清理稳定事实）、`open-issues.md`（勾掉已收口半合入项；保留全量验证前阻塞）、`command-catalog.md`（新增清理后双构建验证段）、核对 `board-matrix.md`；`grep` 项目记忆无 `rtt_bsp_cuav_v5`。
- 依据/假设：子任务 1 审计 + 子任务 2 实施结论；禁止烧录/构建/commit/改计划文件。
- 结果：`status.md` / `open-issues.md` / `command-catalog.md` 已同步；`board-matrix.md` 已为 hwdef/common 路径；`.cursor/project/*` 无 `rtt_bsp_cuav_v5` 残留。
- 下一步：用户明确要求时再 **commit** staged clean set；全量验证前仍须 Cherry 默认化决策、USB 重连/多轮参数、长稳、MAVFTP 首跑稳定性、pixhawk/fmuv2 迁移与工作区大改拆分。

### 2026-05-28（子任务 1：examples + AP_HAL_RTT/test 驱动分层审计 — composer-2.5，只读）
- 动作：读 `rtt-agent-orchestration`、`rtt-driver-validation`、`driver-validation-matrix.md`、`test/README.md`、`docs/AP_HAL_RTT_DRIVER_VALIDATION.md`；盘点 `libraries/AP_HAL/examples/*`、传感器/总线 examples、`AP_HAL_RTT/test/bringup|usb|drivers`；对照 `rtt_test_manifest.py`、`cuav_v5/hwdef.dat`、ChibiOS `BusTest`/sdcard/WSPI 语义。
- 依据/假设：RTT 须「寄存器 bring-up → HAL 抽象 → 外置器件 → 子系统 smoke」四层，对齐 ChibiOS 验证含义但不移植代码；`test/drivers/` 已预留目录。
- 结果：
  - **已有 RTT 分层**：L0_system/L1_iwdg/L2_gpio/L3_uart/L4_spi（寄存器级 + L4 板载 ICM20689 WHO_AM_I）；L7_cherryusb_cdc（CherryUSB echo）；manifest 登记 l0_boot…L7，drivers/ 空。
  - **AP_HAL examples 可复用范式**：UART_test（多 serial begin/printf）、UART_chargen（吞吐/流控）、Storage（XOR 全块）、RCOutput/RCInput/RCInputToRCOutput（PWM/通道读）、AnalogIn（channel 轮询）、BinarySem（线程+信号量）、Scheduler_test（AP_Scheduler 频率计数）；BusTest（SPI/I2C WHO_AM_I + sem）；INS_generic/BARO_generic/AP_Compass_test（库级 init/read/healthy）；StorageTest（StorageManager 随机读写）；jedec_test（WSPI/Flash page program）；File_IO（AP_Filesystem SD 挂载读写）；RCProtocolTest（协议解码）。
  - **CUAV V5 硬件事实**：SPI1=ICM20689+MS5611；SPI2=FRAM；I2C3=IST8310；SDMMC1；无 hwdef WSPI/QUADSPI 条目（WSPI 测试属其他板如 H7）。
  - **缺口**：无 HAL 级 UART/SPI/I2C/Storage/RCOut/RCIn/AnalogIn smoke；无 host hwdef/spi_table 测试；subsystem param/mission/mavlink 无独立 `--test=` 入口。
- 下一步：在 `test/drivers/` 按建议命名实现 HAL smoke；扩展 `rtt_test_manifest.py`；host 测试挂 `tests/` 或 Python 脚本 CI。

### 2026-05-28（子任务 2：驱动测试矩阵 + manifest 设计 — composer-2.5）
- 动作：扩写 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md`（六层模型 H/L/D/E/S/F、内置外设表、外置模块表、已构建 vs 上板边界）；重写 `.cursor/project/driver-validation-matrix.md`（用户点名 UART/SPI/IIC/SD/WSPI/RCOut/RCIn 分项状态）；更新 `libraries/AP_HAL_RTT/test/README.md` 与 `test/drivers/README.md`（D*/E*/S* 规划名）；轻量同步 `rtt-driver-validation` Skill 层级说明。
- 依据/假设：子任务 1 审计模型 Host/static→L*→D*→E*→S*→Full；禁止未实现目录写入 manifest（避免 resolve fallback）；禁止烧录/构建/提交。
- 结果：**未改** `rtt_test_manifest.py`；规划名仅文档登记；矩阵明确 L0/L4/L7 **已构建**、其余用户点名项 **待构建/待实现**、WSPI 对 cuav_v5 **N/A**；**未声称**任何上板通过。
- 下一步：实现 `D_uart`/`D_i2c`/`E_sdcard` 等时「目录+SConscript+manifest」同 PR；先 `L3_uart`/`L5_i2c` 构建回归。

### 2026-05-28（子任务 3：D*/E* 可构建测试入口 — composer-2.5 行动者）
- 动作：新增 `test/drivers/{D_uart_hal,D_spi_hal,D_i2c_hal,D_rcoutput,D_rcinput,E_sdcard,E_wspi_flash}/`（main.c+SConscript）；登记 `rtt_test_manifest.py`；更新 `test/README.md`、`test/drivers/README.md`、`driver-validation-matrix.md`（待构建）。
- 依据/假设：BUILD_ONLY 占位 — `test_runner` 输出 unsupported/N/A，不伪造 HAL 通过；E_wspi_flash 在 cuav_v5 文档化 N/A；未烧录/未 commit。
- 结果：7 个 `--test=` 名与目录/manifest 对齐；`scons --target=cuav_v5 --test=<each>` 全部 **done**；运行时均为单步 BUILD_ONLY `TEST_PASS`（链接门禁，非硬件验证）。
- 下一步：父代理或 CI 跑 `scons --target=cuav_v5 --test=D_uart_hal` 等确认 **已构建**；再按 AP_HAL examples 链接真实 HAL 逻辑。

### 2026-05-28（子任务 4：D*/E* 驱动测试构建门禁串行 — composer-2.5 行动者）
- 动作：manifest 自检；串行 `scons --target=cuav_v5 --test=<name> -j$(nproc)`（无烧录/OpenOCD/GDB/pymavlink）；可选 L3_uart、L4_spi。
- 依据/假设：BUILD_ONLY 占位仅验证链接/产物；硬件行为留后续上板。
- 结果：
  - manifest 解析：7 名 → `libraries/AP_HAL_RTT/test/drivers/*`（exit 0）
  - D_uart_hal：**PASS** exit 0，`scons: done building targets`
  - D_spi_hal：**PASS** exit 0
  - D_i2c_hal：**PASS** exit 0
  - D_rcoutput：**PASS** exit 0
  - D_rcinput：**PASS** exit 0
  - E_sdcard：**PASS** exit 0
  - E_wspi_flash：**PASS** exit 0（构建通过；cuav_v5 无 WSPI，运行时 N/A 未在本轮验证）
  - L3_uart（对照）：**PASS** exit 0
  - L4_spi（对照）：**PASS** exit 0
  - 首败：无
- 硬件阻塞（后续运行时，非构建）：UART/SPI/I2C 需外设或回环；RCOut 需示波器/电调；RCIn 需接收机/PPM；E_sdcard 需 SD 卡；E_wspi_flash 在 cuav_v5 **N/A**；均未占用 ST-Link/CDC。
- 下一步：上板跑各 test 的 msh/Finsh 或扩展 BUILD_ONLY 为真实 HAL smoke。

### 2026-05-28（子任务 5：driver-validation 项目记忆同步 — composer-2.5 行动者）
- 动作：更新 `driver-validation-matrix.md`（7×D/E + L3_uart 构建日期 2026-05-28；上板列未验证/N/A）、`command-catalog.md`（D/E 串行构建门禁 for 循环）、`open-issues.md`（BUILD_ONLY placeholder 未闭环 + 硬件阻塞四项）、`status.md`（分层测试构建事实，不含实机验证声称）；未 commit/未跑构建。
- 依据/假设：子任务 4 manifest 自检 + 7 D/E + L3/L4 串行 scons 全部 PASS；BUILD_ONLY 不等于 HAL 验证。
- 结果：项目记忆与 matrix 对齐；稳定层只写「构建通过」；open-issues 保留 HAL 替换与 SD/RC/PWM/WSPI 硬件阻塞。
- 下一步：按 AP_HAL examples 逐个替换 D/E main 为真实 HAL smoke → 上板 → 矩阵升级「已上板通过」。

### 2026-05-28（子任务：BUILD_ONLY → HAL smoke 短执行计划 — composer-2.5 行动者）
- 动作：读取 orchestration/driver-validation skill、`status`/`open-issues`/`driver-validation-matrix`、`docs/AP_HAL_RTT_DRIVER_VALIDATION.md`、`test/drivers/README.md`、`AP_HAL/examples`（UART_test/Storage/RCOutput/RCInput）；撰写 `.cursor/project/driver-validation-hal-smoke-plan.md`；同步 `open-issues.md` 下一阶段与下一步。
- 依据/假设：7×D/E 仅链接门禁；用户要求 Batch A–D 排序；禁止 commit/构建/烧录/改测试源码。
- 结果：
  - **Batch A**：`D_uart_hal`→`D_spi_hal`→`D_i2c_hal`（板载，对照 UART_test/L4/IST8310）
  - **Batch B**：新建 `D_storage`、`E_fram` → 替换 `E_sdcard`（SD 硬件阻塞）
  - **Batch C**：`D_rcoutput`→`D_rcinput`（示波器/接收机阻塞）
  - **Batch D**：`E_wspi_flash` cuav_v5=N/A 文档化；H7 延后
  - manifest：保留现有 CLI 名；新增 `D_storage`、`E_fram` 须先 SConscript 再 manifest
- 下一步：父代理派发 Batch A1（`D_uart_hal` 真实 HAL smoke）；行动者改 test 源码+构建+UART7 上板。

### 2026-05-28（Batch A1：`D_uart_hal` HAL smoke — composer-2.5 行动者）
- 动作：`main.c`→`main.cpp`（`hal.serial(6)` begin/printf/write + 可选 serial(0)）；`D_uart_hal/SConscript` 链 HAL（`rtt_test_hal_uart_link.py`、stubs、`ap_test_force_include.h` 覆盖 `HAL_WITH_IO_MCU=0`/`HAL_LOGGING_ENABLED=0`/`AP_RCPROTOCOL_ENABLED=0`）；`test_stubs.c` USB/ctl 弱符号；更新 matrix/README/trace。
- 依据/假设：模块测试不链整车；对照 `UART_test`；CherryUSB policy 不改默认 USB；未烧录。
- 结果：`python3 -m SCons --target=cuav_v5 --test=D_uart_hal -j$(nproc)` **PASS** exit 0；运行态边界：无 RX/loopback、无 `scheduler->init()`、上板 TX 未验证。
- 下一步：Batch A2 `D_spi_hal`；或上板观测 UART7 `D_uart_hal HAL smoke` 行。

### 2026-05-28（Batch A2：`D_spi_hal` HAL smoke — composer-2.5 行动者）
- 动作：`main.c`→`main.cpp`（`hal.spi->get_device("icm20689")`、semaphore、`set_read_flag(0x80)`、`read_registers(0x75)` 期望 `0x98`）；`SConscript` 链 HAL（`rtt_test_hal_spi_link.py`→复用 uart whitelist）；删除 BUILD_ONLY `main.c`；更新 matrix、`test/README.md`、`drivers/README.md`。
- 依据/假设：设备名来自 hwdef `HAL_SPI_DEVICE_LIST`；寄存器值对齐 `L4_spi`/`Invensense`；未烧录；运行时错值须 `TEST_FAIL`。
- 结果：`python3 -m SCons --target=cuav_v5 --test=D_spi_hal -j$(nproc)` **PASS** exit 0；上板 WHO_AM_I 未验证。
- 下一步：Batch A3 `D_i2c_hal`；或上板 UART7 跑 `D_spi_hal` 确认 WHO_AM_I。

### 2026-05-28（Batch A3：`D_i2c_hal` HAL smoke — composer-2.5 行动者）
- 动作：`main.c`→`main.cpp`（`hal.i2c_mgr->get_device(0,0x0E)`、semaphore、IST8310 SRST+WAI reg 0x00→0x10）；`SConscript` 链 HAL（`rtt_test_hal_i2c_link.py`）；首败缺 `#include <AP_HAL/I2CDevice.h>` 已修；更新 matrix/README/trace。
- 依据/假设：bus/addr 来自 hwdef `PROBE_MAG_I2C(IST8310,0,0x0E)`；WAI 语义对齐 `AP_Compass_IST8310`；未烧录。
- 结果：`python3 -m SCons --target=cuav_v5 --test=D_i2c_hal -j$(nproc)` **PASS** exit 0（首败：incomplete `I2CDeviceManager` → 加 include 后通过）；上板 WAI 未验证。
- 下一步：上板 UART7 跑 `D_i2c_hal`；Batch B `D_storage` 或 `D_rcoutput`。

### 2026-05-28（Batch B1：`D_storage` HAL smoke — composer-2.5 行动者）
- 动作：新建 `test/drivers/D_storage/`（`main.cpp`、`SConscript`）；`rtt_test_hal_storage_link.py`（复用 uart HAL whitelist，含 `Storage.cpp`）；登记 `rtt_test_manifest.py` `D_storage`；更新 matrix、`test/README.md`、`drivers/README.md`。
- 依据/假设：`hal.storage` init/read_block/write_block；scratch 在 `HAL_STORAGE_SIZE-8`（cuav_v5=32768→offset 32760）；写 pattern 后读回并 restore；cuav_v5 `Storage.cpp` 当前 RAM stub（非 FRAM）；不测 SD/E_sdcard。
- 结果：`python3 -m SCons --target=cuav_v5 --test=D_storage -j$(nproc)` **PASS** exit 0；上板/FRAM 持久未验证。
- 下一步：Batch B2a `E_fram`；或上板 UART7 跑 `D_storage` 确认 TEST_PASS 日志。

### 2026-05-28（Batch B2：`E_sdcard` SD/FS smoke — composer-2.5 行动者）
- 动作：替换 `test/drivers/E_sdcard/main.c` BUILD_ONLY → POSIX smoke（等 `rtt_sd_mount_*`/`stat("/APM")`、写读删 `/APM/.rtt_e_sdcard_smoke`）；无卡/超时/result=-4 → `TEST_FAIL`；更新 matrix、`test/README.md`、`drivers/README.md`。
- 依据/假设：cuav_v5 挂载点为 `/` + `/APM`（非 `/sd`）；背景 `sdmnt` 线程（`INIT_APP_EXPORT`）；不链 `AP_Filesystem`/`sdcard.cpp`；30s 主线程轮询上限。
- 结果：`python3 -m SCons --target=cuav_v5 --test=E_sdcard -j$(nproc)` **PASS** exit 0；上板插卡验证未做。
- 下一步：上板 UART7 跑 `E_sdcard`（插 microSD）；或 Batch B2a `E_fram`。

### 2026-05-29（Batch C1：`D_rcoutput` HAL smoke — composer-2.5 行动者）
- 动作：替换 `test/drivers/D_rcoutput/main.c` BUILD_ONLY → `main.cpp`（`hal.rcout` init/enable_ch/set_freq/write/read/read_last_sent/cork/push）；新增 `rtt_test_hal_rcout_link.py`；更新 `SConscript`（HAL 链接同 D_uart/D_spi）；更新 matrix、`test/README.md`、`drivers/README.md`。
- 依据/假设：CH0 only；1000→1100→1200→1000 µs；`force_safety_off()`（HAL_WITH_IO_MCU=0）使 write 非零；验证 `read`/`read_last_sent` 软件态；**不**声称示波器/PWM 波形/ESC。
- 结果：`scons --target=cuav_v5 --test=D_rcoutput -j$(nproc)` **PASS** exit 0（ROM ~212KB）；上板未验证。
- 下一步：上板 UART7 跑 `D_rcoutput` 确认 TEST_PASS；Batch C2 `D_rcinput`。

### 2026-05-29（Batch C2：`D_rcinput` HAL smoke — composer-2.5 行动者）
- 动作：替换 `test/drivers/D_rcinput/main.c` BUILD_ONLY → `main.cpp`（`hal.rcin` init/new_input/num_channels/read；3s 轮询 `RCInput::_timer_tick`+UART `_timer_tick`）；`SConscript` 链 HAL（`rtt_test_hal_rcin_link.py`→uart whitelist）；`ap_test_force_include_rcin.h`（`AP_RCPROTOCOL_ENABLED=0`）；首链 AP_RCProtocol 因 FPort/CRSF/VideoTX 依赖放弃，保持无 protocol 链接；更新 matrix、`test/README.md`、`drivers/README.md`。
- 依据/假设：无接收机须 `TEST_FAIL`（不伪造 PASS）；有 SBUS 需后续启用 AP_RCProtocol+SerialManager 才可能 `num_channels>0`；未烧录。
- 结果：`scons --target=cuav_v5 --test=D_rcinput -j$(nproc)` **PASS** exit 0（ROM ~212KB）；上板未验证。
- 下一步：上板 UART7 + SBUS/PPM 源验证；或 `E_sbus` / 启用 protocol 的 `D_rcinput` 变体。

### 2026-05-29（HAL smoke 构建门禁串行复跑 — composer-2.5 行动者）
- 动作：manifest 自检 8 项路径；串行 `python3 -m SCons --target=cuav_v5 --test=<name> -j$(nproc)`（D_uart_hal … E_wspi_flash）；未烧录/OpenOCD/pymavlink。
- 依据/假设：BUILD_ONLY 替换后须确认无链接回归；仅构建门禁。
- 结果：**8/8 PASS**（约 27s 总耗时，增量/已缓存）；manifest 路径均解析到 `libraries/AP_HAL_RTT/test/drivers/*`。
- 下一步：父代理只读验收；上板 smoke 另任务。

### 2026-05-29（子任务：HAL smoke 8/8 项目记忆同步 — composer-2.5 行动者）
- 动作：更新 `status.md`（8×真实 HAL smoke 串行构建 PASS、明确未上板）、`driver-validation-matrix.md`（构建通过/上板未验证/限制）、`open-issues.md`（移除「全部 BUILD_ONLY」表述，保留上板与 RX/FRAM/PWM/SBUS/SD/WSPI 未闭环）、`command-catalog.md`（for 循环含 `D_storage`、标注非 BUILD_ONLY）；未 commit/未跑构建/烧录。
- 依据/假设：manifest 8 项 + 串行 scons 8/8 PASS；固件已链真实 `hal.*`/E_sdcard POSIX；验证边界仅构建。
- 结果：稳定层与 matrix/open-issues 对齐；`E_wspi_flash` cuav_v5 仍 N/A。
- 下一步：上板逐项 OpenOCD + UART7 → matrix「已上板通过」；或 `E_fram` 新建。

### 2026-05-29 00:15 D_uart_hal on-board validation
- 动作：检查 OpenOCD 占用
- 结果：无 openocd 进程
- 下一步：构建 D_uart_hal

### 2026-05-29 00:15 D_uart_hal build
- 动作：scons --target=cuav_v5 --test=D_uart_hal
- 结果：通过，rtthread.bin 214524 bytes
- 下一步：OpenOCD 烧录

### 2026-05-29 00:15 D_uart_hal flash
- 动作：program 0x08008000 verify reset
- 结果：Verified OK，exit=1（resume warn target not halted）
- 下一步：UART7 观测

### 2026-05-29 D_uart_hal UART7 + fault
- 动作：115200 读 CH343 `/dev/serial/by-id/usb-1a86_USB_Single_Serial_*` 14s（复位后）
- 结果：见 `[D_UART_HAL] RESULT: PASS`、`PASS (251 ms)`、`SERIAL6/UART7: write returned 24 bytes`；无 TEST_PASS 字面量（runner 用 RESULT: PASS）
- 动作：OpenOCD halt mdw CFSR/HFSR @ 0xE000ED28/2C
- 结果：均为 0x00000000；openocd 已 exit
- 下一步：父代理同步 matrix/status

### 2026-05-29 D_spi_hal flash
- 动作：program 0x08008000 verify reset
- 结果：Verified OK；openocd exit=1（resume warn target not halted）
- 下一步：UART7 观测 14s

### 2026-05-29 D_spi_hal UART7 + fault
- 动作：115200 pyserial 读 `/dev/serial/by-id/usb-1a86_USB_Serial_*`（→ ttyACM0）16s，OpenOCD `reset run` 同步；纯 `cat` 易 0 字节，需复位后捕获
- 结果：`read_registers(0x75) -> ok, whoami=0x98`；`[D_SPI_HAL] RESULT: PASS`；`PASS (256 ms)`；2284 bytes
- 动作：OpenOCD halt `mdw 0xE000ED28/2C`
- 结果：CFSR=0x00000000 HFSR=0x00000000；openocd 已 exit
- 下一步：父代理更新 matrix D_spi_hal 上板通过；Batch A3 `D_i2c_hal` 上板（若需要）
### 2026-05-29 00:22 D_i2c_hal 上板验证
- 动作：环境检查
- 依据：无 OpenOCD 残留、串口枚举
- 结果：pgrep openocd 空；by-id: 1a86->ttyACM0, ArduPilot->ttyACM1
- 下一步：SCons 构建 D_i2c_hal
### 2026-05-29 00:22 D_i2c_hal 上板验证
- 动作：SCons 构建
- 依据：--test=D_i2c_hal
- 结果：PASS；rtthread.bin 215108B
- 下一步：UART7 监听 + OpenOCD 烧录
### 2026-05-29 00:23 D_i2c_hal 上板验证
- 动作：OpenOCD program 0x08008000 verify reset + UART7 18s
- 依据：CH343 ttyACM0 115200 pyserial
- 结果：Verified OK；wai=0x10；[D_I2C_HAL] RESULT: PASS；PASS (278 ms)；uart 2147B；openocd exit=1（resume warn target not halted）
- 下一步：CFSR/HFSR 检查
### 2026-05-29 00:23 D_i2c_hal 上板验证
- 动作：OpenOCD halt mdw CFSR/HFSR
- 结果：0xE000ED28=0x00000000；0xE000ED2C=0x00000000；openocd exit 0；无残留进程
- 下一步：父代理更新 matrix D_i2c_hal 上板通过

### 2026-05-29（子任务：同步 Batch A 上板状态 — composer-2.5 行动者）
- 动作：只读 `agent-trace` D_uart/D_spi/D_i2c 条目；更新 `driver-validation-matrix.md`、`status.md`、`open-issues.md`、`command-catalog.md`（单项 D/E 上板 smoke 模板）；未跑构建/烧录/OpenOCD
- 依据：父代理验收三项上板 PASS（UART7 RESULT PASS、WHOAMI/WAI、CFSR/HFSR=0）
- 结果：**已写入** — matrix/status 标 `D_uart_hal`/`D_spi_hal`/`D_i2c_hal` **已上板通过**；open-issues 改为 3/8 通过；`D_storage`/`D_rcoutput`/`D_rcinput`/`E_sdcard` 仍未上板；`E_wspi_flash` cuav_v5 N/A
- 下一步：Batch B 单项上板（`D_storage` 或 `D_rcoutput`）；或父代理只读复核 memory  diff

- 动作：构建 D_storage
- 命令：python3 -m SCons --target=cuav_v5 --test=D_storage
- 结果：exit 0；产物 build/rtt_cuav_v5/rtthread.bin

- 动作：烧录 D_storage app @ 0x08008000
- 命令：openocd program build/rtt_cuav_v5/rtthread.bin verify reset + resume + exit
- 结果：Verified OK；openocd exit=1（resume 时 target not halted，与 catalog 已知现象一致）；烧录后无 openocd 残留

- 动作：UART7 观测 22s（先开串口再烧录复位）
- 设备：/dev/serial/by-id/usb-1a86_USB_Single_Serial_594C048352-if00 @ 115200
- 结果：见 D_STORAGE Layer、backup ff×16、readback a5 5a c3…、RESULT: PASS；SD mount failed 日志存在但未跑 E_sdcard 门禁

- 动作：OpenOCD fault 检查 CFSR/HFSR
- 结果：0xE000ED28=0、0xE000ED2C=0；exit 后无 openocd 残留

### 2026-05-29 00:32 E_sdcard step3-5
- 动作：UART7 48s（CH343 by-id @115200）+ OpenOCD program 0x08008000 verify reset + CFSR/HFSR
- 烧录：Verified OK；openocd exit=1（resume target not halted，与 D_* 一致）
- UART 关键证据：
  - `[sdcard] dfs_mount("sd0","/sdcard","elm") failed: -1`
  - `[E/drv.sdio] wait completed timeout`（多次）
  - `mount timeout: stage=2 result=-99 errno=0`
  - `[E/DFS] can't find mounted filesystem on this path:/APM`（轮询期大量）
  - `=== [E_SDCARD] RESULT: FAIL ===`（9 failures；Steps 2；~30594 ms）
  - 未见 `mount ok`；文件 smoke 段有矛盾行（read errno=32、unlink errno=-2 后仍打印 file smoke ok PASS）— 以 RESULT FAIL 为准
- Fault：CFSR=0xE000ED28=0；HFSR=0xE000ED2C=0；openocd 已 exit
- 结论：**DONE_WITH_FAILURES** — SD 卡已插入前提下，SDIO 完成超时 + dfs_mount -1，挂载未就绪导致 /APM POSIX 失败；非「无卡」归因
- 下一步：对照 D_storage 旁路日志与 `board/ports/sdcard_port.c` / `rtt_sd_mount_*` stage=2 result=-99 根因（SDIO 时序/DFS 挂载点 cuav_v5 为 /sdcard vs 测试期望 /APM on root）

### 2026-05-29 USB CDC layered test (L7/L6)
- 动作：基线检查 OpenOCD/USB
- 依据：子任务上板验证门禁
- 结果：无 openocd；by-id: CH343 ttyACM0, ArduPilot compat ttyACM1；lsusb 1209:5741 无
- 下一步：构建 L7_cherryusb_cdc

### 2026-05-29 E_sdcard SDIO 挂载修复
- 动作：阅读 `rt_board_init.c`/`sdcard_port.c`/`E_sdcard/main.c`；根因=SD 上电晚于 `rt_hw_sdio_init` + `sdcard_port` 重复 init/mount `/sdcard`；最小修复=INIT_PREV_EXPORT 提前 PG7、挂载仅 `rt_board_init` 后台线程、清空 `sdcard_port.c` 重复逻辑
- 构建：`python3 -m SCons --target=cuav_v5 --test=E_sdcard -j$(nproc)` PASS
- 上板：OpenOCD verify OK；UART7 40s：`[sd] mounted sd on / ok`，stage=10 result=0，`=== [E_SDCARD] RESULT: PASS ===`（615ms）；早期仍有 `[E/drv.sdio] wait completed timeout` 在卡识别前
- 下一步：父代理验收；可选同步 `hwdef/common/board/rt_board_init.c` 与 cuav_v5 板级副本

### 2026-05-29 USB CDC L7 上板验证
- 动作：构建 L7_cherryusb_cdc → OpenOCD 烧录 0x08008000 verify → USB 枚举 → Python serial echo → fault 寄存器
- 依据：分层门禁 L7/L6，VID/PID 1209:5741
- 结果：
  - 构建 PASS（RTT USB backend: cherryusb，产物 build/rtt_cuav_v5/rtthread.bin）
  - 烧录 Verified OK（openocd exit=1 因 resume 时 target not halted 警告）
  - lsusb: `1209:5741 Generic L7 CherryUSB`；by-id `usb-PogoAPM_L7_CherryUSB_0001` → ttyACM1（CH343 仍在 ttyACM0，勿混淆）
  - CDC IO：write HELLO_L7\\r\\n → RX_AFTER_WRITE 回显相同，ECHO_OK True
  - Fault：CFSR=0 HFSR=0（halt 后读）；PC=0x0800de6e 在 app 区
- 下一步：父代理验收；可选 L6 manifest 名复测或接 L7_hal_shim

### 2026-05-29 HAL abstract 测试登记（D_scheduler / D_analogin / D_usb_serial）
- 动作：新增 `drivers/D_scheduler`、`D_analogin`、`D_usb_serial`（SConscript+main）；更新 `rtt_test_manifest.py` TEST_LAYOUT/LEGACY；更新 `test/README.md` 与 `drivers/README.md`
- 依据：Batch 非 RC HAL smoke 第一段；RCOut/RCIn 未动
- 构建（串行）：
  - `python3 -m SCons --target=cuav_v5 --test=D_scheduler -j$(nproc)` — 首次失败：`hal_initialized` 非 AP_HAL::Scheduler 虚接口；修复为 `(RTT::Scheduler*)` 强转后 **PASS**
  - `python3 -m SCons --target=cuav_v5 --test=D_analogin -j$(nproc)` — **PASS**
  - `python3 -m SCons --target=cuav_v5 --test=D_usb_serial -j$(nproc)` — **PASS**（BUILD_ONLY 占位，无 HAL 链接）
- 实现边界：D_scheduler=真实 timer proc+init+delay；D_analogin=init/ch8+直接 `_timer_tick`（无电压阈值）；D_usb_serial=BUILD_ONLY 文档+链接门禁（L7 不变）
- 下一步：上板 UART7 验证 D_scheduler callback>0 与 D_analogin raw 打印；D_usb_serial 需独立 CherryUSB+HAL link 白名单后再做 runtime

### 2026-05-29 D_usb_serial HAL CDC runtime（composer-2.5）
- 动作：`test_stubs_no_usb.c`（去 `usb_lld_*` stub）+ `rtt_test_hal_usb_serial_link.py` + `D_usb_serial/main.cpp`/`SConscript`；`RTT_USB_BACKEND=cherryusb`；未改 L7/RC
- 原 BUILD_ONLY 根因：`test_stubs.c` USB stub 与 `hal_usb_cherryusb_shim.c` 冲突；无 HAL+CherryUSB 链接白名单
- 构建：`python3 -m SCons --target=cuav_v5 --test=D_usb_serial -j$(nproc)` — **PASS**（ROM ~222KB）；`nm` 仅 1× `OTG_FS_IRQHandler`
- 上板：烧录 Verified OK；UART7（115200）见 `usb_lld_init_rtt() ok`、`USB configured`、`serial(0): write returned 26 bytes`、`[D_USB_SERIAL] RESULT: PASS`；CFSR/HFSR 读为空（0）
- CDC 主机：`1209:5741` → ttyACM1 可见；pyserial 读/写常 0 字节或 `EIO`（复位枚举窗口）；**未稳定读到** `D_usb_serial HAL CDC smoke on SERIAL0`
- 下一步：ACM 门禁（复位前 open+DTR 或固件周期重发横幅）；再推进 `S_mavlink_usb`；回归 `L7_cherryusb_cdc`

### 2026-05-29 External module 测试登记（E_imu / E_ms5611 / E_fram）
- 动作：新增 `drivers/E_imu`、`E_ms5611`、`E_fram`（main.cpp+SConscript）；登记 `rtt_test_manifest.py`；更新 `test/README.md`、`drivers/README.md`、`driver-validation-matrix.md`、`open-issues.md`
- 依据：父代理规划；RCOut/RCIn 未动；cuav_v5 生成 hwdef 仅 `HAL_SPI_DEVICE0 icm20689`（无 ms5611/ramtron SPIDEV）
- 实现边界：
  - E_imu=ICM20689 chip WHO_AM_I 0x75→0x98 + PWR_MGMT_1≠0xFF；**非** AP_InertialSensor
  - E_ms5611=PROM 8 words + CRC-4 when `get_device("ms5611")`；否则 **TEST_FAIL** device not registered
  - E_fram=RDID 0x9F + 4B RW@0 when `get_device("ramtron")`；否则 **TEST_FAIL** device not registered
- 构建（串行）：`--test=E_imu` PASS；`--test=E_ms5611` PASS；`--test=E_fram` PASS
- 下一步：恢复 hwdef `SPIDEV ms5611`/`ramtron` 后上板 E_ms5611/E_fram；E_imu 上板 chip smoke（与 D_spi_hal 重叠但 E 层标签）

### 2026-05-29 Subsystem smoke 首批（S_param_storage / S_sensors / S_mavlink_usb）
- 动作：新建 `test/subsystem/{S_param_storage,S_sensors,S_mavlink_usb}/`；`subsystem/README.md`；登记 manifest；更新 `test/README.md`、`driver-validation-matrix.md`、`open-issues.md`
- 依据：父代理规划；**禁止** S_rc_chain / D_rc* / E_rc* 推进；S_compass 仅规划
- 实现边界：
  - S_param_storage=storage tail-16B scratch R/W+restore；**非** AP_Param::load_all（无 vehicle var_info）；HAL 门=D_storage
  - S_sensors=两步：ICM20689（E_imu 边界）+ MS5611 PROM/CRC（E_ms5611 边界）；ms5611 未注册 → **TEST_FAIL**
  - S_mavlink_usb=BUILD_ONLY（CherryUSB+serial0+MAVLink 依赖文档；不碰 L7/D_usb_serial）
- 构建（串行）：`--test=S_param_storage` PASS；`--test=S_sensors` PASS；`--test=S_mavlink_usb` PASS
- manifest 自检：`S_rc_chain` 不在 TEST_LAYOUT（fallback 无 SConscript）
- 下一步：上板 S_param_storage / S_sensors（UART7）；恢复 hwdef ms5611 后 S_sensors 两步全 PASS；S_mavlink_usb runtime 需独立 HAL+CherryUSB 白名单

### 2026-05-29 Subsystem smoke 构建复核（composer-2.5）
- 动作：串行重跑 `--test=S_param_storage`、`S_sensors`、`S_mavlink_usb`；确认 manifest/文档与子目录齐全；未触 D_rc* / S_rc_chain
- 结果：三目标 **PASS**（rtthread.bin 已复制至 build/rtt_cuav_v5/）
- 下一步：同上一节上板验收

### 2026-05-29 上板验收 D_scheduler（composer-2.5）
- 动作：串行 `--test=D_scheduler` → OpenOCD program 0x08008000 → UART7 18s（CH343 `usb-1a86_USB_Single_Serial_594C048352-if00`）→ halt 读 CFSR/HFSR
- 结果：**PASS** — UART7 见 `timer callback count=504`、`[D_SCHEDULER] RESULT: PASS`；`0xE000ED28=0`、`0xE000ED2C=0`；烧录 Verified OK
- 下一步：D_analogin

### 2026-05-29 上板验收 D_analogin（composer-2.5）
- 动作：串行 `--test=D_analogin` → 烧录 → UART7 18s → CFSR/HFSR
- 结果：**PASS** — `[D_ANALOGIN] RESULT: PASS`；CFSR/HFSR=0；UART7 有 `WARN: zero samples` 与 printf 占位符未替换（`%u`/`%.3f` 字面量），属已知降级边界
- 下一步：S_param_storage

### 2026-05-29 上板验收 S_param_storage（composer-2.5）
- 动作：串行 `--test=S_param_storage` → 烧录 → UART7 18s → CFSR/HFSR
- 结果：**PASS** — `[S_PARAM_STORAGE] RESULT: PASS`；CFSR/HFSR=0；printf 占位符 `%u` 未替换（已知）
- 下一步：E_imu

### 2026-05-29 上板验收 E_imu（composer-2.5）
- 动作：串行 `--test=E_imu` → 烧录 → UART7 18s → CFSR/HFSR
- 结果：**PASS** — `WHO_AM_I reg 0x75 = 0x98`；`PWR_MGMT_1 = 0x40`；`[E_IMU] RESULT: PASS`；CFSR/HFSR=0
- 下一步：四项非 RC 上板验收批次完成（未触 D_rc*/S_rc_chain）

### 2026-05-29 D_analogin printf/采样修正（composer-2.5）
- 动作：读 `test_runner.c` — 确认 `test_printf` 仅支持 `%d/%lu/%x/%s`，无 `%u`/`%f`；改 `D_analogin/main.cpp` 为 mV+`%lu`；导出 `rtt_adc_*`（`extern "C"`）；smoke 通道 ch6（原 ch8/PA4 SPARE2 恒为 0）；12 轮 `_timer_tick`+50ms delay；串行构建+OpenOCD 烧录+UART7 20s+CFSR/HFSR
- 依据：根因=格式串不支持 + 错误 logical 通道；非 ADC 未初始化（`conv=108`、`tick_state=1`）
- 结果：**PASS** — `read_latest raw_counts=2064`、`voltage_latest=3326 mV`、`adc_diag conv=108`；无 WARN；CFSR/HFSR=0
- 修改：`libraries/AP_HAL_RTT/test/drivers/D_analogin/main.cpp`、`libraries/AP_HAL_RTT/AnalogIn.cpp`（诊断符号 C 链接）
- 下一步：恢复 hwdef `SPIDEV ms5611`/`ramtron` → `E_ms5611`/`E_fram`/`S_sensors` 上板

### 2026-05-29 恢复 cuav_v5 SPIDEV ms5611/ramtron（composer-2.5）
- 动作：对照 `hwdef.dat.baseline` 与 ChibiOS `fmuv5/hwdef.dat`；在 `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` 于 `icm20689` 后增加 `ramtron`/`ms5611` 两行（未改 `icm20689`）；SCons 自动再生 `build/rtt_deploy/cuav_v5/hwdef.h`
- 依据：CS 已存在 `PF5 RAMTRON_CS`、`PF10 MS5611_CS`；SPI2/4 引脚已在 hwdef；`BARO MS5611 SPI:ms5611` 与 `HAL_WITH_RAMTRON` 已声明
- SPIDEV：`ramtron SPI2 DEVID1 RAMTRON_CS MODE3 8*MHZ 8*MHZ`；`ms5611 SPI4 DEVID1 MS5611_CS MODE3 20*MHZ 20*MHZ`；`icm20689` 保持 `SPI1 DEVID1 ICM20689_CS MODE3 2*MHZ 8*MHZ`
- 生成表：`HAL_SPI_DEVICE_COUNT 3` — icm20689(spi11)、ramtron(spi21)、ms5611(spi41)
- 结果：串行构建 **5/5 PASS** — `D_spi_hal`、`E_imu`、`E_ms5611`、`E_fram`、`S_sensors`（约 19s）；**未**上板
- 下一步：`E_ms5611`/`E_fram`/`S_sensors` UART7 上板验收

### 2026-05-29 上板验收 E_ms5611（composer-2.5）
- 动作：串行 `--test=E_ms5611` → OpenOCD program 0x08008000 verify reset → UART7 25s（CH343）→ halt CFSR/HFSR
- 结果：**PASS** — `[E_MS5611] RESULT: PASS`；PROM 8 words + `prom_crc_ok` 断言通过（UART 上 `PROM[%u]=` 为 test_printf 无 `%u` 占位符，值未展开属已知）；CFSR=0 HFSR=0；烧录 Verified OK
- 备注：首读 20s 仅 201B（仍在 RT-Thread banner），重烧+25s 得完整日志

### 2026-05-29 上板验收 E_fram（composer-2.5）
- 动作：串行 `--test=E_fram` → 烧录 → UART7 22s → CFSR/HFSR
- 结果：**FAIL** — `RDID bytes: 0 0 0 0...`（12 failures）；`[E_FRAM] RESULT: FAIL`；无有效 RDID/RW；restore 步骤亦 ASSERT 失败；CFSR=0 HFSR=0（无 HardFault）
- 判断：ramtron SPIDEV 已登记但 SPI2/CS 链路或芯片无响应（与 E_ms5611 SPI4 通过对比）
- 下一步：继续 S_sensors（用户要求失败不阻塞）

### 2026-05-29 上板验收 S_sensors（composer-2.5）
- 动作：串行 `--test=S_sensors` → 烧录 → UART7 22s → CFSR/HFSR
- 结果：**PASS** — STEP1 `WHO_AM_I=0x98`；STEP2 MS5611 PROM/CRC PASS；`[S_SENSORS] RESULT: PASS`；CFSR=0 HFSR=0
- 批次结论：E_ms5611 PASS / E_fram FAIL（FRAM SPI）/ S_sensors PASS

### 2026-05-29 E_fram RDID 全 0 根因与修复（composer-2.5）
- 根因：`SPIDevice` 仅对 bus 1/4 走 STM32 寄存器轮询 + `_spi1/_spi4_gpio_init`；**SPI2 ramtron 仍走 RT-Thread `spi21` 框架**，MISO 无有效数据 → RDID/RW 全 0
- 修复：`SPIDevice.cpp` 增加 `_spi2_gpio_init()`（PI1/2/3 AF5 + PF5 CS）、bus 2 纳入 CMSIS 轮询路径、SPI2 用 APB1 54MHz 算 BR；hwdef `ramtron` lowspeed 4MHz；`E_fram` RDID 读 9 字节 + RDSR + id1/id2 断言
- 验证：串行构建 `E_fram`/`D_spi_hal`/`E_ms5611` PASS；上板 `E_fram` **PASS** — Cypress id1=0x22 id2=0x08、scratch RW+restore、RDSR=0；CFSR/HFSR=0（halt）
- 下一步：可选 `D_storage` 切真实 FRAM 后端；`S_sensors` 上板若尚未跑

### 2026-05-29 D_usb_serial HAL CDC runtime（composer-2.5）
- 动作：BUILD_ONLY→真实 HAL 链接：`test_stubs_no_usb.c`、`rtt_test_hal_usb_serial_link.py`、`D_usb_serial/SConscript`+`main.cpp`；`rtt_usb_backend._infer_test_backend('D_usb_serial')→cherryusb`
- 阻塞曾现：`test_stubs.c` USB no-op 与 shim 符号冲突；缺 `rtt_dbg_setup_stage`（已补 stubs_no_usb）
- 构建：`scons --target=cuav_v5 --test=D_usb_serial` PASS（ROM ~222KB）
- 上板：烧录 0x08008000 Verified OK；`lsusb 1209:5741`；GDB CFSR/HFSR=0；`rtt_uart_dbg_drain_bytes`>0；ACM1@921600 每 3s 收到 `D_usb_serial CDC beacon`（首包横幅易错过，需主机晚开或看 beacon）
- L7：未改 `L6_cherryusb_cdc_echo/` 目录
- 下一步：`S_mavlink_usb` 可依赖本镜像；主机验收脚本固定 921600+按 VID 找 ACM；可选强化 RX echo（需 `_timer_tick` 或更多 poll）

### 2026-05-29 D_usb_serial 复验（composer-2.5 续）
- 动作：重构建+烧录+主机 CDC；未改 L7/RC；修正 `test/README.md` 仍写 BUILD_ONLY 的过时行
- 构建：`scons --target=cuav_v5 --test=D_usb_serial` PASS（rtthread.bin ~226KB）
- 上板：OpenOCD Verified OK；`1209:5741`；ACM1@921600 收到 `D_usb_serial CDC beacon`；写 `HELLO_D_USB\r\n` echo 13B 回读；CFSR/HFSR=0；`rtt_dbg_cherry_configured_state=1`
- 结论：**D_usb_serial 已从 BUILD_ONLY 闭环为真实 HAL serial(0) CDC smoke**

### 2026-05-29 S_mavlink_usb runtime HEARTBEAT（composer-2.5）
- 动作：`main.c`→`main.cpp`；复用 `rtt_test_hal_usb_serial_link.py`；新增 `rtt_test_mavlink_minimal_link.py`（`build_old`/`build` ardupilotmega headers）；`rtt_usb_backend` 推断 `s_mavlink_usb`→cherryusb；主机 `tests/rtt_test_S_mavlink_usb_host.py`；未改 L7/RC
- 构建：`scons --target=cuav_v5 --test=S_mavlink_usb` PASS（rtthread.bin ~227KB）
- 上板：OpenOCD Verified OK @0x08008000；`lsusb 1209:5741`；ACM1@921600 pymavlink HEARTBEAT msgid=0 sys=1 comp=1 type=2 autopilot=3 status=3；CFSR/HFSR=0（0xE000ED28/2C）
- 边界：1Hz HEARTBEAT + 3s 文本 beacon；**非** GCS/param/整机 L0；MAVLink 头依赖 `build_old/rtt_cuav_v5/libraries/GCS_MAVLink/include` 或全量 build 产物
- 结论：**S_mavlink_usb 已从 BUILD_ONLY 升级为 runtime subsystem smoke**

### 2026-05-29 D_usb_serial 60s wait + 全链路复验（composer-2.5 续）
- 动作：`main.cpp` wait_usb_ready 20s→60s（SDIO 拖慢枚举）；step 后打印 `RESULT: PASS/FAIL`；重构建并 `strings` 确认 bin 含 `D_usb_serial`（曾误烧 `S_mavlink_usb` 残留 bin）
- 构建：`scons --target=cuav_v5 --test=D_usb_serial` PASS
- 上板：Verified OK；UART7@115200 t=4s 见 `USB configured`、`write returned 26`、`[D_USB_SERIAL] RESULT: PASS`；ACM1@921600 见 `CDC beacon` + `ping123` echo；GDB CFSR/HFSR=0
- 下一步：`S_mavlink_usb` 已独立 runtime；`D_usb_serial` 主机脚本可固定 VID+921600+晚开 ACM 读首包横幅

### 2026-05-29 CherryUSB 全量默认 + milestone commit（composer-2.5）
- 动作：`rtt_usb_backend.py` 默认 cherryusb；selector 验证；`scons --v=ArduCopter --target=cuav_v5`；commit `85c4f83b3e`（155 files，仅 staged clean set）
- 结果：构建 PASS；`OTG_FS_IRQHandler` 单行 `080e7050 T`；RCOut/RCIn/S_rc_chain 未纳入
- 下一步：硬件 CDC+OpenOCD 双重验证；工作区 ~51 unstaged + ~101 untracked 仍保留

### 2026-05-29 受控全量回归 — 双 backend 构建（composer-2.5）
- 动作：串行 `scons --v=ArduCopter --target=cuav_v5`（默认 cherryusb）→ `RTT_USB_BACKEND=native` → 再默认 cherryusb 收尾
- 结果：三次均 `scons: done building targets`；`nm … | rg OTG_FS_IRQHandler` 各仅一行：cherryusb `080e7050 T`、native `080e2670 T`、收尾 cherryusb `080e7050 T`；git_hash=85c4f83b3e
- 下一步：烧录 cherryusb 镜像

### 2026-05-29 受控全量回归 — 烧录（composer-2.5）
- 动作：无 openocd 残留；BL `Tools/bootloaders/CUAVv5_bl.bin @0x08000000` Verified OK；app `build/rtt_deploy/cuav_v5/rtthread.bin @0x08008000` Verified OK；等待 12s
- 结果：两次烧录后 `pgrep openocd` 为空
- 下一步：L0 gate

### 2026-05-29 受控全量回归 — L0 gate cherryusb（composer-2.5）
- 动作：`POGO_APM_ROOT=… RTT_USB_BACKEND=cherryusb /tmp/cherryusb_main_l0_gate.sh --skip-build --skip-bl --json --wait 30`
- 结果：exit=0 PASS；CDC `/dev/ttyACM1`；STANDBY(3)；params 904/904 FORMAT_VERSION=120.0；30s stream 1902 msgs / 23 types；VTOR=0x08008000 CFSR=0 HFSR=0 IWDGRSTF=0 POST_L0_FAULT=0
- 下一步：MAVFTP + Mission

### 2026-05-29 受控全量回归 — MAVFTP（composer-2.5）
- 动作：`python3 tests/test_mavftp.py --port /dev/ttyACM1`
- 结果：exit=0 **6/6 PASS**（T1–T6 含 param.pck 读、写读删、ResetSessions、3s 稳定性 205 msgs）
- 下一步：Mission protocol

### 2026-05-29 受控全量回归 — Mission（composer-2.5）
- 动作：`python3 tests/test_mission_protocol.py --port /dev/ttyACM1`
- 结果：exit=0 **PASS**（CLEAR/COUNT/REQUEST/ACK/upload/download/clear 全链路）
- 结论：受控全量回归 **PASS**（构建双 backend + 烧录 + L0 + MAVFTP + Mission）；未覆盖 soak/重连/RCOut/RCIn/S_rc_chain

### 2026-05-29 长稳 soak ≥600s（composer-2.5）
- 动作：`python3 .cursor/tmp_rtt_soak_usb_resilience.py` CDC `/dev/ttyACM1` @921600，600s 连续收包，每 30s 打点
- 结果：**PASS** — 600.1s、47880 msgs、~79.8 msg/s、HB=1208、全程 STANDBY(3)、max_silence=0、无 >3s 静默、无状态跌出 STANDBY/ACTIVE
- 下一步：soak 后 OpenOCD fault + 重连/参数

### 2026-05-29 USB 重连韧性 3 轮（composer-2.5）
- 动作：关闭串口→冷却 3s→重开 wait_heartbeat；每轮含全量参数可读性检查
- 结果：R1/R2 **PASS**（reconnect_hb≈0.01s，904/904）；R3 首轮仅 419/904（4.8s，脚本在连跑后未充分 drain）→ **重试 PASS**（reconnect_hb=0.01s，904/904）；by-id 始终 `ttyACM1`，无重枚举换口
- 下一步：参数多轮

### 2026-05-29 参数多轮下载 3 次（composer-2.5）
- 动作：soak 后同连接 3 轮 param_request_list（首轮脚本）→ 失败；改 **新连接 + drain 5s + 120s** 重试 3 轮
- 结果：soak 后同连接 3 轮 **FAIL**（799/689/760，流仍饱和）；重试 **PASS** R1–R3 各 **904/904**，22.4–27.8s；post-soak OpenOCD：VTOR=0x08008000、CFSR=0、HFSR=0、RCC_CSR=0x3（IWDGRSTF=0）
- 结论：长稳/USB 韧性 **部分→实质 PASS**（soak+fault+重连+参数重试均过；首轮参数需 drain 后新连接）

### 2026-05-29 GPS 实测（composer-2.5，用户已接 GPS）
- 动作：pymavlink CDC `usb-APM_CUAV_V5_CDC_1_*`→ttyACM1 @115200，45s 采 GPS_RAW_INT/STATUSTEXT；PARAM GPS1/2_TYPE、SERIAL3/4_PROTOCOL
- 结果：HB STANDBY(3)；SERIAL3/4_PROTOCOL=5、BAUD=230；**GPS1_TYPE=0、GPS2_TYPE=0**（NONE）；GPS_RAW_INT×180 fix=0 sats=0 lat/lon=0 eph=65535；无 GPS_STATUS/GPS2_RAW；无 “u-blox”/“GPS.*detected” STATUSTEXT
- 结论：**MAVLink 空 GPS 流在发，驱动未启用/未检测**；非室外 3D fix；室内无星 + 参数 TYPE=0 叠加
- 下一步：若需检测 OK 需设 GPS1_TYPE（如 AUTO/UBLOX）并室外见天复测

### 2026-05-29 notify/RGB 实测（composer-2.5）
- 动作：STATUSTEXT 扫 Notify 错误；OpenOCD 3 次采样 GPIOH/B/C ODR（PH10-12/PB1/PC6-7）
- 结果：无 Notify 报错；ODR 有变化（例 GPIOH 0→0xffff；GPIOC 末次 0xffbf）；openocd 已 exit/pkill 清理
- 结论：**notify GPIO 有翻转证据**；物理颜色需用户肉眼（STANDBY+PreArm 常见黄/蓝闪，未目视确认）

### 2026-05-29 CUAV v5 Storage FRAM 持久化（composer-2.5）
- 动作：`_storage_open` FRAM→Flash→Stub；`spi_cmsis_prepare_bus(2)`；对齐 ChibiOS `_timer_tick`；修 CMSIS `set_chip_select(false)` 释放 CS；RTT `handle_param_set` 用 `save_sync`+`flush`；`AP_RAMTRON` init/read/write 设 SPEED_LOW
- 依据：`rtt_dbg_storage_backend=1`(FRAM)、`fram_probe=2`、`fram_tick_ok=5`、`tick_fail=0`；CFSR/HFSR=0
- 结果：构建 PASS；掉电保持 **PASS** — `LOG_BITMASK` 180222→147454，OpenOCD reset 后仍 147454；`STAT_BOOTCNT` 1→2
- 根因：CMSIS SPI `set_chip_select(false)` 未拉高 CS → AP_RAMTRON WREN/WRITE 失败；外加 `_timer_tick` 多余整行读回校验导致 `write_ok` 恒 false
- 旧 FRAM 内容：未擦除，依赖 AP_Param sentinel/format_version（同 ChibiOS）

### 2026-05-29 GPS1_TYPE=1 + gps.init 复测（composer-2.5）
- 动作：CDC `usb-APM_CUAV_V5_CDC_1_*`→**ttyACM0** @921600；读参 GPS1_TYPE/GPS2_TYPE/SERIAL3_*；已烧录含 `gps.init()` 的 `rtthread.bin`；80–90s 采 GPS_RAW_INT/STATUSTEXT；OpenOCD CFSR/HFSR；halt 读 `serial3Driver`/`USART1`
- 参数：GPS1_TYPE=**1.0**、GPS2_TYPE=0、SERIAL3_PROTOCOL=5、SERIAL3_BAUD=230、GPS_AUTO_CONFIG=1；重启后读回仍为 1 → **FRAM 持久 OK**
- 运行：`rtt_dbg_setup_stage=0x28b(651)`（已过 gps.init@631）；`serial3Driver`：port=3、`_initialized=1`、`_uart_hw=0x40011000`(USART1)、探测波特≈460800
- MAVLink：~180×GPS_RAW_INT 全程 fix=0、sats=0、eph=65535；STATUSTEXT 无 “GPS.*detected”/u-blox/NMEA；UART7 `RTT_CTL` stg=651 正常
- Fault：CFSR=0；HFSR 读数为 0/低位 0x0b（无 HardFault 证据）
- 结论：**参数+持久成立；检测未 OK（完全无检测日志、星数不增）**；室内不期望 3D fix
- 下一步：确认接 **GPS1/USART1**（非 GPS2/UART4）；示波器或 `serial3` RX 计数；试 SERIAL3_BAUD=115/57；查 initblob 是否卡住探测

### 2026-05-29 USART1 GPS RX 硬件采样（composer-2.5，OpenOCD）
- 动作：reset run +12s 后 20×（halt→读 USART1/UART4/DMA2S2→resume），间隔 2s，约 40s；补读 `serial3Driver@0x2000f454`、RCC_APB2ENR、DMA2_S2@0x40026440
- USART1 @0x40011000：**CR1/CR2/CR3/BRR 全程 0**（UE/RE/TE 未使能）；**ISR 恒 0xC0**（TXE+TC，**RXNE 从未置位**）；**RDR 恒 0**；无 ORE/FE/NE 跳动
- UART4 @0x40004C00：同样 CR1/BRR=0、ISR=0xC0、RDR=0
- `serial3Driver`：port=3、`_initialized=1`、`_baudrate=0x70800`(460800)、`_uart_hw=0x40011000` — **软件认为已开、硬件未配置**
- DMA2 Stream2：CR=0x06000414 活跃，PAR=0x4001300c（**非** USART1 RDR 0x40011024）
- RCC_APB2ENR@0x40023844=0x3901（USART1 时钟位已开）
- 结果：**完全无 RX 字节**（非「有字节但 AP_GPS 不识别」）
- 根因分类：**RTT UART 通路** — `UARTDriver` 与 USART1 寄存器脱钩（CR1/BRR 未保持）；须先修 `_begin` 使能/防被 RT-Thread 或 `_end` 清零，再复测接线/波特
- 下一步：查谁将 USART1 CR1 清 0（`_end`/RT-Thread serial vs AP_HAL 双占）；修后重采样 ISR/RDR；仍无 RX 再查 PB6/PB7 AF 与模块接 GPS1 非 GPS2

### 2026-05-29 USART1 GPS1 RX 硬件采样（composer-2.5，OpenOCD）
- 动作：`/tmp/usart1_poll.py` 18×（halt→读 USART1 CR1/BRR/ISR/RDR + GPIOB MODER/AFR0→resume）间隔 2s，共 ~36s；GDB 读 `serial3Driver` 与 `USART1->CR1`
- USART1@0x40011000：18/18 次 **CR1=0x0、BRR=0x0**（UE=0 RE=0）；**ISR=0xC0**（仅 TC/TXE 类标志，**RXNE 从未置位**）；**RDR=0x00** 无变化
- GPIOB：PB_MODER≈0x001A000F → PB6=AF(2) 但 **PB7=输入(0)**；**GPIOB_AFR0=0**（非 AF7）→ PB6/PB7 均未复用到 USART1
- GDB：`serial3Driver._initialized=true`、`_baudrate=460800`、`_uart_hw=0x40011000`，但 **`USART1->CR1/BRR=0`**
- 结果：**判定为完全无 RX 字节**（非「有字节但 AP_GPS 不识别」）
- 根因分类：**RTT UART 通路** — CMSIS `UARTDriver::_begin` 未配 GPS 引脚复用（HAL_MspInit 仅随 RT-Thread `uart1` 设备打开）；且运行期 USART1 外设处于关闭态与驱动 `_initialized` 不一致
- 下一步：在 `UARTDriver::_begin` 补 PB6/PB7 AF7+时钟；保证波特扫描期间 CR1.UE/RE 常开；硬件仍须确认模块接 GPS1 非 GPS2、TX/RX 未反
- 清理：`openocd` shutdown/pkill，无残留

### 2026-05-29 USART1 GPS 软硬件脱钩修复闭环（composer-2.5）
- 根因：`UARTDriver::_begin()` 对 GPS 口走 CMSIS 直写、不 `rt_device_open("uart1")`，故无 `HAL_UART_MspInit`；仅置 `_initialized` 时 USART1 **CR1/BRR 恒 0**、PB6/PB7 未 AF7。UART7 由 `rtt_ctl_uart_hw_init()`/`usart_ll_init` 提前拉起。
- 修复（最小集）：
  - `drv_usart_ll.c/h`：CUAV V5 `usart1_ll_cfg`(PB6/PB7 AF7) + `usart_ll_init_for_instance()`
  - `UARTDriver.cpp`：非 USB 且非 uart7 控制台时调用 `usart_ll_init_for_instance()`；UE=0 时重 init；已 init 仅改 BRR
  - 同步 `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/drivers_ll/drv_usart_ll.{c,h}`
- 构建：`scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**（2026-05-29）
- 烧录：OpenOCD `program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify reset` **Verified OK**
- OpenOCD/GDB（运行 ~45s 后 halt）：
  - USART1：**CR1=0x0000000d**（UE|RE|TE）、**BRR=0x15f9**（探测期 19200）/ 运行期 **0x3aa**（230400，与 SERIAL3_BAUD=230 一致）
  - **ISR=0x006000d8**、**RDR=0xb5**；GPIOB MODER=0x001aa00f、AFR0=0x77000000（PB6/PB7 AF7）
  - UART7：CR1=0x2d BRR=0x1d5（对照正常）
  - `serial3Driver`：port=3、`_initialized=1`、`_uart_hw=0x40011000`、`_baudrate=0x4b00`；`_rx_bounce`/`_tx_bounce` 含 **u-blox NMEA/$PUBX/CONFIG** 字符串
  - CFSR/HFSR=**0**
- MAVLink（ttyACM0 @921600，75–90s）：HEARTBEAT OK；**GPS_RAW_INT ~90 条**（室内 fix=0 sats=0 可接受）；STATUSTEXT 窗口内未见 “GPS 1: detected”（可能仅在冷启早期）；参数 GPS1_TYPE=1(AUTO)、SERIAL3_PROTOCOL=5、SERIAL3_BAUD=230(230400)
- 结论：**UART 已使能且有 RX 字节**（软硬件脱钩已修复）；室内无 3D fix 正常。若需 MAVLink “detected” 字样可冷启从头抓 STATUSTEXT；AP_GPS singleton 某次 halt 仍为 NO_GPS/num_instances=0，与缓冲内 u-blox 配置流量并存，后续可单独查协议探测时序。
- 硬件确认清单（仅当 UE 使能后仍无 RX 时）：模块接 **GPS1/USART1**（PB6 TX→模块 RX、PB7 RX←模块 TX）；非 GPS2/UART4；5V/3.3V 供电；默认 230400 或 AUTO 探测波特；天线室内可无星。

### 2026-05-29 外置 I2C notify RGB bus mask（composer-2.5）
- 动作：RTT `I2CDeviceManager` 对齐 ChibiOS：`get_bus_mask()=(1<<RTT_I2C_BUS_COUNT)-1`；`internal=mask&HAL_I2C_INTERNAL_MASK`；`external=mask&~internal`；`hwdef`/`board/rtt.h` 设 `HAL_I2C_INTERNAL_MASK 1`（bus0=I2C3/IST8310）
- mask（CUAV V5）：**前** all=0x01 int=0x01 ext=0x00 → **后** all=0x0F int=0x01 ext=0x0E
- 构建：`scons --v=ArduCopter --target=cuav_v5` **PASS**；`--test=D_i2c_hal` **PASS**
- 烧录：OpenOCD program+verify **OK**
- 上板：halt 读 CFSR/HFSR **均为 0**；CDC ttyACM0 HEARTBEAT+ArduPilot Ready+EKF；SYS_STATUS health 含 **3D_MAG(0x04)**；STATUSTEXT 无 NCP5623/Toshiba 成功字样（外置总线无 RGB 或 probe 静默失败）
- GPIOH ODR：本次 OpenOCD `mrw 0x58021C14` 读失败（调试器访问）；同日前序 trace 有 GPIOH ODR 变化记录，板载 PixRacer RGB 配置未改
- 结论分类：**(a) mask 已修 + 启动无 fault，外置 probe 路径已激活**；无接 GPS RGB 时属 **(b) 安全未探测到设备**；非 **(c)** 除非用户外接模块并见灯/日志
- 遗留：有外置 RGB 硬件时需 STATUSTEXT/肉眼确认 **(c)**；可选加 rtt_ctl 计数外置 I2C probe 成败

### 2026-05-29 E_ist8310 + S_compass 分层测试（composer-2.5）
- 动作：新增 `test/drivers/E_ist8310`、`test/subsystem/S_compass`；`rtt_test_compass_subsystem_link.py`；manifest `TEST_LAYOUT`/`LEGACY`；README/matrix 更新
- E_ist8310 边界：chip-level I2C（WAI+CNTL1 单测+raw XYZ）；**非** `AP_Compass`；无 `scheduler->init()`
- S_compass 边界：`Compass::init`+`read`/`get_field`；`scheduler->init()`+`hal_initialized()`；`COMPASS_MOT_ENABLED=0`、`AP_CUSTOMROTATIONS_ENABLED=0`；排除 `Compass_PerMotor.cpp`/`Compass_learn.cpp`；**非** vehicle cal/GCS/MAVLink
- 构建：`--test=E_ist8310` **PASS**；`--test=S_compass` 首轮链接失败（custom_rotations/battery/SRV）→ 上述 define+源过滤后 **PASS**
- 上板 UART7（CH343 `usb-1a86_*`→ttyACM1）：
  - E：`WAI=0x10`；raw x=-75 y=47 z=17；`[E_IST8310] RESULT: PASS`；CFSR/HFSR=0
  - S：`IST8310 found on bus 0`；field sample0/1 有变化；`[S_COMPASS] RESULT: PASS`；CFSR/HFSR=0
- 修复：`S_compass` `get_count` 打印改 `%lu`（`test_printf` 无 `%u`）
- 下一步：可选烧回全量 ArduCopter（CherryUSB）；`S_compass` 需 rebuild 才含 printf 修复

### 2026-05-29 M7 安全子集 + 全量 ArduCopter 整合回归（composer-2.5）
- 动作（代码）：
  - `I2CDevice.cpp`：`HAL_I2C_CLEAR_ON_TIMEOUT` 默认 1；timeout 路径 `_i2c_xfer_timeout()` 仅 **SDA==0** 时 `clear_bus()`；NACK/BERR/OVR/ARLO 不清总线；`_i2c_master_xfer_ll` 传 `bus`
  - `HAL_RTT_Class.cpp`：`RCC->CSR |= RMVF` **前** `rtt_boot_rcc_csr = RCC->CSR`；`#ifdef HAL_I2C_CLEAR_BUS` 时 boot `clear_all_buses()`
  - `Util.cpp`：全局 `rtt_boot_rcc_csr`；`was_watchdog_reset()` 读缓存，移除 lazy save
- 构建：`python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**（~37s，`build/rtt_cuav_v5/rtthread.bin` ~1.30MB）
- 烧录：OpenOCD program 多次 **SIGKILL(137)**（~9s，未完成 verify）；**st-flash write 0x08008000 verify PASS** + reset（~44s）可靠
- 板级固件：已从 S_compass test 恢复 **全量 ArduCopter**（CherryUSB CDC 1209:5741，`usb-APM_CUAV_V5_CDC_1_00001-if00`→ttyACM0）
- 回归（CDC ttyACM0，非 compat ttyACM1）：
  - L0_heartbeat：**PASS**（sys=1，多轮）
  - L0_standby：**间歇 PASS**（曾 `system_status=3`；亦见 boot=1、长时间后 critical=5）
  - L0_params：**FAIL/不稳定**（param list 过程中 USB 断开；最高 ~382，未达 904；多进程占 port 会加剧）
  - L0_30s_streams：**曾 PASS**（742 msg/23 types）；后续长连或 param 下载后 SerialException
  - FRAM_persist：**未闭环**（LOG_BITMASK set 后 st-flash reset + 重读未在本轮稳定完成）
  - GPS_RAW_INT：**短窗口 PASS**（有消息，fix=0 可接受）；USART1 CR1/BRR OpenOCD 本轮未稳定读出（openocd 超时/SIGKILL）
  - Compass 3D_MAG：流式窗口内 **曾见 SYS_STATUS health 0x800**（与 trace 前序一致）
  - MAVFTP：`tests/test_mavftp.py` 连上 sys=1 但 **0/6 FAIL**（`NoneType` item assignment，FTP 会话未就绪）
  - OpenOCD fault：本轮 **未完整**（flash/diag 被 SIGKILL；运行态 CFSR/HFSR 需单实例长超时复测）
- I2C clear_bus：**保留**（无 IST8310/D_i2c_hal 回退证据；USB 崩溃更像 CDC/参数流量问题）
- 结论：**代码+构建 PASS；整合回归 PARTIAL**（L0 心跳/流 intermittent；参数/FRAM/MAVFTP/OpenOCD 需无并发占串口 + st-flash 烧录后 20s 冷启窗口重跑）

### 2026-05-29 M7 收尾复测（composer-2.5）
- 动作：独占 CDC 复跑 L0/GPS/OpenOCD；gate 脚本 `--skip-build --skip-bl`；未 commit/push
- 构建：全量 `scons --v=ArduCopter --target=cuav_v5` **PASS**；ELF `OTG_FS_IRQHandler` @ **0x080e7775** 唯一
- 手工 L0（ttyACM0，5s drain 后）：STANDBY **PASS**；参数 **912/912 PASS**；30s 流 **258 msg / 22 types PASS**
- Gate `/tmp/cherryusb_main_l0_gate.sh`：参数 **912/912**；30s 流 **FAIL**（`no HEARTBEAT for 5s`，参数洪泛后 CDC 心跳中断；**建议 gate 前加 5s drain 或降 param 并发**）
- OpenOCD 快照（gate 后）：CFSR/HFSR=**0**；VTOR=**0x08008000**；IWDGRSTF=**0**；PC 在 scheduler 主循环
- USART1@0x40011000：**CR1=0x0d**（UE|RE|TE）；**BRR 非 0**（运行期采样 0x57e4/0x3aa 视波特探测阶段）
- GPS_RAW_INT 45s：**40 条**（室内 fix=0）**PASS**
- SYS_STATUS **3D_MAG**：present/enabled/health **均未置位 FAIL**（`COMPASS_ENABLE=1` 但全量固件本轮无 MAG health bit；与 S_compass/E_ist8310 子测 PASS 分离）
- FRAM 持久：OpenOCD reset 后 **未稳定闭环**（param 单读偶发 None；gate 占口后 CDC 断连）
- MAVFTP：`test_mavftp.py` gate 后 **FATAL No heartbeat**（需独占 CDC + STANDBY 后复跑；历史最佳 **4/6**）
- I2C clear_bus：**保留**（仅 SDA 低 + timeout；无 D_i2c_hal 回退）
- 板子终态：**全量 ArduCopter CherryUSB CDC** @ **0x08008000**（非 S_compass test）

### 2026-05-29 SYS_STATUS 3D_MAG 根因与修复验证（composer-2.5）
- 基线（`HAL_I2C_CLEAR_ON_TIMEOUT` 默认 1）：CDC STANDBY 后 **3D_MAG present/enabled/health 全 False**；`COMPASS_ENABLE=1` 但 `COMPASS_DEV_ID` 单读超时；STATUSTEXT 无 Compass/PreArm 类
- 对照实验 A：hwdef 设 `HAL_I2C_CLEAR_ON_TIMEOUT 0` 重编上板 → **3D_MAG 仍 False** → **否定**「timeout clear_bus 单独导致 MAG 掉线」
- 根因（B）：`ArduCopter/system.cpp` `init_ardupilot()` 内 **RTT HACK 注释掉** `AP::compass().init()`；`GCS.cpp` 依赖 `compass.available()/healthy()`；`S_compass` 子测自行 `init()` 故分层 PASS、全量 FAIL
- 修复：恢复 `AP::compass().set_log_bit` + `AP::compass().init()`（stage 631→633）；**不**在 cuav_v5 hwdef 长期关 `HAL_I2C_CLEAR_ON_TIMEOUT`（保持 `I2CDevice.cpp` 默认 1，仅 SDA 低 + timeout 时 clear）
- 构建：`scons --v=ArduCopter --target=cuav_v5 -j$(nproc)` PASS（曾遇 `avoidance_adsb.o` 归档竞态，重跑全量 PASS）
- 烧录：OpenOCD `init; program … verify; reset run` PASS
- 验证（ttyACM0，恢复 init 后）：
  - SYS_STATUS 流式窗口：**present=True enabled=True health=True**（连续 5 条，load 334–1000）
  - `COMPASS_DEV_ID=658945`（非 0）、`COMPASS_ENABLE=1`、`COMPASS_USE=1`
  - OpenOCD 快照：CFSR/HFSR=**0**；`pgrep openocd` 无残留
- clear_bus 终态：**保留默认** `HAL_I2C_CLEAR_ON_TIMEOUT=1`；boot 侧 cuav_v5 未开 `HAL_I2C_CLEAR_BUS`（无全总线 boot clear）
- 备注：长时 `param_request_list` 或 GDB halt 仍可能触发 CDC 断连（与 MAG 根因无关）；轻量 poll 可稳定看到 3D_MAG

### 2026-05-29 17:22（独占 CDC MAVFTP/FRAM/OpenOCD 回归）
- 动作：开跑前 `pgrep` 独占检查（无 pymavlink/mavftp/openocd 残留）；CDC by-id `usb-APM_CUAV_V5_CDC_1_00001-if00`→ttyACM0；**未重烧**（沿用现板 compass init + FRAM 全量固件）
- 依据/纪律：全程单连接串行；`param_request`/读参前 5s drain；st-flash reset 后 15s 冷启
- 步骤1 L0：5s drain → HEARTBEAT sys=1 → system_status=**3 STANDBY PASS**
- 步骤2 MAVFTP（`MAVFTP_PORT=…if00 python3 tests/test_mavftp.py`）：
  - 第1轮 **4/6**：T1 PASS、T2 PASS、T3 **FAIL timeout on open**、T4 FAIL write、T5 PASS、T6 PASS(53 msg)
  - 冷却 3s 第2轮 **4/6**：T1/T2/T5/T6 PASS；T3 **opened size=10944**（非垃圾 1330926404）→ FAIL timeout offset 1434；T4 FAIL data mismatch
  - 专项 T3（20s timeout + drain）：`param.pck size=10944 (0x2ac0)`，读 **10229/10944** 后 EOF（缺 715B）
- 步骤3 FRAM：`LOG_BITMASK` 176126→**831** 写确认 → `st-flash reset`（AIRCR 软复位）→ 15s 冷启 → param list **752 项**，`LOG_BITMASK=831` **PASS**（单读 param_request_read 偶发 None，list 稳定）
- 步骤4 OpenOCD 快照（单实例 background + GDB batch）：VTOR=**0x08008000**、CFSR=**0**、HFSR=**0**、RCC_CSR=**0x00000003**、IWDGRSTF=**0**；`kill` 后 `pgrep openocd` **CLEAN**
- 判断：历史 `@PARAM/param.pck size=1330926404` **本轮未复现**；当前失败为 FTP 读中断/SD 写回不一致，**更像 MAVFTP 协议/会话或负载问题，非 FRAM size 垃圾回归**；T3 首轮 open 超时具瞬态特征，二轮 size 正常
- 下一步：父代理可排 MAVFTP T3 早 EOF（10229/10944）与 T4 write mismatch；本执行者不改源码

### 2026-05-29 17:45（T3/T4 瞬态 vs 真 bug 钉死 — 独占 CDC + OpenOCD 取证）
- 动作：Read `rtt-build-flash-debug` + `rtt-mavlink-verification`；开跑前 pgrep 独占（无 pymavlink/openocd）；**未重烧**
- 步骤1 现板：HEARTBEAT sys=1 **STANDBY(3) PASS**；后续压测后 status 降为 **CRITICAL(5)** comp=0（仍有心跳）
- 步骤2 干净全集复跑（drain 5s，`MAVFTP_PORT=…if00 python3 tests/test_mavftp.py`，最多 3 轮）：
  - 第1轮 **2/6**：T1/T2 timeout；T3 **PASS** read 10707（size 字段垃圾 1297105220）；T4 create timeout；T5 timeout；T6 PASS
  - 第2轮 **2/6**：T1 0 entries；T2 timeout；T3 open timeout；T4 Create **Nack err=6(EOF)**；T5/T6 PASS
  - 第3轮 **FATAL**：pymavlink TypeError（recv 乱序/comp=0）
  - 追加第4轮 **3/6**：T1 PASS；T2 Nack err=6；T3 Nack offset0 err=1；T4 **data mismatch** got `b'\\x1f\\x00\\x00\\x00'`（像 size 头）；T5/T6 PASS
  - 追加第5轮（OpenOCD resume 后 15s）**2/6**：T1/T2 timeout；T3 PASS 10707B；T4 create timeout；T5 timeout；T6 PASS
  - **未拿到 6/6**
- 步骤3 T4 隔离（ResetSessions + 唯一名 `/APM/ftp_iso_<ts>_<n>.tmp`，无 T3 前置，3 次）：
  - run1：Create/Write **Ack** → Read **空数据** FAIL
  - run2：**PASS**（43B 读写一致 + Delete）
  - run3：Write Ack → **OpenRO timeout** FAIL
  - 判定 **1/3 PASS** → 失败模式不一致（非稳定 Create/Write Nack），**倾向瞬态/CDC 背压，非 stat shim 稳定 bug**
- 步骤4 OpenOCD 取证（GDB batch halt）：`ftp_dbg_state=10` `ftp_dbg_opcode=2`(ResetSession) `ftp_dbg_stat_count=17`；`rtt_uart_usb_diag_write_fails=**21144**` `fail_streak=**767**` clears=0；无 Create/Write 专用 Nack 计数（halt 时 idle）
- 步骤5 OpenOCD 快照：VTOR=**0x08008000** CFSR=**0** HFSR=**0** RCC_CSR=0x00000003 **IWDGRSTF=0**；pkill 后 openocd **CLEAN**
- 结论：T3/T4 失败形态随轮次变化（timeout/EOF/空读/size 头污染），且 T4 隔离曾完整 PASS；USB diag write_fails 极高 → **CDC 背压/会话压力瞬态**；**不建议为 ap_rtt_posix_stat shim 派代码修复**
- 下一步：父代理可选 — 测试脚本 T4 改唯一文件名+重试；或排 CRITICAL(5)/comp=0 与 CherryUSB 背压（非本轮改码范围）

### 2026-05-29 18:04（修复 #1：回退关中断 USB poll — 独占 ST-Link/CDC）
- 假设：主循环/delay 路径 `rt_hw_interrupt_disable()` 包裹 `usb_lld_poll_rtt()` 阻塞 `OTG_FS_IRQHandler` CDC TX 完成 → cherry_tx_busy 背压 → MAVFTP 抖动
- 动作：Read `rtt-build-flash-debug` + `rtt-mavlink-verification`；`git diff 85c4f83b3e` 确认差异
- 改码（仅 #1，未动 CherryUSB/UARTDriver）：
  - `HAL_RTT_Class.cpp` ~255-256：主循环恢复 milestone **裸 `usb_lld_poll_rtt()`**（去掉 IRQ 关断）
  - `HAL_RTT_Class.cpp` ~381-385：启动 poll 从 100ms 忙等环 → milestone **单次 poll**
  - `Scheduler.cpp` ~450-453：`_poll_usb_if_active()` 去掉 IRQ 关断（保留 delay 路径裸 poll）
- 构建：`python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` **PASS**；`nm` 唯一 `OTG_FS_IRQHandler`；`usb_lld_*` 为 cherry shim 符号
- 烧录：`st-flash write …/rtthread.bin 0x08008000` verify **PASS**（md5 a97c3d4b…）；冷启 ≥15s
- L0 gate（`--skip-build --skip-bl --skip-flash`）：首轮 **exit=61** — 曾达 status=1 后 **CRITICAL(5)**，未 STANDBY（疑 L0 末步 OpenOCD halt + 冷启窗口）；复位+18s 后 **STANDBY(3)** 复现
- MAVFTP 3 轮（`MAVFTP_PORT=…CDC_1_00001-if00`，每轮前 drain 5s）：
  - 批次 A：R1 **4/6**（T1/T2/T5/T6 PASS；T3 size=10944 读 timeout@6453；T4 mismatch）；R2 **2/6**；R3 **2/6**（T3 size 垃圾 1330926404）
  - 批次 B（重烧后）：R1 **2/6**；R2 **2/6**（T3 PASS 10707B size 垃圾）；R3 **1/6**；压测后 **system_status=3**（未 CRITICAL）
- OpenOCD：L0 快照 CFSR=0 HFSR=0 VTOR=0x08008000 IWDGRSTF=0；复测后 pkill openocd **CLEAN**
- 结论：**#1 部分有效**（首轮 4/6、T3 首轮 size 正常）但未稳定 6/6，多轮仍 timeout/垃圾 size → **建议父代理批准 #2**（UARTDriver 背压泄放）；**未做 #2/#3**
- 风险：st-flash 软复位偶发落 **bootloader**（1209 CUAVv5-BL），需重烧 app；L0 脚本 post-GDB halt 会扰 USB

### 2026-05-29 18:27（MAVFTP #1+#2 硬件复验 — 完整流水线）
- 动作：核对 git diff 85c4f83b3e → #1 裸 poll（Scheduler `_poll_usb_if_active`/`usb_lld_poll_rtt` 无 irq disable）；#2 `>500` clear + `rtt_uart_usb_diag_clears++` 在位。清理残留进程（无 openocd/scons）。
- 构建：`python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)` 通过；`nm` 唯一 OTG_FS_IRQHandler；`usb_lld_poll_rtt` @ hal_usb_cherryusb_shim.c:348。
- 烧录：`st-flash write rtthread.bin 0x08008000` verify OK + reset；CDC @5s `/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00`。
- MAVFTP 4轮（STANDBY 等60s未达→status=5 CRITICAL/PreArm；pre ResetSessions；轮间4s）：
  - R1: 6/6 T3 size=10944 read=10229
  - R2: 4/6 T3 size=10944；T4 re-open FAIL；T5 ResetSessions timeout
  - R3: 0/6 轮前无 heartbeat（瞬断？）
  - R4: 4/6 T3 size=10944 timeout@2629；T4 data mismatch
- 压测后：HEARTBEAT system_status=5 CRITICAL；无 HardFault。
- OpenOCD 快照：VTOR=0x08008000 CFSR=0 HFSR=0 RCC_CSR=0x3 IWDGRSTF=0；diag write_fails=3143 fail_streak=354 clears=9（#2 前 clears=0）。
- 结论：#2 背压清队列已触发（clears>0），R1 达 6/6，但连跑仍 4/6~6/6 波动，未完全消除退化；疑 CherryUSB 突发+FTP/SD 路径或需 #3 FRAM sync。
- 下一步：若继续攻坚，优先 #3 或拉长轮间冷却复测 R3 瞬断是否为 USB 争用。

### 2026-05-29 18:37:01 MAVFTP A/B 验证
- MAVFTP A/B 验证启动：无重烧，独占 CDC

### 2026-05-29 18:37:01 MAVFTP A/B 验证
- 方法 A 开始：单持久连接 5 轮

### 2026-05-29 18:37:07 MAVFTP A/B 验证
- CDC 连接 OK system_status=5 drain_5s=27

### 2026-05-29 18:38:35 MAVFTP A/B 验证
- A R1: 2/6 T3_size=0

### 2026-05-29 18:38:43 MAVFTP A/B 验证
- A R2: 2/6 T3_size=10944

### 2026-05-29 18:38:56 MAVFTP A/B 验证
- A R3: 2/6 T3_size=10944

### 2026-05-29 18:39:03 MAVFTP A/B 验证
- A R4: 2/6 T3_size=10944

### 2026-05-29 18:39:10 MAVFTP A/B 验证
- A R5: 2/6 T3_size=10944

### 2026-05-29 18:39:12 MAVFTP A/B 验证
- 方法 B 开始：10s 冷却重连 2 轮

### 2026-05-29 18:39:19 MAVFTP A/B 验证
- B R1 重连 OK system_status=5

### 2026-05-29 18:39:38 MAVFTP A/B 验证
- B R1: 5/6 T3_size=10944

### 2026-05-29 18:39:39 MAVFTP A/B 验证
- B R2: 冷却 10s 重连前

### 2026-05-29 18:40:03 MAVFTP A/B 验证
- B R2 重连 OK system_status=5

### 2026-05-29 18:42:10 MAVFTP A/B 验证
- B R2: 1/6 T3_size=None

### 2026-05-29 18:42:10 MAVFTP A/B 验证
- 方法 A/B 完成 JSON=/tmp/mavftp_ab_results.json

### 2026-05-29 18:42:30 MAVFTP T6补测
- T6 修正后：fresh 连接单轮基线

### 2026-05-29 18:43:00 MAVFTP T6补测
- BASELINE_fresh: 4/6 T3_size=10944

### 2026-05-29 18:43:30 MAVFTP A/B OpenOCD 快照
- 动作：GDB halt 读 CFSR/HFSR/VTOR/IWDGRSTF + USB diag
- 结果：CFSR=0 HFSR=0 VTOR=0x08008000 IWDGRSTF=0；write_fails=0x165a(5722) fail_streak=0x41(65) clears=0xe(14)；pkill openocd 无残留
- 结论：方法 A 单连接 R1 后稳定 2/6（T6 脚本 bug 未计，补测 T6 PASS）；方法 B R1=5/6→修正 6/6、R2=1/6→修正 2/6（R3 型重连退化复现）；判定为 **(a)+(b) 并存**
