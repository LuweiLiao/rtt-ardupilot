# AP_HAL_ChibiOS ↔ AP_HAL_RTT 文件对应矩阵

> 生成日期：2026-05-28  
> 范围：`libraries/AP_HAL_ChibiOS` 与 `libraries/AP_HAL_RTT` 顶层 HAL 源文件 + hwdef 生成链语义  
> 基线板：`CUAV v5` / `STM32F767`（见 `.cursor/project/status.md`）  
> **本文件为只读对齐索引，不替代实机构建/烧录验证。**

---

## 目的与使用方式

本矩阵用于 **HAL 语义对齐治理**：在改 RTT 代码前，先查 ChibiOS 侧职责、RTT 侧落点、差距与建议动作，避免无地图搬迁或误把 ChibiOS 内核依赖抄进 RT-Thread 树。

**推荐使用顺序：**

1. 按 ChibiOS 文件名在下方主表检索。
2. 看「状态」与「语义差距」判断是否真缺文件，还是已有结构替代。
3. 按「优先级」与文末「第一批推进顺序」排期；验证以「验证入口」列为准（脚本/分层测试/整机 gate，**不在本任务中执行**）。
4. 状态变化后同步 `.cursor/project/status.md` / `open-issues.md`，勿把过程推理写进本表。

---

## 状态分类定义

| 状态 | 含义 |
|------|------|
| **已有** | RTT 存在同名或明确 1:1 重命名文件，且主路径语义已对齐或可运行基线已覆盖 |
| **部分** | 有对应实现，但功能缺口、未实机验证、或分散在 BSP/LLD/其他文件 |
| **缺失** | 无对等源文件，且无已文档化的结构替代 |
| **结构替代** | 职责由不同文件名/模块承担（如 `DeviceBus` 代 `Device`、`HAL_RTT_Class` 代 `HAL_ChibiOS_Class`） |
| **不应照搬** | ChibiOS 实现绑定 ChibiOS/OSAL/生成物语义；RTT 应保留 RT-Thread + 寄存器/LLD + CherryUSB 路线，仅对齐 **AP_HAL 对外语义** |

---

## 对齐原则

1. **保留 RTT 运行时**：线程/调度/同步用 RT-Thread API；启动、时钟、IRQ 在 BSP（`hwdef/common/board`、`rtt_bsp_*`）与 LLD（`hal_*_lld_rtt.*`）完成。
2. **CherryUSB 为 USB 后端**：`RTT_USB_BACKEND=cherryusb` 时走 `hal_usb_cherryusb_shim.c` / `thirdparty/cherryusb`；**不**回退为 ChibiOS USB 栈。
3. **不照搬 ChibiOS 内核依赖**：`chibios_hwdef.py` 中 ChibiOS 专用 DMA/AF 解析、OS 对象等 **不** 原样复制；用 `rtt_hwdef.py` + `hwdef.dat` 生成同等 **宏/引脚/设备表** 语义。
4. **对齐边界是 AP_HAL 语义**：与 `AP_HAL_ChibiOS` 相同的类接口、调度节奏、参数/存储行为；底层可寄存器化（如 `Flash.cpp`、`system.cpp` 已采用）。
5. **单模块小步验证**：优先 `libraries/AP_HAL_RTT/test/`（`scons --test=L*`）与驱动矩阵，再整机 CDC+MAVLink+OpenOCD gate。

---

## 主表（52 项 HAL 文件 + hwdef 生成链）

| # | ChibiOS 项 | RTT 对应项 | 状态 | 语义差距 | 建议动作 | 优先级 | 验证入口 |
|---|------------|------------|------|----------|----------|--------|----------|
| 1 | `AnalogIn.cpp` | `AnalogIn.cpp` + `hal_adc_lld_rtt.c` | 部分 | F767 基线为轮询；头注释标明未来 DMA 走 `shared_dma`；电压通道与 `_timer_tick` 节奏需与 ChibiOS 对照 | 实装 DMA 采样时再接 `Shared_DMA`；统一 1kHz tick | P1 | `test_L0_system`；整机 `RAW_IMU`/电池电压 |
| 2 | `AnalogIn.h` | `AnalogIn.h` + `hal_adc_lld_rtt.h` | 已有 | 头文件接口一致 | 保持 API 同步即可 | P2 | 编译 + AnalogIn 单元路径 |
| 3 | `AP_HAL_ChibiOS_Namespace.h` | `HAL_RTT_Namespace.h` | 结构替代 | 命名空间宏 `ChibiOS`→`RTT` | 新增对外类型时同步两处 pattern | P3 | 全量 `scons` 编译 |
| 4 | `AP_HAL_ChibiOS_Private.h` | `DeviceBus.h` + 各驱动头直引 | 结构替代 | ChibiOS 用伞头聚合 include；RTT 无单一 Private 伞 | 可选增 `AP_HAL_RTT_Private.h`（仅 include 聚合，无 ChibiOS 依赖） | P3 | 编译 |
| 5 | `AP_HAL_ChibiOS.h` | `AP_HAL_RTT.h` | 已有 | HAL 工厂声明/板级钩子 | 与 `HAL_RTT_Class` 同步 | P2 | `scons --target=cuav_v5` |
| 6 | `bxcan.hpp` | `bxcan.hpp` | 已有 | 寄存器定义共享 | CAN 未实机验证时仍标驱动级风险 | P2 | CAN 启用后 `CanIface` 冒烟 |
| 7 | `CANFDIface.cpp` | — | 缺失 | `hwdef` 中 `HAL_CANFD_SUPPORTED 0` | 非 F767 主线；H7 板再 port，勿混入 CUAV 基线 | P4 | 目标板 hwdef + CAN FD 仪表 |
| 8 | `CANFDIface.h` | — | 缺失 | 同上 | 同上 | P4 | 同上 |
| 9 | `CanIface.cpp` | `CanIface.cpp` | 部分 | 源码已 port；**CAN 总线未在 status 标为已验证** | 按 open-issues 排 CAN 实机；对齐 ISR/邮箱语义 | P3 | CAN 外设 + MAVLink CAN 相关 |
| 10 | `CANIface.h` | `CANIface.h` | 部分 | 同上 | 同上 | P3 | 同上 |
| 11 | `Device.cpp` | `DeviceBus.cpp` | 结构替代 | 每总线线程 + bouncebuffer API 在 DeviceBus；**无** ChibiOS `shared_dma` 仲裁 | 文档化 bouncebuffer 与 DMA 归属；评估是否拆 `Device.cpp` 薄封装 | P1 | SPI/I2C 回调 + `SPIDevice` 压测 |
| 12 | `Device.h` | `DeviceBus.h` | 结构替代 | 类名 `DeviceBus` vs `Device` | 保持与 `AP_HAL::Device` 总线语义一致 | P1 | 同上 |
| 13 | `DSP.cpp` | `DSP.cpp` | 已有 | 非 CUAV 关键路径 | 按需回归 | P4 | 编译 |
| 14 | `DSP.h` | `DSP.h` | 已有 | 同上 | 同上 | P4 | 编译 |
| 15 | `Flash.h` | `Flash.h` + `Flash.cpp` | 已有 | RTT 寄存器化 erase/program（见 status） | 保持非阻塞 erase 语义 | P2 | 参数读写 + `Storage` |
| 16 | `GPIO.cpp` | `GPIO.cpp` | 已有 | `usb_connected()` 等已对齐语义 | 新板引脚仅改 hwdef/BSP | P2 | `test_L2_gpio` |
| 17 | `GPIO.h` | `GPIO.h` | 已有 | 同上 | 同上 | P2 | 同上 |
| 18 | `HAL_ChibiOS_Class.cpp` | `HAL_RTT_Class.cpp` | 已有 | 初始化顺序、外设 singleton | 改 init 链时对照 ChibiOS 表 | P1 | L0 gate / `init_ardupilot` 日志 |
| 19 | `HAL_ChibiOS_Class.h` | `HAL_RTT_Class.h` | 已有 | 同上 | 同上 | P1 | 同上 |
| 20 | `I2CDevice.cpp` | `I2CDevice.cpp` | 部分 | 硬件 I2C + bitbang 冲突曾用 hwdef 注释处理；IST8310 已通 | 继续 hwdef 驱动 I2C 表驱动化 | P2 | `test_L*` / 罗盘数据 |
| 21 | `I2CDevice.h` | `I2CDevice.h` | 已有 | 接口对齐 | 同上 | P2 | 编译 |
| 22 | `LogStructure.h` | `LogStructure.h` + `AP_Logger` 条件包含 | 已有 | `LOG_IDS_FROM_HAL_RTT` / `LOG_STRUCTURE_FROM_HAL_RTT`（MON/WDOG）；Scheduler 尚未写 MON 包 | 需要时在 Scheduler 写 MON（对照 ChibiOS） | P3 | Logger 消息枚举 |
| 23 | `RCInput.cpp` | `RCInput.cpp` | 部分 | SBUS 路径有；PPM 捕获依赖 `SoftSigReaderInt`；实机 RC 待验证 | 与 `SoftSigReader` 缺口一并排期 | P2 | `RC_CHANNELS` / 实机 SBUS |
| 24 | `RCInput.h` | `RCInput.h` | 已有 | 文档指向 SoftSigReaderInt | 保持注释与实现一致 | P3 | 编译 |
| 25 | `RCOutput_bdshot.cpp` | `RCOutput_bdshot.cpp`（占位+注释） | 部分 | 矩阵落点文件已建；FMU bdshot/DMA 未实现；CUAV 走 IOMCU | 从 ChibiOS 抽 bdshot+DMA 状态机；依赖 `shared_dma` | P1 | 电调 DShot 仪 / 示波器 |
| 26 | `RCOutput_iofirmware.cpp` | `RCOutput_iofirmware.cpp` + `RCOutput.cpp` + IOMCU | 结构替代 | 占位 cpp 文档化职责；应用固件 DShot/IOMCU 在 RCOutput*；IOMCU_FW 未移植 | IOMCU_FW 构建时再 port ChibiOS 体 | P1 | `MOTOR_OUTPUTS` / IOMCU 版本 |
| 27 | `RCOutput_serial.cpp` | `RCOutput_serial.cpp` | 部分 | DShot 命令多转发 IOMCU；本地 `dshot_send_command` 桩 | 实装或明确仅-IOMCU 策略 | P1 | DShot 命令 + `RCOutput_serial` |
| 28 | `RCOutput.cpp` | `RCOutput.cpp` | 部分 | PWM TIM 已初始化；DShot/bdshot 不全 | 与 #25–27 合并推进 | P1 | PWM 示波器 / MOTOR_OUTPUTS |
| 29 | `RCOutput.h` | `RCOutput.h` | 已有 | 接口声明对齐 | 随 cpp 变更更新 | P2 | 编译 |
| 30 | `Scheduler.cpp` | `Scheduler.cpp` + `rtt_ctl_telemetry.c` | 已有 | 线程模型已对齐；含调试遥测扩展 | 保持 `delay()`/`persistent_data` 语义 | P1 | L0 主循环 Hz / `MON` 类日志 |
| 31 | `Scheduler.h` | `Scheduler.h` | 已有 | 同上 | 同上 | P2 | 编译 |
| 32 | `sdcard.cpp` | `sdcard.cpp` | 部分 | 驱动/DFS 有；SDMMC 硬件响应见 alignment 备注 | 对照 ChibiOS SDMMC 初始化时序 | P3 | 日志写 SD / `test_*` |
| 33 | `sdcard.h` | `sdcard.h` | 已有 | 同上 | 同上 | P3 | 编译 |
| 34 | `Semaphores.cpp` | `Semaphores.cpp` | 已有 | `take(0)`/`take_blocking` 语义已对齐 status | 回归勿破坏 | P2 | 并发路径 / L0 |
| 35 | `Semaphores.h` | `Semaphores.h` | 已有 | 同上 | 同上 | P2 | 编译 |
| 36 | `shared_dma.cpp` | `shared_dma.cpp` | 部分 | RT-Thread mutex 最小 port；`HAL_RTT::run` 调 `init()`；驱动尚未普遍接入 | SPI/UART/bdshot 迁移时 `lock()`/`unlock()` | P1 | `test_L4_spi` + SPI 并发 |
| 37 | `shared_dma.h` | `shared_dma.h` | 部分 | API 对齐 ChibiOS；无 ChibiOS 类型 | 与 #36 同 PR；hwdef 可补 `SHARED_DMA_MASK` | P1 | 同上 |
| 38 | `SoftSigReader.cpp` | `SoftSigReader.cpp` | 结构替代 | ICU+DMA 未移植；头文件内联委托 `SoftSigReaderInt` | RCInput 继续用 `SoftSigReaderInt::init` | P1 | PPM/输入捕获示波器 |
| 39 | `SoftSigReader.h` | `SoftSigReader.h` | 结构替代 | `attach_capture_timer` 返回 false；`read`/`disable` 转发 Int | 同上 | P1 | 同上 |
| 40 | `SoftSigReaderInt.cpp` | `SoftSigReaderInt.cpp` | 已有 | 注释标明 ChibiOS 对照；IRQ 路径在 RTT | RCInput 联调 | P1 | `RCInput` + 捕获 IRQ |
| 41 | `SoftSigReaderInt.h` | `SoftSigReaderInt.h` | 已有 | 同上 | 同上 | P1 | 编译 |
| 42 | `SPIDevice.cpp` | `SPIDevice.cpp` + `SPIDeviceManager.cpp` | 部分 | 每总线 `rt_mutex` 已合入；与 `shared_dma` 协调未统一 | 在 shared_dma 后收敛 DMA 争用 | P1 | `test_L4_spi`；IMU/Baro 流 |
| 43 | `SPIDevice.h` | `SPIDevice.h` | 已有 | 设备表由 hwdef 生成 | hwdef 驱动化继续 | P2 | 编译 |
| 44 | `stdio.cpp` | `stdio.cpp` + `hwdef/common/stdio.h` | 已有 | RTT msh/console 路由 | 勿引入 ChibiOS `chn*` API | P3 | 控制台输出 |
| 45 | `Storage.cpp` | `Storage.cpp` | 已有 | Flash 后端已验证 | 保持 | P2 | 参数 904/904 |
| 46 | `Storage.h` | `Storage.h` | 已有 | 同上 | 同上 | P2 | 编译 |
| 47 | `system.cpp` | `system.cpp` | 已有 | `millis`/`panic`/Fault 已对齐 | 小步对照 ChibiOS 变更 | P1 | OpenOCD CFSR/HFSR |
| 48 | `UARTDriver.cpp` | `UARTDriver.cpp` + `hal_usb_*` / `usb_cdc_rtt.*` | 部分 | 文件头文档化 USART DMA vs CherryUSB 边界；DMA 表存在 | UART DMA 接入 `Shared_DMA`；USB 背压策略 | P1 | `test_L3_uart`；CDC MAVLink gate |
| 49 | `UARTDriver.h` | `UARTDriver.h` | 已有 | 同上 | 同上 | P2 | 编译 |
| 50 | `Util.cpp` | `Util.cpp` | 已有 | 工具/重入辅助 | 随 ChibiOS Util  diff 同步 | P1 | L0 / 故障注入 |
| 51 | `Util.h` | `Util.h` | 已有 | 同上 | 同上 | P2 | 编译 |
| 52 | `WSPIDevice.cpp` | `WSPIDevice.cpp` | 已有 | F767 少用；H7/外部 Flash 板更重要 | 按板级启用 | P4 | 目标板 WSPI |
| 53 | `WSPIDevice.h` | `WSPIDevice.h` | 已有 | 同上 | 同上 | P4 | 编译 |
| 54 | **hwdef 生成链（语义项）** | `hwdef/scripts/rtt_hwdef.py`；`hwdef/<board>/hwdef.dat`；`hwdef/common/SConscript`；`rtconfig.h`/`rt_pin_config.c`/`link.lds` 生成 | 部分 | ChibiOS：`chibios_hwdef.py`、`dma_parse.py`、`dma_resolver.py`、`bdshot_encoder.py`、`STM32*.py`、`defaults_*.h`；RTT 继承 `HWDef` 但输出 RT-Thread 产物 | 继续把 SPI/UART/PWM/ROMFS 留在 dat；**不应照搬** ChibiOS-only 脚本 | P1 | `rtt_hwdef.py` dry-run；`cuav_v5` 全量编译 |

> **项数说明**：上表 1–53 为清单内 HAL 文件；第 54 行为 **hwdef 生成链** 语义项。与任务「约 53 项」计数方式一致（52 文件 + 1 生成链）。

---

## 汇总统计

| 状态 | 数量 | 占比（54 项） |
|------|------|----------------|
| 已有 | 31 | 57% |
| 部分 | 16 | 30% |
| 缺失 | 2 | 4% |
| 结构替代 | 5 | 9% |
| 不应照搬 | 0 | — |

**说明：** 「不应照搬」作为原则体现在 #54 hwdef 与 USB 路线说明中，未单独占行计数。若将 hwdef 中 ChibiOS 专用脚本（`chibios_hwdef.py` 本体）视为「不应照搬」参照物，治理时对照第 54 行即可。

**仍缺失（2 项）：** `CANFDIface.cpp`、`CANFDIface.h`（非 CUAV F767 主线）。

**高影响仍待实装（已有落点/部分）：** `shared_dma` 驱动接入、FMU `RCOutput_bdshot`、PPM/RCInput 实机、`Device*`→`DeviceBus*`。

---

## 第一批建议推进顺序

与对齐治理计划一致，在 **不破坏 CUAV v5 L0 基线** 前提下：

| 顺序 | 主题 | 矩阵行 | 目标 |
|------|------|--------|------|
| 1 | **shared_dma** | #36–37, #42 | DMA 流仲裁或等价机制，解锁 SPI/bdshot/AnalogIn 并发 |
| 2 | **RCOutput bdshot / iofirmware / serial** | #25–28, #27 | DShot/bdshot 与 IOMCU 分工清晰；减少桩函数 |
| 3 | **SoftSigReader** | #38–41, #23 | PPM/捕获与 `SoftSigReaderInt` 缺口闭合 |
| 4 | **AnalogIn / UART / Util / system** | #1–2, #48–51, #47 | 采样与串口 DMA/USB 背压；系统时间与故障路径 |
| 5 | **hwdef 生成链** | #54 | `rtt_hwdef.py` 覆盖 SPI/UART/ROMFS，避免 HAL 硬编码回潮 |

每步完成后更新本表「状态」列，并将证据写入 `.cursor/project/status.md`（仅稳定结论）。

---

## 相关文件索引（RTT 侧扩展，不在 53 项内）

| 文件 | 与矩阵关系 |
|------|------------|
| `hal_adc_lld_rtt.c` | #1 AnalogIn 底层 |
| `hal_usb_lld_rtt.c` / `hal_usb_cherryusb_shim.c` | #48 UART/USB |
| `SPIDeviceManager.cpp` | #42 SPI 设备表 |
| `DeviceBus.cpp` | #11–12 代 Device |
| `hwdef/common/board/rt_board_init.c` | #54 BSP 初始化 |
| `libraries/AP_HAL_RTT/test/`（`scons --test=L*`） | 各列「验证入口」 |

---

## 变更记录

| 日期 | 说明 |
|------|------|
| 2026-05-28 | 初版：仓库内静态文件存在性扫描 + status/open-issues/alignment 交叉 |
| 2026-05-28 | 补齐 shared_dma、SoftSigReader、RCOutput_bdshot/iofirmware 占位、LogStructure.h；矩阵状态更新 |
