# CUAV V5 RT-Thread ArduPilot 移植验收分支

本分支用于把 ArduPilot 在 CUAV V5 上的 RT-Thread 移植推进到可验收状态。它不是上游 ArduPilot 的通用介绍页，而是本工作分支的交付说明：记录这个分支解决什么问题、怎么做、怎么构建、怎么验收，以及最终达到了什么效果。

当前主仓库分支：

```text
issue/rtt-spi-lld-real-activation
```

当前主仓库交付提交：

```text
4786392220 AP_HAL_RTT: complete CUAV V5 acceptance path
```

RT-Thread 子模块交付提交：

```text
modules/rt-thread
df63617ad1 stm32: keep CUAV V5 hardware IWDG fed
```

## 目标

这个分支的目标是让 CUAV V5 上的 RT-Thread ArduPilot 具备完整的台架验收能力，而不是只做到能启动或能出心跳。

主要目标包括：

- USB CDC MAVLink 可稳定工作。
- 参数下载速度接近 ChibiOS 版本的使用体验，不能长时间卡住。
- MAVLink FTP 可正常读写 SDCard 和读取 `@PARAM/param.pck`。
- USB 复合设备中的 CDC 和 SLCAN 两个虚拟串口可同时使用。
- 标准 CAN 和 DroneCAN 可通过 USB SLCAN 调试。
- IMU、INS、磁力计、气压计、SDCard、日志等核心外设 driver-level 健康。
- OpenOCD/GDB 检查无 HardFault。
- 代码、测试脚本、证据和文档整理清晰，可提交和推送 GitHub。

## 已达到的效果

最终全量验收结果为 GREEN：

```text
results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/acceptance_suite.json
verdict=GREEN
reason=acceptance_suite_ok
```

关键指标：

```text
参数下载: 942/942, 1.601 s, 588.3 params/s, missing=0
参数持久化: GREEN
MAVLink FTP: GREEN
@PARAM/param.pck: decoded_count=942
DataFlash 日志下载: GREEN, 94208 bytes
OpenOCD/GDB: CFSR=0, HFSR=0, VTOR=0x08008000, 无 HardFault
```

外设 driver-level 验收：

```text
gyro: healthy
accel: healthy
mag: healthy
baro: healthy
logging: healthy
ATTITUDE: 有数据
EKF_STATUS_REPORT: 有数据
RAW_IMU: 有数据
SCALED_PRESSURE: 有数据
mag_norm_mgauss=382.9
press_abs_hpa=1001.59
```

USB SLCAN / 标准 CAN：

```text
results/execution/slcan_20260613T090627Z_ascii_after_full_acceptance/slcan_ascii_gate.json
verdict=GREEN
reason=slcan_ascii_bidir_ok
```

DroneCAN：

```text
results/execution/dronecan_20260613T091439Z_after_full_acceptance_fixed_device/pydronecan.json
verdict=GREEN
reason=node_status_seen
dronecan_version=1.0.27
NodeStatus events=9
source_node_id=10
health=0
mode=0
```

CDC + SLCAN 并存压力：

```text
results/execution/coexist_20260613T091529Z_after_full_acceptance/coexist_summary.json
verdict=GREEN
CAN traffic active
can_apm saw 9220 frames with ID 0x555
can_dbg saw 9220 frames with ID 0x555
CDC 参数下载: 942/942, 1.944 s, 484.6 params/s
MAVFTP: GREEN
```

## 仍需明确的边界

`strict_prearm` 仍然是 RED：

```text
driver_verdict=GREEN
calibration_verdict=RED
```

这不是驱动失败，而是物理飞行准备条件未闭合，例如 AHRS/pre-arm 校准、RC、安全开关等。因此本分支声明的是：

```text
CUAV V5 RT-Thread ArduPilot driver-level 软件台架验收通过。
```

本分支不声明：

```text
已经满足实机解锁起飞条件。
```

CAN 路径也要保持准确：

```text
当前台架物理 CAN 口通过 CAN_SLCAN_CPORT=2 路径为 GREEN。
CAN_SLCAN_CPORT=1 仍是 ACK error 红路径。
```

也就是说，当前接线的物理口在本 RT-Thread/ArduPilot 映射里走内部 CAN2。不能把 `CAN_SLCAN_CPORT=1` 说成已经修好。

## 做了什么

### 1. USB CDC / MAVLink

RTT/CherryUSB 的 CDC 路径对 ArduPilot 的假设进行了补齐：

- 区分 CDC buffer 所有权和 USB IN endpoint 完成时机。
- 处理满包结束时的 ZLP 行为。
- 避免参数下载和 MAVFTP 因调度或缓冲复用而卡住。
- 修正 MAVFTP 对 RTT 负 errno 的处理。
- 增加参数下载、MAVFTP、心跳稳定性的可复现测试脚本。

结果：

```text
CDC 参数下载稳定在 1.6-2.0 s 级别。
MAVFTP 可稳定 list/read/write/remove。
FTP 后心跳仍稳定。
```

### 2. Scheduler / Logger / Storage

RTT 的调度行为和 ChibiOS 不同。早期问题不是单个 USB bug，而是主循环、USB、logger、storage 之间的系统耦合。

本分支做了这些修复：

- 主循环周期性让出真实 RT-Thread tick，避免低优先级 storage/logger liveness 被饿死。
- 保持 logger IO 在线程优先级上的合理位置，不用粗暴升优先级掩盖问题。
- 强化 stack 检查，避免诊断代码自身造成 fault。
- 改进日志创建、列表、下载和恢复测试。

结果：

```text
DataFlash 日志 list/download/restore 为 GREEN。
SDCard 读写和 MAVFTP 读写均通过。
```

### 3. I2C / IST8310 磁力计

CUAV V5 的 IST8310 在 OpenOCD MCU-only reset 后可能处于不干净状态。ChibiOS 路径中很多启动时序和 I2C 恢复细节已经被长期打磨，RTT 需要补齐这些系统行为。

本分支做了这些处理：

- 传感器电源轨早期拉低。
- 在传感器断电窗口 clamp I2C3 的 PH7/PH8，避免 IST8310 被 I2C 保护路径弱供电。
- 使用确定性的 sensor rail off/on settle 时间。
- I2C timeout 后恢复 STM32 I2C 外设和 GPIO AF open-drain 状态。
- I2C bus clear 采用更接近 ChibiOS 的方式：释放 SDA、脉冲 SCL、生成 STOP、恢复 AF。
- IST8310 probe 增加 RTT 下的 WHOAMI 重试预算并使用低速 I2C。

结果：

```text
磁力计 driver health 为 GREEN。
RAW_IMU 磁场非零。
mag_norm_mgauss=382.9。
```

### 4. CAN / USB SLCAN / DroneCAN

USB 第二虚拟串口作为 SLCAN 调试口：

```text
/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if02
```

主机工具：

```text
slcand
candump
cansend
cangen
dronecan 1.0.27
```

本分支修复了 RTT CAN 发送邮箱超时恢复问题：

```cpp
txi.aborted = false;
txi.setup   = true;
txi.pushed  = false;
```

这避免无 ACK 发送后 TX mailbox 状态卡住。

当前默认参数：

```text
SERIAL6_PROTOCOL 22
CAN_P1_DRIVER 1
CAN_D1_PROTOCOL 1
CAN_P2_DRIVER 1
CAN_D2_PROTOCOL 1
CAN_SLCAN_CPORT 2
```

结果：

```text
USB SLCAN ASCII 标准 CAN 双向收发 GREEN。
SocketCAN/can-utils 路径 GREEN。
pydronecan NodeStatus 监听 GREEN。
CDC + SLCAN 并存压力 GREEN。
```

注意：官方 `dronecan 1.0.27` 使用 SocketCAN 时设备名应直接写：

```python
dronecan.make_node("can_dbg", node_id=127, bitrate=1000000)
```

不要写成：

```python
dronecan.make_node("can:can_dbg", ...)
```

后者会把 `can:can_dbg` 当作真实接口名并报 `OSError: [Errno 19] No such device`。

### 5. Hardware IWDG / RT-Thread 子模块

CUAV V5 使用硬件 IWDG option bytes，早期尝试重新配置 PR/RLR 可能受限或卡在同步状态。

RT-Thread 子模块中做了：

- 启动早期只 feed watchdog，不再依赖重新配置 PR/RLR。
- SysTick 中 feed IWDG，避免慢初始化期间 watchdog 触发。

子模块提交：

```text
df63617ad1 stm32: keep CUAV V5 hardware IWDG fed
```

## 如何构建

本分支禁止使用 waf 构建 CUAV V5 RTT 固件。使用 SCons：

```bash
python3 -m SCons --target=cuav-v5 -j$(nproc)
```

常用产物：

```text
build/rtt_cuav_v5/rtthread.bin
build/rtt_deploy/cuav_v5/rt-thread.elf
build/rtt_deploy/cuav_v5/arducopter.apj
```

## 如何烧录

CUAV V5 应用入口为：

```text
0x08008000
```

OpenOCD 直接烧录：

```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify" \
  -c "reset run" \
  -c "shutdown"
```

OpenOCD 操作后清理：

```bash
pkill -9 -x openocd 2>/dev/null || true
pkill -9 -f '[o]penoccd' 2>/dev/null || true
```

## 如何验收

所有手工测试脚本都建议用 `nohup + timeout`，并把输出放到 `results/execution/`。

### 全量串行验收

不要让多个 pymavlink reader 同时抢 CDC 口。推荐使用串行 suite：

```bash
TS=$(date -u +%Y%m%dT%H%M%SZ)
DIR="results/execution/acceptance_${TS}_cuav_v5_rtt"
mkdir -p "$DIR"

nohup timeout 960 python3 Tools/scripts/rtt_acceptance_suite.py \
  --port /dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 \
  --outdir "$DIR" \
  --include-param-persist \
  --strict-prearm-health \
  --allow-strict-prearm-red \
  > "$DIR/nohup.log" 2>&1 &
```

### USB SLCAN ASCII 验收

```bash
TS=$(date -u +%Y%m%dT%H%M%SZ)
DIR="results/execution/slcan_${TS}_ascii"
mkdir -p "$DIR"

nohup timeout 80 python3 Tools/scripts/rtt_slcan_ascii_gate.py \
  --board-port /dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if02 \
  --debugger-port /dev/serial/by-id/usb-STMicroelectronics_STM32_Virtual_ComPort_206E395C5446-if00 \
  --outdir "$DIR" \
  --frames 5 \
  --listen-s 0.8 \
  > "$DIR/nohup.log" 2>&1
```

### MAVFTP 验收

```bash
TS=$(date -u +%Y%m%dT%H%M%SZ)
DIR="results/execution/mavftp_${TS}"
mkdir -p "$DIR"

nohup timeout 180 python3 Tools/scripts/rtt_mavftp_gate.py \
  --port /dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 \
  --outdir "$DIR" \
  --require-sd \
  > "$DIR/nohup.log" 2>&1
```

## 证据文档

主要验收文档：

```text
docs/rtt-porting/CUAV_V5_RTT_ACCEPTANCE_CURRENT.md
docs/rtt-porting/CUAV_V5_RTT_USB_SLCAN_CURRENT.md
```

系统性根因文档：

```text
docs/rtt-porting/RTT_USB_CHIBIOS_CHERRYUSB_ROOT_CAUSE.md
```

关键证据目录：

```text
results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/
results/execution/slcan_20260613T090627Z_ascii_after_full_acceptance/
results/execution/dronecan_20260613T091439Z_after_full_acceptance_fixed_device/
results/execution/coexist_20260613T091529Z_after_full_acceptance/
```

废弃或被替代的过程文件保存在：

```text
results/recycle_bin/
```

## 工作区规则

本分支的工作区整理规则：

- 源码修复放在对应库目录。
- 可复现测试脚本放在 `Tools/scripts/`。
- 长期说明文档放在 `docs/rtt-porting/`。
- 有效测试证据放在 `results/execution/`。
- 废弃过程文件、旧日志和临时产物放在 `results/recycle_bin/`。
- 不把临时 PID、临时 candump、临时 setup.log 散落在仓库根目录。

当前交付时已经确认：

```text
无残留 slcand/candump/cangen/openocd 进程。
无残留 can_apm/can_dbg SocketCAN 接口。
无当前测试产生的根目录临时 CAN 文件。
主仓库和子模块均已提交并推送到 GitHub。
```

## 结论

本分支已经完成 CUAV V5 RT-Thread ArduPilot 的 driver-level 软件台架验收：

```text
USB CDC: GREEN
参数下载: GREEN
MAVLink FTP: GREEN
SDCard / 日志: GREEN
IMU / INS / ATTITUDE / EKF 数据流: GREEN
磁力计: GREEN
气压计: GREEN
USB SLCAN: GREEN
标准 CAN: GREEN
DroneCAN: GREEN
CDC + SLCAN 并存: GREEN
OpenOCD/GDB HardFault 检查: GREEN
```

保留边界：

```text
strict pre-arm 仍需真实飞行前校准、RC、安全开关闭合。
CAN_SLCAN_CPORT=1 仍是当前台架红路径。
```
