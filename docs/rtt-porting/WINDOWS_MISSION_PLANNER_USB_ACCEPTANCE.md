# Windows / Mission Planner USB 验收说明

本文用于验证 RTT ArduPilot 在 Windows 下的 USB CDC 连接问题，重点覆盖 Mission Planner 连接、参数下载、以及双 CDC 中 MAVLink/SLCAN 的接口区分。

完成状态矩阵见 `docs/rtt-porting/WINDOWS_USB_COMPLETION_MATRIX.md`。该矩阵
明确区分 Linux/固件侧已证明的项目和仍缺 Windows/Mission Planner 实机证据
的项目。

## 当前固件 USB 身份

RTT app 默认使用 ArduPilot/ChibiOS 双 CDC 的官方复合设备 PID。
`1209:5740` 对应 composite serial，`MI_00` 是 MAVLink，`MI_02` 是
SLCAN。`1209:5741` 在正式双 CDC 验收里视为 bootloader/单 CDC 旧路径；
只有明确刷入 `windows_mavlink_only.dat` 诊断固件时，才把 `1209:5741`
解释为临时 MAVLink-only 单 CDC 诊断身份。

| 项目 | 值 |
| --- | --- |
| VID/PID | `1209:5740` |
| Product | `CUAVv5` |
| Serial | STM32 UID, observed `2B0039000351383439353636` |
| bcdDevice | `2.00` |
| Device class | `EF/02/01`，IAD 复合设备 |
| MAVLink CDC | `MI_00` |
| SLCAN CDC | `MI_02` |
| CDC notification EP size | `16` bytes，和 ArduPilot ChibiOS 双 CDC 对齐 |
| Interface strings | `MI_00` / `MI_02` 的 IAD/control/data interface 索引默认是 `0` |
| String table | 默认双 CDC 只保留 ChibiOS 的 0-3 号字符串：language / manufacturer / product / serial |

默认双 CDC 固件下，旧的 `1209:5741` 是 bootloader/单 CDC 身份。如果
Windows 只看到 `1209:5741`，说明 Windows 没有枚举到当前 RTT app 的官方
双 CDC 设备。
如果 Windows 只显示一个名为 `ArduPilot` 的 COM 口，不要只看显示名，必须
用本页脚本确认它的硬件 ID 是否为 `VID_1209&PID_5740&MI_00`。只显示一个
COM 也可能是 `MI_02` / SLCAN 被绑定而 `MI_00` 未绑定；Mission Planner
连接这种端口一定不会成功。

当前默认双 CDC 已回到 ChibiOS 形态：`bcdDevice=2.00`，`MI_00` / `MI_02`
的 IAD/control/data interface 字符串索引都置为 `0`，端点顺序和包长与
ChibiOS 双 CDC 一致。默认双 CDC 的字符串表也已裁剪到 ChibiOS 双 CDC
一致，只保留 language、manufacturer、product、serial 四项，不再暴露未引用
的 `MAVLink CDC` / `SLCAN CDC` 字符串。序列号当前从 STM32 96-bit UID
动态生成，和 ChibiOS `%SERIAL%` 语义一致，USB product 也收敛到 ChibiOS
`CUAVv5` 默认产品名，用于避免 Windows 继续复用旧 `RTT5740*` 固定序列号和
RTT 诊断产品名绑定缓存。
Windows 侧最终判定仍以 `target_usb_topology.json`、
`classified_flight_controller_com_ports.json` 和 MAVLink gate 的
raw-byte/heartbeat/参数下载结果为准。

从系统层面再对照一次 ChibiOS，RTT 当前也把 USB 断开/重新枚举节奏收敛到
ChibiOS-like 路径：ChibiOS 在 `usbDisconnectBus()` 后只等待约 `1.5ms`
就继续 `usbStart()` / `usbConnectBus()`；RTT 默认双 CDC 固件现在使用
`RTT_USB_PREINIT_DISCONNECT_US=1500`，仍保留同一个 `1209:5740`、`MI_00`
MAVLink、`MI_02` SLCAN 描述符形态。这样可以避免旧的长预初始化断开窗口让
Windows 继续复用 bootloader/旧单口设备实例。若需要反向 A/B，
`windows_long_usb_disconnect.dat` 可以恢复旧的 `750ms` 窗口，但它只作为
诊断对照，不是当前默认目标。

当前默认固件还专门补了两层时序兜底：MAVLink CDC 收到 `GET_LINE_CODING`、`SET_LINE_CODING`、`SET_CONTROL_LINE_STATE` 或 MAVLink OUT 数据时，会视为主机已经打开或正在探测 `MI_00`，并立即 kick TX；同时 CherryUSB 的标准接口请求路径现在会像 ChibiOS 一样，对重复的 `SET_INTERFACE(alt=0)` 直接 ACK，不再重置既有端点。这样即使 Mission Planner 打开 COM 时没有先拉高 DTR，或 Windows 在 composite CDC 绑定时发了重复的 `SET_INTERFACE(0)`，RTT 也不会把已经准备好的 MAVLink/SLCAN 端点拆掉。固件快照还会记录 `SET_CONTROL_LINE_STATE` 的 RTS 状态，和 DTR、line coding、bulk IN/OUT、`SET_INTERFACE` 计数一起判断 Windows 是否真正打开了 MAVLink CDC。当前 UID-serial / ChibiOS string-table 固件已经完成构建、静态描述符 gate、OpenOCD 烧录和 Linux 实机闭环；Windows/Mission Planner 实机证据仍缺失，不能据此宣布最终完成。

## 当前同步后的 Goal

本轮 goal 的验收口径按 ChibiOS 行为和 Windows 现场证据闭环，不再用 Linux
固件侧 GREEN 代替 Windows/Mission Planner 完成：

1. 固件继续保持 ChibiOS 双 CDC 形态：`1209:5740`、`bcdDevice=0x0200`、
   4 个 interface、`MI_00` 为 MAVLink、`MI_02` 为 SLCAN，IAD/control
   interface 字符串索引默认保持 `0`，默认双 CDC 字符串表只保留
   ChibiOS 的 0-3 号字符串。
2. `SET_INTERFACE(alt=0)` 必须像 ChibiOS 一样 ACK，不能拆掉已配置端点。
3. `GET_LINE_CODING`、`SET_LINE_CODING`、`SET_CONTROL_LINE_STATE`、DTR/RTS
   变化和 MAVLink OUT 数据都必须成为可观测诊断信号；MAVLink TX 不以
   DTR 高电平作为唯一发送条件。
4. Windows 如果只显示一个 `ArduPilot` COM，必须先分类这个 COM：
   `MAVLINK_MI00`、`SLCAN_MI02`、`TARGET_5740_NO_MI_TAG`、COM 被占用，
   或 COM 能打开但没有原始 MAVLink 字节。不能凭显示名判断它能给
   Mission Planner 使用。
5. 最终通过条件必须同时满足：Windows 当前 app MAVLink COM 可用、raw
   MAVLink 字节可见、heartbeat 成功、完整参数下载成功、Mission Planner
   连接同一个 COM 并完成参数下载。SLCAN/`MI_02` 需要单独保留为 CAN/SLCAN
   口，不能被当成 Mission Planner 口。
6. 默认 USB reconnect timing 使用 ChibiOS-like `1500us`。如果 Windows 上仍失败，
   必须用同一套验收脚本区分 `MI_00` MAVLink、`MI_02` SLCAN、旧缓存节点和
   COM 占用；必要时再用 `windows_long_usb_disconnect.dat` 恢复旧 `750ms`
   做反向 A/B，不能把显示名相同的单个 `ArduPilot` COM 当作完成证据。

当前板端 Linux 参考证据：

```text
results/execution/chibios_usb_sync_20260619T054419Z/descriptor_gate.json
results/execution/chibios_usb_sync_20260619T054419Z/flash/openocd_flash2.log
results/execution/chibios_usb_sync_20260619T054419Z/flash/usb_after_flash.txt
results/execution/chibios_usb_sync_20260619T054419Z/param_download_retry/param_download.json
results/execution/chibios_usb_sync_20260619T054419Z/mavftp/mavftp_gate.json
results/execution/chibios_usb_sync_20260619T054419Z/peripheral_health/peripheral_health.json/peripheral_health.json
results/execution/chibios_usb_sync_20260619T054419Z/socketcan_dronecan/socketcan_dronecan_gate.json

Earlier same-identity reference:
results/execution/chibios_product_sync_20260618T115640Z/descriptor.json
results/execution/chibios_product_sync_20260618T115640Z/openocd_flash.log
results/execution/chibios_product_sync_20260618T115640Z/usb_after_flash.txt
results/execution/chibios_product_sync_20260618T115640Z/param_download/param_download.json
results/execution/chibios_product_sync_20260618T115640Z/mavftp_rerun/mavftp_gate.json
results/execution/chibios_product_sync_20260618T115640Z/socketcan_dronecan/socketcan_dronecan_gate.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/descriptor.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/openocd_flash.log
results/execution/chibios_uid_serial_followup_20260618T113117Z/usb_after_flash.txt
results/execution/chibios_uid_serial_followup_20260618T113117Z/param_download/param_download.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/mavftp/mavftp_gate.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/socketcan_dronecan_after_flash/socketcan_dronecan_gate.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/descriptor.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/build.log
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/flash2.log
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/param_after_flash/param_download.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/mavftp_after_flash/mavftp_gate.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/slcan_after_flash/slcan_ascii_gate.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/open_close_after_flash/cdc_open_close_stress.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/socketcan_dronecan_after_flash_sudo/socketcan_dronecan_gate.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/peripheral_after_flash2/peripheral_health.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/usb_snapshot_after_flash/20260618T095040Z/usb_debug_snapshot.json
results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/usb_snapshot_after_open_close/20260618T095202Z/usb_debug_snapshot.json

最新结果：默认 `1209:5740` / UID serial / `CUAVv5` product 静态描述符
GREEN；ChibiOS-like `1500us` reconnect timing 已成为默认源码路径，当前
重建后的描述符 gate 仍为 GREEN，说明时序和 product 字符串改动没有改变最终
双 CDC 身份；OpenOCD 当前烧录日志到 `Programming Finished` / `Verified OK`；
post-flash 参数下载 `945/945`，`1.695 s`，约 `557.4 params/s`，首包
`0.205 s`；MAVFTP GREEN，`@PARAM/param.pck` decoded_count=945，SD
write/read/remove OK；SocketCAN/DroneCAN sudo gate 通过标准帧、扩展帧，
并看到来自 source node 10 的 DroneCAN-like 扩展帧，官方 `dronecan` listener
也解出 NodeStatus。当前 USB by-id 为
`usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00/if02`。

当前外设只能写成 driver-level GREEN：本轮 `peripheral_health.json` 显示
`driver_verdict=GREEN`，`mag.healthy=true`，但 `calibration_verdict=RED`、
`ahrs.healthy=false`，因此不能作为 AHRS/prearm/飞行准备已闭环的证据。

较早同身份参考：

results/execution/chibios_usb_timing_goal_sync_20260618/
results/execution/rtt_chibios_sync_20260617T165721Z_linux_seq/
results/execution/rtt_chibios_otg2_seq_20260617T160805Z/
```

这些 Linux 证据只能证明固件侧 CDC/MAVLink/SLCAN/CAN/外设路径健康，不能
替代 Windows/Mission Planner 实机验收。最终仍必须看到 Windows 上
MAVLink COM 可打开、raw MAVLink/heartbeat/完整参数下载通过，并且 Mission
Planner 连接同一个 COM 成功。

如果诊断报告把唯一 COM 标为 `TARGET_5740_NO_MI_TAG`，说明 Windows/Python
看到了当前 RTT app 的 `1209:5740` COM，但没有在串口层暴露 `MI_00/MI_02`。
此时不能仅凭显示名判断，也不能把它当成最终 Mission Planner 验收口。当前
总验收脚本会自动把这个唯一当前 app COM 当作数据路径诊断候选，直接测试
raw MAVLink、heartbeat、完整参数下载；若通过，会标为
`YELLOW_MAVLINK_DATA_PATH_NO_MI_TAG`，表示 CDC/MAVLink 数据路径活着，但最终
仍需要 Mission Planner 在同一个 COM 上成功连接，且最好修复到明确 `MI_00`
或明确 MAVLink 接口。

如果手工指定的 `COMx` 没有出现在 Windows pyserial/CIM/PnP 枚举里，
`rtt_windows_mavlink_acceptance.py` 会把它记为
`WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE`。这个状态允许继续证明 raw
MAVLink、heartbeat、完整参数下载，但它仍然只是诊断证据，不是最终
`MI_00` / Mission Planner 闭环验收。

2026-06-15 的最新诊断脚本还会在 `-ProbeComOpen` 下对可疑 ArduPilot/CUAV
COM 发送轻量 SLCAN ASCII 探针（`V`、`F`、`N`）。如果
`slcan_ascii_probe_results.json` 或报告中的 `SLCAN ASCII Probe` 显示
`SlcanLike=true`，这个唯一端口就是 CAN/SLCAN 管道，不是 Mission Planner
的 MAVLink 口。用户现场看到“Windows 只有一个 ArduPilot 端口但 Mission
Planner 连不上”时，这个检查可以直接区分“选错了 SLCAN 口”和“真正的
MAVLink CDC 没吐数据”。

## Windows 侧期望结果

设备管理器和 PowerShell 应看到 `VID_1209&PID_5740`，并且至少有 `MI_00` 对应的 COM 口：

```text
USB\VID_1209&PID_5740&MI_00  -> MAVLink CDC，Mission Planner 连接这个 COM
USB\VID_1209&PID_5740&MI_02  -> SLCAN CDC
```

Mission Planner 连接时选择 `MI_00` 的 COM 口。波特率可选 `115200` 或 `921600`，USB CDC 实际吞吐不由该数值限制。

如果 Windows 只显示一个名为 `ArduPilot` 的端口，立即运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py
```

然后优先看输出和 `summary.json` 中的这些字段：

```text
SingleNamedFlightControllerPortKind
SinglePortDiagnosis
WindowsMissionPlannerDiagnosis
MissionPlannerComCandidates
SlcanComCandidates
RawMavlinkProbeVerdict
RawMavlinkProbeBytes
RawMavlinkProbeMagicCount
MavlinkVerdict
MavlinkAcceptanceMode
MavlinkIsFinalDualCdcAcceptance
MavlinkDataPathDiagnosticOnly
```

判读规则：

| 诊断值 | 含义 | 下一步 |
| --- | --- | --- |
| `MAVLINK_MI00` / `SINGLE_VISIBLE_PORT_IS_MAVLINK_CANDIDATE` | 唯一端口是 MAVLink 候选 | 用该 COM 跑 MAVLink gate；GREEN 后再用 Mission Planner 连同一个 COM |
| `RED_SINGLE_VISIBLE_SLCAN_NOT_MAVLINK` | 唯一可见飞控 COM 已经被 SLCAN ASCII 探针确认是 CAN/SLCAN 管道 | 不要用 Mission Planner 连它；恢复或绑定 `VID_1209&PID_5740&MI_00` |
| `SLCAN_MI02` / `SINGLE_VISIBLE_PORT_IS_SLCAN_NOT_MAVLINK` | 唯一端口是 SLCAN | 不要用 Mission Planner 连它；修复或绑定 `MI_00` |
| `SINGLE_VISIBLE_PORT_RESPONDS_SLCAN_ASCII_NOT_MAVLINK` | 该 COM 对 `V/F/N` 等 SLCAN 命令有响应 | 这是 CAN/SLCAN 管道，不是 MAVLink |
| `TARGET_5740_NO_MI_TAG` / `SINGLE_VISIBLE_TARGET_5740_NO_MI_TAG_REQUIRES_RAW_MAVLINK_GATE` | Windows 串口层隐藏了 MI 标签，且没有被 SLCAN ASCII 证实为 CAN 口 | 只能作为诊断候选；必须 raw MAVLink、heartbeat、完整参数下载都 GREEN，再要求 Mission Planner 同 COM 证明 |
| `WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE` | 手工输入的 COM 没有被 Windows 枚举 API 列出 | 只能证明数据路径；必须回到 `MI_00` / 明确 MAVLink 接口身份后才能最终验收 |
| `MI00_DEVICE_PRESENT_BUT_NO_COM` | `MI_00` 设备节点存在但没有 COM | 把 `USB\VID_1209&PID_5740&MI_00` 绑定到 Windows `usbser.sys` / USB Serial Device |
| `MAVLINK_COM_OPENS_BUT_NO_RAW_MAVLINK_BYTES` | COM 可打开但没有 MAVLink 字节 | 立即采集 OpenOCD USB snapshot，看 line coding、DTR/RTS、bulk IN/OUT、TX kick/recovery 计数 |
| `MAVLINK_COM_BUSY_OR_ACCESS_DENIED` | COM 被占用或权限拒绝 | 关闭 Mission Planner、串口工具、设备管理器属性页后重跑 |

## 当前 Linux / CAN 工具状态

2026-06-15 当前主机已具备 CAN/DroneCAN 调试工具：

```text
slcand: /usr/bin/slcand
candump: /usr/bin/candump
cansend: /usr/bin/cansend
python dronecan: 1.0.27
python-can: 4.6.1
pymavlink: 2.4.48
pyserial: 3.5
```

飞控自身 SLCAN CDC 在 `RTT5740K closepause` 固件上已通过板端命令层：
`results/execution/windows_mp_rtt5740k_closepause_slcan_20260615T053529Z/slcan_ascii_gate.json`。
旧 `RTT5740I` 固件还做过 CAN1 被动监听：
`results/execution/windows_mp_rtt5740i_slcan_boardonly_20260615T041957Z/slcan_ascii_gate.json`。
当前 Linux 可见的 `usb-1a86_USB_Single_Serial_5565052973-if00` 不响应 SLCAN
ASCII，见
`results/execution/windows_mp_rtt5740i_slcan_with_1a86_debugger_20260615T042832Z/slcan_ascii_gate.json`。
因此它不能作为本轮可控外部 SLCAN 调试器来闭环 DroneCAN 注入测试；外部
DroneCAN 完整验收需要一个能响应 `C/Sx/O/F/V/N` 的 SLCAN 或 SocketCAN 设备。

本分支还提供 Linux 侧 SocketCAN/DroneCAN 统一门禁：

```bash
nohup timeout 180 python3 Tools/scripts/rtt_socketcan_dronecan_gate.py \
  --port auto \
  --iface can_rtt0 \
  --require-rx \
  --require-dronecan \
  --outdir results/execution/socketcan_dronecan_<timestamp> \
  > results/execution/socketcan_dronecan_<timestamp>.log 2>&1
```

主机工具自检证据：
`results/execution/socketcan_dronecan_gate_selftest_20260615/socketcan_dronecan_gate.json`
为 GREEN，确认 `slcand`、`candump`、`cansend`、`ip`、`dronecan 1.0.27`
均可用。硬件模式必须在飞控 `if02`/ST-Link 可见后重新运行；只有 `cansend`
返回 0 不算完整物理总线闭环，必须结合 `candump` 或 `dronecan` 事件。

## 一键 Windows 验收

优先运行总验收脚本。它会依次执行 USB PnP/COM 诊断和 MAVLink
heartbeat/参数下载，并生成一个总 `summary.json`：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1
```

如果 Python 命令在你的 Windows 上不是 `python`，可以指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -Python py
```

如果 Windows / Mission Planner 只显示一个 `ArduPilot` COM，可以先关闭
Mission Planner，然后直接运行总脚本。新版脚本会自动探测唯一的当前
`1209:5740` COM：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -Python py
```

如果你已经知道这个 COM 号，也可以手工指定，让脚本直接做 MAVLink 心跳和参数下载：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -MavlinkPort COM7 -ProbeComOpen
```

若这个命令仍是 RED，说明这个 COM 不是可用的 MAVLink `MI_00` app 口，或被
Windows 驱动/其他程序占用；继续看 `usb_diag\...\classified_flight_controller_com_ports.json`
和 `mavlink_stdout.txt`。
如果 RED 的失败类别是 `COM_OPENED_NO_RAW_MAVLINK_BYTES`，请保持飞控和
Windows 连接状态不变，马上在 Linux/OpenOCD 主机运行总脚本生成的
`FirmwareSnapshotCommand`，或手工运行：

```bash
nohup timeout 90 python3 Tools/scripts/rtt_usb_debug_snapshot.py --outdir results/execution/windows_mp_firmware_snapshot_after_windows_attempt > results/execution/windows_mp_firmware_snapshot_after_windows_attempt.log 2>&1
```

重点看 `rtt_dbg_cherry_get_line_coding_calls`、`set_line_coding_calls`、
`set_dtr_calls`、`set_rts_calls`、`host_open_hint_state`、`bulk_in_calls`
和 `tx_start_ok`。快照分类还会汇总 `SET_INTERFACE`、重复
`SET_INTERFACE(alt=0)` ACK、line coding、DTR/RTS 和 bulk IN/OUT 计数。
如果这些计数没有增加，问题在 Windows 是否打开了正确 `MI_00` 设备节点；
如果 class request 增加但 `bulk_in/tx_start_ok` 不增加，才继续查 RTT
CDC TX 路径。
总脚本会把手工 COM 的身份校验结果写到 `summary.json` 里的
`ManualPortCheckVerdict` / `ManualPortCheckReason`。如果原因是
`manual_port_is_slcan_mi02_not_mavlink`、`manual_port_is_legacy_5741_not_current_app`
或 `manual_port_name_match_only_not_target_5740`，Mission Planner 选到的就不是
当前 RTT app 的 MAVLink `MI_00` 口。

如果 Windows 仍然只显示一个旧的 `ArduPilot` COM，或者怀疑系统复用了旧
USB 设备实例，可以先用总脚本的刷新模式预览旧节点：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb
```

确认 `usb_refresh\...\removal_candidates.json` 里只有旧的非在线
`1209:5740/5741` 节点后，再执行刷新并验收：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshApply
```

如果刷入当前 UID-serial 双 CDC 固件后，Windows 仍然把当前在线设备显示成一个不能连接的
父级 `ArduPilot` COM，先关闭 Mission Planner，再预览一次“包含当前在线
目标设备”的刷新候选：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshRemovePresentTarget
```

确认 `removal_candidates.json` 里只包含 `VID_1209&PID_5740` / `VID_1209&PID_5741`
的 ArduPilot/CUAV 目标节点后，再执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshRemovePresentTarget -RefreshApply -ProbeComOpen -Python py
```

这一步会让 Windows 删除当前错误绑定的目标节点并重新扫描；不要在 Mission
Planner 占用 COM 口时执行。

如果怀疑这个唯一 COM 被 Mission Planner 或其他程序占用，或者想确认
Windows 是否能直接打开该端口，加入 `-ProbeComOpen`：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen
```

该模式会在 `summary.json` 和 `com_open_probe_results.json` 中记录
COM 是否能被打开。运行此命令前先关闭 Mission Planner，否则端口被占用时
探测会失败，这是有用的证据。

如果缺少 Python 依赖，先安装：

```powershell
python -m pip install pymavlink pyserial
```

## 单 CDC 诊断固件

如果默认双 CDC 固件在 Linux 上已经证明 `1209:5740 / MI_00` 可以高速下参，
但 Windows / Mission Planner 只显示一个 `ArduPilot` 口且连不上，可以临时
刷入 MAVLink-only 诊断固件来做 A/B 定位：

```bash
scons --v=ArduCopter --target=cuav_v5 \
  --extra-hwdef=libraries/AP_HAL_RTT/hwdef/extras/windows_mavlink_only.dat \
  -j$(nproc)
```

该诊断固件仍走同一条 MAVLink CDC 数据路径，但 USB 身份改为 ChibiOS
单 CDC 风格的 `1209:5741`，且没有 SLCAN。Windows 上运行总验收时必须显式
允许这个诊断身份：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -AllowDiagnostic5741
```

判定方式：

- 单 CDC `1209:5741` 在 Mission Planner 中能连接并完成参数下载，而默认双
  CDC `1209:5740` 失败：问题集中在 Windows 复合 CDC / MI 绑定或端口选择。
- 单 CDC 也失败：问题不在 SLCAN/双 CDC，继续查 Windows `usbser.sys` 绑定、
  COM 口占用、Mission Planner 选口或 CDC 数据路径。

诊断完成后必须刷回默认双 CDC `1209:5740` 固件；最终验收仍以
`VID_1209&PID_5740&MI_00` 为 Mission Planner MAVLink 口、`MI_02` 为 SLCAN
口。

## ChibiOS-Shape 双 CDC 兼容入口

当前默认固件已经采用这个 ChibiOS-style 双 CDC 描述符形态：保持最终
`1209:5740` 双 CDC 身份和两条数据路径不变，并把 CDC function / control
interface 字符串索引置为 `0`。下面的 extra hwdef 仍保留为历史兼容入口，
但对当前默认 UID-serial 构建不再是一个有区分度的 A/B 对照。

```bash
scons --v=ArduCopter --target=cuav_v5 \
  --extra-hwdef=libraries/AP_HAL_RTT/hwdef/extras/windows_chibios_dualcdc_shape.dat \
  -j$(nproc)
```

判定方式：

- 如果当前默认 UID-serial 固件仍然只能在 Windows 生成一个 COM，重点查 Windows
  旧设备节点、`usbser.sys` 绑定、复合父设备状态，或 CherryUSB/DWC2 的更深层
  复合设备差异。
- 即使 Windows 生成了两个 COM，也必须继续通过 raw MAVLink、heartbeat、完整
  参数下载和 SLCAN 门禁；仅枚举出两个 COM 不等于完成。

## USB 时序 A/B 诊断固件

如果默认 UID-serial 双 CDC 固件仍在 Windows 上只显示一个 `ArduPilot`
COM，或 Mission Planner 能看到 COM 但连不上，下一步不是继续盲改 CDC 数据
路径，而是先固定当前默认 ChibiOS-like `1500us` 时序，用 Windows 脚本分类
这个 COM 到底是 MAVLink、SLCAN、旧缓存节点还是被占用。只有需要反向验证
旧行为时，才构建长断开 A/B 固件。该 A/B 固件保持最终 `1209:5740` 双 CDC
身份不变，仅把 app 启动时的软断开等待恢复到旧的长断开窗口 `750ms`。

构建：

```bash
python3 -m SCons --v=ArduCopter --target=cuav_v5 \
  --extra-hwdef=libraries/AP_HAL_RTT/hwdef/extras/windows_long_usb_disconnect.dat \
  -j$(nproc)
```

构建后仍必须先跑描述符 gate，确认没有把目标变成单 CDC 或旧 PID：

```bash
nohup timeout 60 python3 Tools/scripts/rtt_usb_descriptor_gate.py \
  --bin build/rtt_cuav_v5/rtthread.bin \
  --json-out results/execution/windows_long_usb_disconnect_descriptor.json \
  > results/execution/windows_long_usb_disconnect_descriptor.log 2>&1
```

然后烧录到 `0x08008000` 并做 Linux 回归；如果 Linux 的 heartbeat、快速参数
下载、MAVFTP、SLCAN/DroneCAN 或外设门禁退化，该诊断固件不能拿去当 Windows
修复候选。

Windows 判定方式：

- 默认 `1500us` 固件让 Windows 稳定出现 `VID_1209&PID_5740&MI_00`，并且
  raw MAVLink、heartbeat、完整参数下载、Mission Planner 同 COM 参数下载都
  通过：继续保留 ChibiOS-like 默认时序。
- 默认 `1500us` 固件仍只显示一个不可用 `ArduPilot` COM，或 raw MAVLink 仍失败：
  优先查 Windows `usbser.sys` 绑定、`MI_00` 设备节点、CherryUSB/DWC2
  复合接口请求、或 Mission Planner 选口；可用旧 `750ms` A/B 固件确认是否为
  reconnect timing 回归。
- 旧 `750ms` A/B 固件表现更好：说明短断开时序和当前 Windows 主机存在兼容
  风险，需要保留证据后重新评估默认值，不能直接宣称完成。

## Windows 只显示一个 ArduPilot COM 时

先不要打开 Mission Planner 占用串口，按下面顺序处理：

1. 运行 USB 诊断，确认这个唯一 COM 到底是 `MI_00`、`MI_02`、旧
   `5741`，还是只有显示名相似：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_diag.ps1
```

如果需要同时确认 COM 是否可打开：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_diag.ps1 -ProbeComOpen
```

2. 如果诊断显示有旧的 `1209:5740/5741` 非当前在线设备实例，可以用总验收脚本预览刷新候选项：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb
```

3. 确认 `removal_candidates.json` 里只有旧的非在线 ArduPilot/CUAV USB
   节点后，再移除这些旧节点、触发 Windows 重新扫描、并重新跑 USB/MAVLink 验收：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshApply
```

4. 如果只想单独执行刷新脚本，也可以运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_refresh.ps1 -Apply
```

如果刷新后仍然只显示一个端口，但分类是 `MAVLINK_MI00`，并且 MAVLink
验收为 `GREEN`，Mission Planner 就连接这个 COM。如果 Windows/Python 只给
出 `TARGET_5740_NO_MI_TAG`，必须让 `rtt_windows_acceptance_all.ps1` 在这个
COM 上完成 raw MAVLink、heartbeat、参数下载三项绿色验收，用来证明数据链路
没有坏；但这仍不是最终 Windows/Mission Planner 闭环验收，最终还要修复到
`MI_00` 或明确 MAVLink 接口可识别。若手工输入的 COM 被标为
`WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE`，它也只能作为数据路径诊断结果，
不能当作最终 Windows/Mission Planner 验收。若分类是 `SLCAN_MI02` 或
`LEGACY_5741_BOOTLOADER_OR_SINGLE_CDC`，这个端口不是正式双 CDC 固件的
Mission Planner MAVLink app 口。

当前默认 UID-serial 固件保留 ChibiOS 双 CDC 的 IAD/MI 结构，并把
`MI_00` / `MI_02` 的 IAD/control interface 索引置为 `0`，更贴近 ArduPilot
ChibiOS 描述符。如果 Windows 仍只显示一个名为 `ArduPilot` 的端口，优先看
脚本输出中的 `Kind` / `PNPDeviceID` / `target_usb_topology.json` /
MAVLink 验收结果，不能只看 Mission Planner 下拉框显示名。

总脚本输出目录形如：

```text
.\rtt_windows_acceptance_evidence\20260614T001122\
```

`rtt_windows_acceptance_all.ps1` 现在会在同一个目录里自动执行最终审计，
并写出：

```text
firmware_descriptor\descriptor.json   # 如果 build\rtt_cuav_v5\rtthread.bin 存在
audit_summary.json
audit_stdout.txt
```

`firmware_descriptor\descriptor.json` 来自 `rtt_usb_descriptor_gate.py`，用于把
ChibiOS-style USB 描述符和字符串证据一起放进 Windows 证据包：`1209:5740`、
`MI_00`/`MI_02`、`ArduPilot`、`CUAVv5`、24 位 serial 结构。若 Windows
工作区没有固件 bin，这一步会跳过；此时 USB/MAVLink 诊断仍可用，但最终提交
还要配套 Linux/固件侧 descriptor gate 证据。

如果运行的是显式 `-AllowDiagnostic5741` 单 CDC 隔离固件，总脚本会把
descriptor profile 切到 `mavlink_only`。这只能产生诊断闭环
`YELLOW_DIAGNOSTIC`，用于判断 Windows/Mission Planner 是否能连单 CDC；
它不能替代最终 `1209:5740` 双 CDC + `MI_02` SLCAN 验收。

如果已经有 Mission Planner 本体连接证据，可以在运行总验收时直接带入该
JSON，并要求最终 audit 决定进程退出码：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py -MissionPlannerEvidence .\mission_planner_evidence.json -RequireAuditGreen
```

`-MissionPlannerEvidence` 会把给定 JSON 复制到本次证据目录根部；
`-RequireAuditGreen` 会让命令只有在 `audit_summary.json` 的 `verdict`
为 `GREEN` 时才返回 0。没有 Mission Planner 证据时不要加这个参数期待
GREEN，因为 audit 会保持 RED。

把该目录拷回仓库或在同一工作区内，也可以手工复核同一套审计规则：

```bash
python3 Tools/scripts/rtt_windows_acceptance_audit.py rtt_windows_acceptance_evidence/20260614T001122
```

审计脚本只读取 JSON 证据，不连接硬件、不修改 Windows。只有当它输出
`"verdict": "GREEN"`，或自动生成的 `audit_summary.json` 中
`verdict` 为 `GREEN` 时，才表示 Windows 侧至少证明了：

- `VID_1209&PID_5740&MI_00` 已经映射为 Mission Planner 可选 COM，或
  Windows 明确暴露了当前 app 的 MAVLink 接口字符串。
- 同一 COM 打开后能看到原始 MAVLink framing byte，`raw_mavlink_probe`
  为 `GREEN` 且 `0xfe/0xfd` magic count 大于 0。
- MAVLink heartbeat 和完整参数下载在该 COM 上通过。
- SLCAN `MI_02` 被识别；如果没有 `MI_02`，必须明确记录为非最终诊断/
  fallback 情况。默认双 CDC 固件只有 `MI_00` MAVLink、没有 `MI_02` 证据时，
  `rtt_windows_acceptance_audit.py` 必须保持 RED。
- 总验收 `Overall` 为 `GREEN_MAVLINK`。
- Mission Planner 本体连接了同一个 `MI_00` COM，并且这个 COM 必须和
  `mavlink_acceptance.json` 里通过 raw MAVLink、heartbeat、参数下载的
  `port` 完全一致。
- `mission_planner_evidence.json` 必须写 `param_count` 或
  `param_reported_count`，并且该值必须和 `mavlink_acceptance.json` 里的
  `param_download.reported_count` 一致。当前参考值是 `945`。
- `mission_planner_evidence.json` 还必须写 `usb_identity` 和 `connected_at`。
  `usb_identity` 必须和 `mavlink_acceptance.json` 里的
  `accepted_usb_identity` 一致；最终双 CDC 应为 `VID_1209&PID_5740&MI_00`
  或明确的 MAVLink interface-string 等价身份。

如果 `raw_mavlink_probe` 是 RED，审计脚本还会要求
`usb_debug_snapshot.json` 作为固件侧解释证据。这个快照不会把失败变成成功，
但会把 `SET_INTERFACE`、line coding、DTR/RTS、`host_open_hint` 和
bulk IN/OUT 计数写进审计结果，避免把“Windows 没真的打开 CDC”误判成
“固件已经坏了”。

总验收脚本会自动生成
`mission_planner_evidence.template.json`。Mission Planner 实际连接后，把这个
模板改名为 `mission_planner_evidence.json`，放在总证据目录根部；或者在下次
运行总验收时用 `-MissionPlannerEvidence .\mission_planner_evidence.json`
带入：

```json
{
  "verdict": "GREEN",
  "source": "Mission Planner",
  "port": "COM7",
  "usb_identity": "VID_1209&PID_5740&MI_00",
  "connected_at": "2026-06-19T12:34:56+08:00",
  "mission_planner_connected": true,
  "param_download_complete": true,
  "param_count": 945,
  "notes": "Mission Planner connected to VID_1209&PID_5740&MI_00 and completed parameter download."
}
```

如果 Mission Planner 仍连不上，把 `verdict` 写成 `RED`，并在 `notes`
里记录错误现象，例如 timeout、端口打不开、参数卡住的位置。审计脚本会保持
RED，直到该证据和自动 MAVLink gate 都通过。

重点文件：

| 文件 | 用途 |
| --- | --- |
| `summary.json` | 总验收结论，包含 USB verdict、MAVLink verdict、应连接的 COM |
| `firmware_descriptor\descriptor.json` | 当前固件 bin 的 ChibiOS-style USB descriptor/string gate；最终 GREEN 必须是 `1209:5740` 双 CDC、`ArduPilot`、`CUAVv5` |
| `usb_diag\...\summary.json` | USB 枚举/驱动诊断结论 |
| `usb_diag\...\target_pnp_devices.json` | `1209:5740` 的 PnP/驱动细节 |
| `usb_diag\...\target_usb_topology.json` | 当前 app 父设备、`MI_00`、`MI_02`、COM 和 `usbser.sys` 绑定拓扑 |
| `summary.json` 中的 `ManualPortCheckReason` | 手工指定 COM 时判断该端口是否是当前 `MI_00`、`MI_02`、旧 `5741` 或仅名字相似 |
| `mavlink\...\mavlink_acceptance.json` | heartbeat 和参数下载结果 |
| `mission_planner_evidence.template.json` | 总脚本生成的 Mission Planner 证据模板 |
| `mission_planner_evidence.json` | Mission Planner 本体连接和参数下载证据 |
| `audit_summary.json` | 总脚本自动生成的最终闭环审计结果，`verdict=GREEN` 才能说 Windows/Mission Planner 完成 |
| `audit_stdout.txt` | 审计脚本原始输出，便于定位缺哪一项证据 |
| `usb_diag_stdout.txt` / `mavlink_stdout.txt` | 命令行原始输出 |
| `rtt_windows_acceptance_audit.py` 手工输出 | 对自动审计的复核入口 |

总脚本的 `Overall` 判定：

| 结果 | 含义 |
| --- | --- |
| `GREEN_MAVLINK` | Windows 上 `MI_00` MAVLink COM 可用，raw MAVLink bytes、heartbeat、参数下载都已通过 |
| `YELLOW_USB_MAVLINK_COM_PRESENT` | Windows 看到了 `MI_00` COM，但 raw probe 或 MAVLink 脚本未通过，常见为 COM 被占用、连接了错误口或 CDC TX 没有出字节 |
| `RED` | Windows 尚未证明有可用的 `MI_00` MAVLink COM |

## 分步采集 Windows 证据

在 Windows PowerShell 中运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_diag.ps1
```

脚本只读取设备信息，不修改驱动、注册表或串口配置。输出目录形如：

```text
.\rtt_windows_usb_evidence\20260613T235959\
```

重点文件：

| 文件 | 用途 |
| --- | --- |
| `summary.json` | 总结是否看到 `MI_00` / `MI_02` COM，以及下一步建议 |
| `report.txt` | 人类可读报告 |
| `target_com_ports.json` | `1209:5740` 的 COM 口列表 |
| `target_pnp_devices.json` | `1209:5740` 的所有 PnP 节点、驱动服务和问题码 |
| `target_usb_topology.json` | 复合父设备、`MI_00`、`MI_02` 子接口、COM、驱动绑定的直接拓扑摘要 |
| `target_problem_devices.json` | `1209:5740` 中有 Windows problem code 的节点 |
| `com_open_probe_results.json` | 使用 `-ProbeComOpen` 时记录 COM 是否可打开、是否被占用 |
| `focused_present_pnp_devices.json` | 当前 Ports/USB/USBDevice/Modem/Unknown 设备快照 |
| `legacy_5741_com_ports.json` | 是否仍有旧 `1209:5741` COM 残留 |
| `target_or_named_flight_controller_com_ports.json` | 目标 VID/PID COM 与 ArduPilot/CUAV/APM/PX4 名称相似 COM 的合并列表；即使 Windows 显示为通用 `USB Serial Device` 也会出现在这里 |
| `classified_flight_controller_com_ports.json` | 上述合并列表的分类结果，含 `MAVLINK_MI00`、`SLCAN_MI02`、`TARGET_5740_NO_MI_TAG` 等 |

如果 Windows 只显示一个 `ArduPilot` COM，先看 `summary.json` 中的
`SingleNamedFlightControllerPortKind`，或者打开
`classified_flight_controller_com_ports.json`。分类含义：

| 分类 | 含义 |
| --- | --- |
| `MAVLINK_MI00` | 当前 RTT app MAVLink 口，Mission Planner 应连接这个 COM |
| `SLCAN_MI02` | 当前 RTT app SLCAN 口，不能用于 Mission Planner MAVLink |
| `MAVLINK_INTERFACE_STRING` | 当前 RTT app MAVLink 口，若 MI 标签被 Win32 串口 API 隐藏，也可用此分类给 Mission Planner |
| `SLCAN_INTERFACE_STRING` | 当前 RTT app SLCAN 口，不能用于 Mission Planner MAVLink |
| `TARGET_5740_NO_MI_TAG` | 是当前 RTT app 的 `1209:5740` COM，但 Windows/Python 没暴露 MI 标签；只能作为手工诊断候选，用来证明数据链路是否活着，不能作为最终 Mission Planner 验收口 |
| `LEGACY_5741_BOOTLOADER_OR_SINGLE_CDC` | 默认双 CDC 验收中视为旧 bootloader/单 CDC 路径；只有显式刷入 MAVLink-only 诊断固件并使用 `-AllowDiagnostic5741` 时才可作为临时诊断口 |
| `NAME_MATCH_ONLY_NOT_TARGET` | 名称像飞控，但硬件 ID 不是当前 `1209:5740` app |

使用 `-ProbeComOpen` 时，`rtt_windows_usb_diag.ps1` 还会在
`summary.json` 里写入 `SinglePortDiagnosis`。这个字段专门用于定位
“Windows 只显示一个 ArduPilot 端口，但 Mission Planner 连不上”的现场：

| `SinglePortDiagnosis` | 含义 |
| --- | --- |
| `SINGLE_VISIBLE_PORT_RESPONDS_SLCAN_ASCII_NOT_MAVLINK` | 唯一可见端口能响应 `V/F/N` 等 SLCAN ASCII 命令；这是 CAN/SLCAN 管道，不是 Mission Planner MAVLink 口 |
| `SINGLE_VISIBLE_PORT_IS_SLCAN_NOT_MAVLINK` | 唯一可见端口被 Windows 拓扑分类为 `MI_02` / SLCAN |
| `SINGLE_VISIBLE_PORT_IS_MAVLINK_CANDIDATE` | 唯一可见端口是 `MI_00` 或 MAVLink interface-string 候选；还必须继续通过 raw MAVLink、心跳和参数下载 |
| `SINGLE_VISIBLE_TARGET_5740_NO_MI_TAG_REQUIRES_RAW_MAVLINK_GATE` | 唯一可见端口是当前 `1209:5740` app，但 Windows 没暴露 `MI_00/MI_02`；只能作为手工 raw gate 候选 |
| `SINGLE_VISIBLE_LEGACY_5741_NOT_FINAL_DUAL_CDC` | 唯一可见端口是旧 `1209:5741`，不能证明当前双 CDC app |
| `SINGLE_VISIBLE_NAME_MATCH_ONLY_NOT_TARGET_5740` | 只是名字像飞控，硬件 ID 不匹配当前目标 |

总验收脚本 `rtt_windows_acceptance_all.ps1` 会把这个结果进一步汇总为
`WindowsMissionPlannerDiagnosis`：

| `WindowsMissionPlannerDiagnosis` | 处理方向 |
| --- | --- |
| `ONLY_VISIBLE_ARDUPILOT_COM_IS_SLCAN` | 当前唯一端口不能给 Mission Planner 用；先恢复/绑定 `VID_1209&PID_5740&MI_00` |
| `ONLY_VISIBLE_ARDUPILOT_COM_HAS_NO_MI_TAG` | 先用 `-BindUsbser -BindUsbserRescan -ProbeComOpen` 修复/刷新绑定；仍无 MI 标签时只能做 raw gate |
| `MI00_DEVICE_PRESENT_BUT_NO_COM` | Windows 已看到 `MI_00` 设备节点，但没有生成 COM；检查设备管理器并绑定 `usbser` |
| `MAVLINK_COM_OPENS_BUT_NO_RAW_MAVLINK_BYTES` | 端口能打开但没有 MAVLink 字节；立即在 Linux/OpenOCD 侧跑固件 USB 快照 |
| `MAVLINK_COM_BUSY_OR_ACCESS_DENIED` | 关闭 Mission Planner 或其他串口工具后重跑 |
| `RAW_MAVLINK_GREEN_BUT_USB_IDENTITY_NOT_FINAL` | 数据链路活着，但最终双 CDC `MI_00` 身份还没被证明 |
| `WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE` | 手工 COM 数据链路可能活着，但 Windows 没把它列为当前 app `MI_00`；只能当诊断证据 |
| `MAVLINK_MI00_GATE_GREEN` | 脚本级 MAVLink gate 已过，继续收集 Mission Planner 截图/参数下载证据 |

当 `MavlinkDataPathDiagnosticOnly=true` 时，即使 raw MAVLink、heartbeat 和
参数下载是绿色，也只能证明数据路径，没有证明最终 `MI_00` / Mission
Planner 端口身份。

## 判定标准

| 结果 | 含义 | 下一步 |
| --- | --- | --- |
| `GREEN_TWO_COM_PORTS` | Windows 已看到 MAVLink 和 SLCAN 两个 COM | 用 MAVLink COM 连接 Mission Planner，再验证参数下载 |
| `YELLOW_MAVLINK_ONLY` | Windows 至少看到 MAVLink COM，但没看到 SLCAN COM | 可以继续验 Mission Planner 数据路径，但不能作为最终 GREEN；还要恢复/证明 `MI_02` SLCAN 或记录为非最终诊断/fallback |
| `RED_MI00_PRESENT_NO_COM` | Windows 看到 `MI_00` 设备节点，但没有创建 COM | 在设备管理器中检查该节点，通常是 CDC/usbser 驱动绑定问题 |
| `RED_TARGET_DEVICE_NO_MAVLINK_COM` | 看到 `1209:5740` 设备，但没有 `MI_00` COM | 查 Windows CDC 驱动绑定 |
| `RED_ONLY_LEGACY_5741_VISIBLE` | 只看到旧 `1209:5741` | Windows 仍在 bootloader/单 CDC 路径，未枚举到当前 app |
| `RED_TARGET_DEVICE_MISSING` | 新旧目标设备都没看到 | 检查固件是否运行、USB 是否枚举 |

## Windows 驱动绑定备用路径

Windows 10/11 正常应自动把 `MI_00` 和 `MI_02` 绑定到内置
`usbser.sys`。如果诊断里出现：

- `RED_MI00_PRESENT_NO_COM`
- `RED_TARGET_DEVICE_NO_MAVLINK_COM`
- `target_com_ports.json` 中 `UsbSerBound=false`

说明 Windows 已经看到设备节点，但没有把 CDC 接口正确生成 COM。此时可用
仓库中的备用 INF 做绑定排障：

```powershell
pnputil /add-driver .\Tools\windows_drivers\rtt_ardupilot_cdc\rtt_ardupilot_cdc_usbser.inf /install
```

或者使用仓库里的辅助脚本，一次完成 INF 安装、设备重扫和后续 USB 诊断：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_bind_fix.ps1 -Install -Rescan -ProbeComOpen
```

该脚本会在自己的 `summary.json` 中汇总后续 USB 诊断关键字段：
`BindFixVerdict`、`UsbDiagVerdict`、`MI00ComCount`、`MI02ComCount`、
`MI00UsbserComCount`、`MI02UsbserComCount`、`MissionPlannerComCandidates`、
`SlcanComCandidates` 和 `NextAction`。如果 `BindFixVerdict` 仍是
`RED_USB_SERIAL_BINDING_STILL_INCOMPLETE`，说明安装 INF 后 Windows 仍没有把
`MI_00` 生成为可用 COM，需要在设备管理器里对
`USB\VID_1209&PID_5740&MI_00` 手工选择系统内置 `USB Serial Device`。

可以只跑分类逻辑自测，不访问硬件：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_bind_fix.ps1 -SelfTest
```

如果希望继续使用总验收入口，也可以让总脚本先执行绑定修复，再继续做
USB/MAVLink 验收：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py
```

如果 Windows 拒绝未签名 INF，用设备管理器对具体
`USB\VID_1209&PID_5740&MI_00` 节点手工更新驱动，选择系统内置的
`USB Serial Device` / `usbser.sys`。完成后重新运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen
```

只有 `MI_00` 生成的 COM 才给 Mission Planner 使用；`MI_02` 是 SLCAN。

不要只看设备显示名。Windows 设备管理器里名称仍可能显示 `ArduPilot`，
但验收必须看硬件 ID：

```text
USB\VID_1209&PID_5740&MI_00  PASS: Mission Planner MAVLink COM
USB\VID_1209&PID_5740&MI_02  PASS: SLCAN COM
USB\VID_1209&PID_5741        默认 FAIL: bootloader/单 CDC/旧缓存路径；诊断固件 + AllowDiagnostic5741 时为临时 MAVLink-only
```

## MAVLink 链路验收

如果枚举脚本看到 `MI_00` COM，先用仓库里的 Python 脚本直接验证 MAVLink
链路。这样可以把 Mission Planner UI/选口问题和固件 CDC 问题分开。

Windows 上运行：

```powershell
python .\Tools\scripts\rtt_windows_mavlink_acceptance.py --port auto
```

如果没有安装 `pymavlink` 或 `pyserial`，先安装：

```powershell
python -m pip install pymavlink pyserial
```

脚本会通过 `pyserial` 和 `Win32_SerialPort.PNPDeviceID` 自动查找
`USB\VID_1209&PID_5740&MI_00` 的 COM 口，拒绝把 `MI_02` 当 MAVLink
使用，并输出 `mavlink_acceptance.json`。脚本默认会先短暂打开同一 COM，
分别做两种原始字节探针并记录到 `raw_mavlink_probe`：

- `natural_open`：自然打开 COM，不强制设置 DTR/RTS，贴近 Mission Planner
  或 Windows `usbser.sys` 不拉 DTR 时的行为。
- `forced_dtr_rts`：显式设置 DTR/RTS，用来和传统串口工具行为对比。

这个探针不是最终通过标准，但它能解释 Mission Planner 失败的层级：

| 字段 | 解释 |
| --- | --- |
| `raw_mavlink_probe.verdict=GREEN` | COM 打开后能看到 `0xfe` 或 `0xfd` MAVLink magic，说明固件 CDC TX 有原始 MAVLink 字节流 |
| `raw_mavlink_probe.natural_open.verdict=GREEN` | 不强制 DTR/RTS 也有 MAVLink 字节，说明 Mission Planner 不拉 DTR 时固件也不会静默 |
| `raw_mavlink_probe.reason=no_bytes_seen` 或 `no_mavlink_magic_seen` | COM 能打开但没有读到 MAVLink 字节，优先查固件 TX、DTR/host-open hint、端点完成、或是否选错口 |
| `raw_mavlink_probe.reason=bytes_seen_without_mavlink_magic` | 该 COM 有字节但不像 MAVLink，常见为选到了 SLCAN/其他串口 |

通过标准仍然是 heartbeat 和完整参数下载：

```text
verdict=GREEN
heartbeat.system_status=3/4/5
param_download.reported_count=945
param_download.unique_indices=945
param_download.missing_count=0
param_download.elapsed_s <= 12
```

本机 Linux 同一脚本的参考结果：

```text
verdict=GREEN
port=/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00
raw_mavlink_probe=GREEN, bytes=42, mavlink_v2_magic_count=2
heartbeat.system_status=3
param_download=945/945
elapsed_s=1.695
```

如果该脚本 `GREEN`，但 Mission Planner 仍连不上，优先检查：

- Mission Planner 是否选择了 `MI_00` 的 COM，而不是 `MI_02` SLCAN。
- 是否还有脚本、串口终端、设备管理器属性页占用了同一个 COM。
- Mission Planner 波特率可选 `115200` 或 `921600`，USB CDC 实际吞吐不由该值限制。
- 关闭 Mission Planner 后重新运行本脚本，确认 COM 没有被占用。

如果该脚本 `RED`，先看 `reason`，再回到
`rtt_windows_usb_diag.ps1` 的 `summary.json` 和 `target_pnp_devices.json`
定位是没有 `MI_00` COM、驱动未绑定，还是固件没有回应。

如果 `reason` 是
`multiple_1209_5740_com_ports_without_mi_tag_use_manual_port`，说明 Windows
或 pyserial 只暴露了多个 `1209:5740` COM，但没有把 `MI_00/MI_02` 标签
传给 Python。此时不要让脚本猜端口；先看 `rtt_windows_usb_diag.ps1`
报告中的 `MissionPlannerComCandidates`，然后手动指定：

```powershell
python .\Tools\scripts\rtt_windows_mavlink_acceptance.py --port COMx
```

其中 `COMx` 必须是 `VID_1209&PID_5740&MI_00` 对应的端口。

## Mission Planner 验收

Mission Planner 连接 `MI_00` COM 后需要确认：

1. 能收到 heartbeat 并进入连接状态。
2. 参数下载能完成，不长时间卡住。
3. 参数数量与 Linux 门禁一致，当前 Linux 参考值为 `945/945`。
4. 断开 Mission Planner 后，`MI_02` 可用于 SLCAN/DroneCAN 测试。

Linux 侧如果只有飞控 `if02`，没有可控的第二个 SLCAN 调试器，也可以先做
SLCAN 命令层加 CAN1 被动接收验证：

```bash
nohup timeout 90 python3 Tools/scripts/rtt_slcan_ascii_gate.py \
  --board-only --passive-listen-s 5 \
  --board-port auto \
  --outdir results/execution/slcan_boardonly_passive_can1_20260615T034911Z \
  > results/execution/slcan_boardonly_passive_can1_20260615T034911Z/run.log 2>&1
```

当前固件的 SocketCAN/DroneCAN 参考为
`results/execution/chibios_product_sync_20260618T115640Z/socketcan_dronecan/socketcan_dronecan_gate.json`：
`slcand` 成功把 `if02` 挂到 `can_rtt0`，标准/扩展 `cansend` 成功，`candump`
看到 11 个 DroneCAN-like 扩展帧，源节点 id 为 10。官方 `dronecan 1.0.27`
listener 在 20 秒窗口内解出 3 个 NodeStatus 事件，`health=0`，`mode=0`。

## Linux 侧参考证据

当前固件在 Linux 上已验证：

```text
1209:5740 Generic CUAVv5
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02
```

参数下载参考结果：

```text
945/945 parameters
2.023 s
467.2 params/s
verdict=GREEN
heartbeat stable, STANDBY
```

SLCAN/SocketCAN 参考结果：

```text
can_rtt0 ERROR-ACTIVE
cansend standard frame rc=0
cansend extended frame rc=0
candump saw DroneCAN-like extended frames from source node id 10
```

完整证据目录：

```text
results/execution/chibios_uid_serial_followup_20260618T113117Z
```

这些目录证明的是 Linux/固件侧已经是 `1209:5740` 双 CDC，两个 CDC
notification endpoint 已与 ChibiOS 对齐为 16 字节，并且 `MI_00`
在 Windows-safe 64B CDC 提交模式下可稳定心跳、快速参数下载和 MAVFTP。
Windows/Mission Planner 仍必须单独用本文件的脚本和 Mission Planner 实测验收。
