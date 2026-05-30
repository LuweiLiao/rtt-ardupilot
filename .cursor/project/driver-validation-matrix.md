# Driver Validation Matrix

本文件是 **执行清单**：记录各驱动/模块的验证层级、`--test=` 或 example 名、**构建状态**、**上板状态**、硬件需求。

- 方法论与六层模型：`docs/AP_HAL_RTT_DRIVER_VALIDATION.md`
- Skill 入口：`.cursor/skills/rtt-driver-validation/SKILL.md`
- Manifest 解析：`Tools/scripts/rtt_test_manifest.py`
- Canonical 测试树：`libraries/AP_HAL_RTT/test/README.md`

**状态图例**

| 状态 | 含义 |
|------|------|
| **已构建** | `scons --test=…` 链接成功（未声称上板通过） |
| **待构建** | 目录/manifest 已有，构建未在本矩阵确认 |
| **待实现** | 仅规划名，目录或 SConscript 未就绪 |
| **已上板通过** | 有烧录/运行证据（须另附 trace；本表默认不填除非已验收） |
| **阻塞** | 缺硬件、缺驱动能力或已知缺陷 |
| **N/A** | 当前板 hwdef 无此能力 |

---

## A. STM32 internal（`bringup/` / `usb/`）

| `--test=` | 对象 | 构建状态 | 上板状态 | 外接硬件 | 备注 |
|-----------|------|----------|----------|----------|------|
| `L0_system` | SysTick / FPU / fault | **已构建**（2026-05-28） | 未验证 | 无 | |
| `l0_boot` | 最小启动 | 待构建 | 未记录 | 无 | legacy 名 |
| `L1_iwdg` | IWDG | 待构建 | 未记录 | 无 | |
| `L2_gpio` | GPIO 读写 | 待构建 | 未记录 | 无 | |
| `L3_uart` | UART 寄存器（USART1/6/7 等） | **已构建**（2026-05-28） | 未验证 | 无 | **非** `UARTDriver`；用户点名「串口」寄存器层 |
| `L4_spi` | SPI1 寄存器 + ICM20689 WHO_AM_I | **已构建**（2026-05-28） | 未验证 | 无（板载 IMU） | 用户点名「SPI」寄存器/板载 ID 层 |
| `L5_usb` | native USB 寄存器 | 待构建 | 未记录 | USB 线 | `_legacy_native/`，调试 only |
| `L6_cdc` | native CDC PoC | 待构建 | 未记录 | USB 线 | 非生产门禁 |
| `L7_cherryusb_cdc` | CherryUSB CDC echo | **已构建**（2026-05-28；2026-05-29 复跑） | **已上板通过**（2026-05-29） | USB 线 | 生产 USB 栈门禁；`1209:5741 Generic L7 CherryUSB`，ttyACM1 echo `HELLO_L7` OK，CFSR/HFSR=0 |
| `L5_i2c`（规划） | I2C 寄存器 / ACK 扫描 | **待实现** | — | 无 | 用户点名「IIC」底层；manifest **未登记** |
| `L6_sdmmc`（规划） | SDMMC 控制器初始化 | **待实现** | — | **需 microSD** | 用户点名 SD 卡底层 |

---

## B. HAL abstract（`test/drivers/D*` — 规划）

| 规划 `--test=` | 对象 | 构建状态 | 上板状态 | 外接硬件 | 参考 example |
|----------------|------|----------|----------|----------|--------------|
| `D_scheduler` | `Scheduler` / timer process | **真实 HAL smoke 构建通过**（2026-05-29；9 项复跑 PASS） | **已上板通过**（2026-05-29） | 无 | UART7 `timer callback count=504`、`[D_SCHEDULER] RESULT: PASS`；CFSR/HFSR=0 |
| `D_uart_hal` (`D_uart`) | `UARTDriver` TX/RX | **真实 HAL smoke 构建通过**（2026-05-29 门禁 8/8） | **已上板通过**（2026-05-29） | UART7 控制台（CH343 ttyACM0） | `hal.serial(6)` begin/printf/write；**证据**：烧录 app @0x08008000 Verified OK；UART7 115200 见 `[D_UART_HAL] RESULT: PASS`；OpenOCD **CFSR/HFSR=0**；**限制**：无 RX/loopback；无 `scheduler->init()` |
| `D_spi_hal` (`D_spi`) | `SPIDevice` / 设备表 | **真实 HAL smoke 构建通过**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 ICM20689（SPI1） | `get_device("icm20689")` reg `0x75`→**WHO_AM_I=0x98**；UART7 **`[D_SPI_HAL] RESULT: PASS`**；**CFSR/HFSR=0**；**限制**：无 MS5611/FRAM/DMA |
| `D_i2c_hal` (`D_i2c`) | `I2CDevice` / bus | **真实 HAL smoke 构建通过**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 IST8310 I2C3 @0x0E | WAI `0x00`→**0x10**；UART7 **`[D_I2C_HAL] RESULT: PASS`**；**CFSR/HFSR=0**；无 `AP_Compass` 全栈 |
| `D_storage` | `Storage` 后端 | **真实 HAL smoke 构建通过**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 FRAM（cuav_v5 **RAM stub**）；**不测 SD** | tail-8B scratch RW+restore；UART7 `[D_STORAGE] RESULT: PASS`，readback pattern OK，CFSR/HFSR=0；**限制**：非 FRAM 持久 |
| `D_rcoutput` | `RCOutput` / PWM | **真实 HAL smoke 构建通过**（2026-05-29） | **上板未验证** | **无桨**；示波器可选 | CH0 1000–1200 µs；`read`/`read_last_sent`；**限制**：PWM 波形未验证 |
| `D_analogin` | `AnalogIn` | **真实 HAL smoke 构建通过**（2026-05-29；printf/通道修正后复跑 PASS） | **已上板通过**（2026-05-29） | 无（板载 ADC） | UART7 `ch6 raw=2064`、`voltage_latest=3326 mV`、`adc_diag conv=108`；CFSR/HFSR=0；**边界**：`test_printf` 无 `%f`/`%u`（用 `%lu`/mV）；**非**校准/全通道量测验收；`scheduler->init()` 未调用 |
| `D_rcinput` | `RCInput` | **真实 HAL smoke 构建通过**（2026-05-29） | **上板未验证** | **需 SBUS/PPM 源** | `rcin` API + 3s poll；无通道→**TEST_FAIL**；**限制**：`AP_RCPROTOCOL_ENABLED=0`；SBUS 协议栈未验 |
| `D_usb_serial` | OTG `UARTDriver` + CDC | **已构建**（2026-05-29；HAL runtime） | **UART7 PASS**；CDC 主机待验 | USB | `rtt_test_hal_usb_serial_link.py` + `test_stubs_no_usb.c`；与 L7 分工：L7=裸栈 echo，D=`hal.serial(0)` |

---

## C. External module（`test/drivers/E*` — 规划）

| 规划 `--test=` | 模块 | 总线 | 构建状态 | 上板状态 | 外接硬件 | 参考 |
|----------------|------|------|----------|----------|----------|------|
| `E_imu` | ICM20689 | SPI1 | **已构建**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 | UART7 `WHO_AM_I reg 0x75 = 0x98`、`PWR_MGMT_1 = 0x40`、`[E_IMU] RESULT: PASS`；CFSR/HFSR=0；**非** AP_InertialSensor 全栈 |
| `E_ms5611` | MS5611 | SPI4 | **已构建**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 | PROM/CRC；UART7 `RESULT: PASS`；fault=0 |
| `E_ist8310` | IST8310 罗盘 | I2C3 | **已构建**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 | WAI 0x10；raw x=-75 y=47 z=17；UART7 `[E_IST8310] RESULT: PASS`；CFSR/HFSR=0；**非** AP_Compass |
| `E_fram` | FM25V02 FRAM | SPI2 | **已构建**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 | SPI2 CMSIS 轮询修复；RDID id 0x22/0x08 + 4B RW+restore；RDSR=0；fault=0 |
| `E_sdcard` | microSD | SDMMC1 | **SD/FS smoke 构建通过**（2026-05-29） | **已上板通过**（2026-05-29） | **必须插卡** | 首轮暴露 SDIO 供电时序 + `/sdcard` 重复挂载冲突；修复后 `/` 挂载 + `/APM` POSIX RW/unlink PASS，`stage=10 result=0`，CFSR/HFSR=0；不等于长稳 logging |
| `E_sbus` | SBUS 接收 | UART/GPIO | **待实现** | — | **必须接收机** | `RCProtocolTest` |
| `E_wspi_flash` | WSPI/QSPI Flash | QUADSPI | **构建通过**（2026-05-29；运行时 N/A） | **N/A**（cuav_v5） | 对应 H7 板 | cuav_v5 打印 N/A + `TEST_PASS`；**CUAV V5 hwdef 无 WSPI** |

---

## D. Subsystem smoke（`test/subsystem/S*`）

| `--test=` | 链 | 构建状态 | 上板状态 | 外接硬件 | 备注 |
|-----------|-----|----------|----------|----------|------|
| `S_param_storage` | Storage（参数后端） | **已构建**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 | UART7 `boundary=HAL Storage only`、`[S_PARAM_STORAGE] RESULT: PASS`；CFSR/HFSR=0；tail-16B scratch；**非**完整 `AP_Param` |
| `S_sensors` | ICM20689 + MS5611 芯片 | **已构建**（2026-05-29） | **已上板通过**（2026-05-29 trace） | 板载 | 薄组合 `E_imu`/`E_ms5611` 边界；**无** INS/Baro 全栈 |
| `S_mavlink_usb` | USB CDC + MAVLink | **已构建**（2026-05-29） | **上板 PASS**（2026-05-29） | USB | HAL `serial(0)` + `mavlink_msg_heartbeat_pack` @921600；`tests/rtt_test_S_mavlink_usb_host.py`；CFSR/HFSR=0；**非**整机 L0/参数 |
| `S_compass` | AP_Compass + IST8310 | **已构建**（2026-05-29） | **已上板通过**（2026-05-29） | 板载 | `IST8310 found`；field x≈140–142 z≈50–52；UART7 `[S_COMPASS] RESULT: PASS`；CFSR/HFSR=0；`COMPASS_MOT=0`；**非** cal/GCS |
| `S_rc_chain` | RCIn + RCOut | **排除** | — | — | 用户要求 RC 暂缓；**无** manifest/目录 |

---

## E. Host / static（`H*` — 规划，非 `--test=` 或独立 CI）

| 名称 | 对象 | 状态 | 备注 |
|------|------|------|------|
| `H_hwdef_parse` | `hwdef.dat` → 宏/表 | **待实现** | Python 或 `libraries/AP_HAL_RTT/tests/` |
| `H_spi_device_table` | `SPIDEV` / devid / CS | **待实现** | 防回归设备表错误 |

---

## F. 用户点名总线 — 快速对照（CUAV v5）

| 用户术语 | 寄存器层（L*） | HAL 层（D*） | 外置器件（E*） | 当前矩阵摘要 |
|----------|----------------|-------------|----------------|--------------|
| **串口 / UART** | `L3_uart` **已构建**（2026-05-28） | `D_uart_hal` **已上板通过**（2026-05-29） | — | UART7 `RESULT: PASS`；无 RX/loopback |
| **SPI** | `L4_spi` **已构建**（2026-05-28） | `D_spi_hal` **已上板通过**（2026-05-29） | `E_imu` **已上板通过**；`E_ms5611`/`E_fram` **已上板通过**（2026-05-29）；`S_sensors` 构建 PASS、上板见 trace | ICM20689 WHO_AM_I **0x98**；fault=0 |
| **IIC / I2C** | `L5_i2c` 待实现 | `D_i2c_hal` **已上板通过**（2026-05-29） | `E_ist8310` **已上板 PASS**；`S_compass` **已上板 PASS**（2026-05-29） | IST8310 WAI **0x10**；E=chip raw；S=`AP_Compass` field |
| **SDCard** | `L6_sdmmc` 待实现 | `D_storage` **已上板通过**（2026-05-29；RAM stub scratch） | `E_sdcard` **已上板通过**（2026-05-29；`/APM` POSIX RW） | SD 供电提前 + 去重 `/sdcard` 挂载后 PASS；长稳 logging 另测 |
| **WSPI** | — | `D_wspi` N/A | `E_wspi_flash` **构建通过**（cuav_v5 **N/A**） | **CUAV V5 无 WSPI** |
| **RCOut** | （TIM 可并入 L*） | `D_rcoutput` **HAL smoke 构建通过**（2026-05-29） | `E_esc` 可选 | PWM 波形未验证 |
| **RCIn** | — | `D_rcinput` **HAL smoke 构建通过**（2026-05-29） | `E_sbus` 待实现 | 须 SBUS/PPM；无源 FAIL |

---

## G. 旧表（首批 smoke 名 — 与 D/E 映射）

| 原 smoke 名 | 映射 | 验证层级 | 当前状态 |
|-------------|------|----------|----------|
| `scheduler-smoke` | `D_scheduler` | HAL abstract | **待实现** |
| `uart-smoke` | `D_uart_hal` | HAL abstract | **已上板通过**（2026-05-29）；UART7 `RESULT: PASS` |
| `usb-cdc-smoke` | `L7` + `D_usb_serial` | L7 已上板通过；D HAL UART7 PASS | `L7_cherryusb_cdc`：ttyACM echo OK；`D_usb_serial`：UART7 PASS + write 26B；**ACM 横幅/echo 主机侧待验** |
| `spi-ms5611-smoke` | `E_ms5611` | External | **已构建**（hwdef `SPIDEV ms5611` 已恢复，2026-05-29 构建 PASS；**上板未验证**） |
| `imu-whoami-smoke` | `E_imu`（`L4_spi` 部分覆盖寄存器级） | External / L4 | L4 **已构建**；E_imu **已上板通过**（chip WHO_AM_I）；`D_spi_hal` 构建未回归 |
| `storage-smoke` | `D_storage` / `E_fram` | HAL + Ext | `D_storage` **已上板通过**（RAM stub）；`E_fram` **已上板通过**（2026-05-29 SPI2 修复） |
| `pwm-output-smoke` | `D_rcoutput` | HAL abstract | **HAL smoke 构建通过**（2026-05-29）；PWM 波形未验证 |
| `analogin-smoke` | `D_analogin` | HAL abstract | **待实现** |

---

## 使用规则

- 从 **L\*** 通过后，再实现同总线的 **D\***，再 **E\***，再 **S\***
- 状态从「待实现」→「待构建」→「已构建」→「已上板通过」依次升级；不得跳过「已构建」直接宣称通过
- 某 `--test=` 实现完成时：先添加 `test/.../SConscript`，再更新 `rtt_test_manifest.py` 的 `TEST_LAYOUT`
- 当前稳定整机事实仍以 `status.md` 为准；本矩阵 **不** 把整机 MAVLink 等同于 driver 门禁通过
