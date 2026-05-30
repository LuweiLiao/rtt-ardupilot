# D*/E*：BUILD_ONLY → 真实 HAL smoke 短执行计划

> **版本**：2026-05-29（计划文档；Batch A–C 源码替换与 **8/8 串行构建 PASS** 已完成，见 `status.md`）  
> **前提**：8 个 `--test=` 已为 **真实 HAL/FS smoke** 且 **2026-05-29 构建门禁 PASS**；**未上板**。**不得**将构建通过等同于驱动验收（见 `driver-validation-matrix.md`）。  
> **方法论**：`docs/AP_HAL_RTT_DRIVER_VALIDATION.md`；**执行清单**：`.cursor/project/driver-validation-matrix.md`。

## 总原则

1. **一次只替换一个 `--test=`**（或同总线内最多 2 个紧密相关项），改完 → 构建 → 上板 → 矩阵升级，再进下一项。
2. **先 D（HAL API）再 E（器件）**：同总线须 `L*` 已构建；Batch A 的 E_imu/E_ms5611 可后置到 A 末尾或并入 Batch A 子阶段。
3. **成功判据分两级**：**构建通过**（`scons --test=` exit 0）≠ **HAL smoke 通过**（UART7/msh 或 Finsh 可观测步骤 + `TEST_PASS`/`TEST_ASSERT`，且调用真实 `hal.*` API）。
4. **manifest**：**不新增**已登记 CLI 名；新目录须 **先 `SConscript` 再 `rtt_test_manifest.py`**（见 `driver-validation-matrix.md` 使用规则）。

## Manifest / 命名策略

| 类型 | 规则 | 示例 |
|------|------|------|
| **已登记 CLI 名** | 保持不动，只替换目录内实现 | `D_uart_hal`、`D_spi_hal`、`D_i2c_hal`、`D_rcoutput`、`D_rcinput`、`E_sdcard`、`E_wspi_flash` |
| **矩阵别名** | 文档可用短名 `D_uart`/`D_spi`，CLI 以 manifest 为准（带 `_hal` 后缀） | matrix 行 ↔ `TEST_LAYOUT` 键一致 |
| **本阶段新增** | 新目录 + 新键，禁止 fallback 到 `hwdef/common/tests/test_*` | `D_storage` → `('drivers','D_storage')`；`E_fram` → `('drivers','E_fram')` |
| **DefineGroup** | 与 manifest 键对应：`test_D_uart_hal` 等 | 与现有 `test/drivers/*/SConscript` 一致 |
| **cuav_v5 N/A** | 运行时 `TEST_STEP` 打印 N/A + `TEST_PASS`（构建门禁），**不**声称上板通过 | `E_wspi_flash` 在 F767 保持 N/A 直至 H7 板 |

---

## Batch A — 低风险板载 HAL：`D_uart_hal` / `D_spi_hal` / `D_i2c_hal`

**目标**：最小真实 `AP_HAL` 调用，不依赖外接线（板载 SPI/I2C 器件；UART 用 USB CDC 或 UART7 控制台）。

| 项 | 参考 example / L* | 允许修改的文件 | 成功判据（HAL smoke） | 构建命令 | 实机 | 硬件阻塞 |
|----|-------------------|----------------|----------------------|----------|------|----------|
| **A1 `D_uart_hal`** | `AP_HAL/examples/UART_test` | `test/drivers/D_uart_hal/main.c` 或 `main.cpp`；`SConscript`（链入 HAL + Scheduler 最小集）；`test/_common/test_stubs.c` 仅当缺符号；**禁止**改 `UARTDriver.cpp` 除非 smoke 暴露明确 bug | `hal.scheduler->delay` 后 `hal.serial(0)` 或 `hal.console`：`begin(57600)` + `printf` 一行；UART7 或 USB 串口可见输出；`test_runner` 多步 `TEST_ASSERT` 非单步 BUILD_ONLY | `scons --target=cuav_v5 --test=D_uart_hal -j$(nproc)` | **是**（看串口） | 无；可选 TELEM 回环为增强项非门禁 |
| **A2 `D_spi_hal`** | `L4_spi` WHO_AM_I 逻辑 + `SPIDevice` API | `test/drivers/D_spi_hal/*`；`SConscript`；可增 `spi_hal_smoke.cpp`；设备表来自 hwdef，**不**在 test 硬编码 CS | `get_HAL().spi->get_device(...)` 或等价：对板载 ICM20689/MS5611 读 WHO_AM_I/PROM 首字节，值与 datasheet/`L4_spi` 一致；失败 `TEST_FAIL` | `scons --target=cuav_v5 --test=D_spi_hal -j$(nproc)` | **是** | 无（板载）；须保证 SPI1 各 CS 默认高（与 status 经验一致） |
| **A3 `D_i2c_hal`** | `AP_Compass_test` / IST8310 ID | `test/drivers/D_i2c_hal/*`；`SConscript` | `I2CDevice` 读 IST8310 chip ID（I2C3）；ID 匹配预期；非 BUILD_ONLY 打印 | `scons --target=cuav_v5 --test=D_i2c_hal -j$(nproc)` | **是** | 无（板载罗盘） |

**Batch A 顺序**：A1 → A2 → A3（UART 先确认 `console` 路径，便于后续 msh 观测）。

**矩阵升级**：每项实机通过后，该行「上板状态」→ **已上板通过**；「构建状态」保持 **已构建**。

---

## Batch B — 存储链：`D_storage` + `E_sdcard` 或 `E_fram`

**目标**：`Storage` API 与 FRAM/SD 介质分离验证；避免无卡阻塞启动。

| 项 | 参考 example | 允许修改的文件 | 成功判据（HAL smoke） | 构建命令 | 实机 | 硬件阻塞 |
|----|--------------|----------------|----------------------|----------|------|----------|
| **B1 `D_storage`**（**新建**） | `AP_HAL/examples/Storage` | **新建** `test/drivers/D_storage/`（`main.cpp`、`SConscript`）；登记 `rtt_test_manifest.py`；`test/README.md`、matrix 行 | `hal.storage->init()`；按 example 对 `HAL_STORAGE_SIZE` 分块 `read_block` 算 XOR；UART7 打印 XOR 值；二次启动 XOR **一致**（FRAM 持久）或文档化 RAM-backend 例外 | `scons --target=cuav_v5 --test=D_storage -j$(nproc)` | **是** | 无（板载 FRAM SPI2）；若仍走 RAM stub 须在 `main` 头注释 + matrix 标明 |
| **B2a `E_fram`**（**新建**，推荐先于 SD） | `Storage` + `StorageTest` 语义 | **新建** `test/drivers/E_fram/`；manifest `E_fram` | SPI2 FM25V02：读 device ID + 单页写读校验 | `scons --target=cuav_v5 --test=E_fram -j$(nproc)` | **是** | 无 |
| **B2b `E_sdcard`**（替换占位） | `AP_Filesystem/examples/File_IO` | `test/drivers/E_sdcard/*`；`SConscript` | **必须插 microSD**；`INIT_APP_EXPORT` 或显式 mount 后：挂载成功、创建/读写小文件、卸载；**禁止** 60s 同步阻塞主路径；失败明确 `TEST_FAIL` 原因 | `scons --target=cuav_v5 --test=E_sdcard -j$(nproc)` | **是** | **必须插卡**；历史 SDMMC1 无响应风险（open-issues）— 无卡则标记 **阻塞**，不伪造 PASS |
| **B2 二选一策略** | — | — | 至少 **FRAM 路径（B2a）** 先闭环；SD（B2b）在卡/硬件就绪后做 | — | — | SD 为 **硬件阻塞项** |

**Batch B 顺序**：B1 `D_storage` → B2a `E_fram` → B2b `E_sdcard`（SD 可并行排期但不得阻塞 B1/B2a）。

---

## Batch C — RC 链：`D_rcoutput` / `D_rcinput`

**目标**：PWM 与 RC 解码走真实 HAL；强调安全边界。

| 项 | 参考 example | 允许修改的文件 | 成功判据（HAL smoke） | 构建命令 | 实机 | 硬件阻塞 |
|----|--------------|----------------|----------------------|----------|------|----------|
| **C1 `D_rcoutput`** | `AP_HAL/examples/RCOutput` | `test/drivers/D_rcoutput/*`；`SConscript`；若需 IOMCU：`AP_BoardConfig` 最小桩（参考 example `#if HAL_WITH_IO_MCU`） | `enable_ch` + `write` 扫描 1000–2000µs；UART7 打印 increasing/decreasing；**示波器**任一路 PWM 频率/脉宽变化，或安全默认 CCR=1500 仅验证 init 无 fault | `scons --target=cuav_v5 --test=D_rcoutput -j$(nproc)` | **是** | **示波器或无桨 ESC**；未解锁低脉宽为预期；**禁止带桨上电** |
| **C2 `D_rcinput`** | `AP_HAL/examples/RCInput` | `test/drivers/D_rcinput/*`；`SConscript` | `num_channels()>0` 且 `read(i)` 在接 SBUS/PPM 时变化；无接收机时允许 **SKIP** 步骤（非 BUILD_ONLY）并打印 `No channels` | `scons --target=cuav_v5 --test=D_rcinput -j$(nproc)` | **是** | **必须 SBUS/PPM 接收机或模拟器**；无源则运行时验证 **阻塞** |
| **C3（可选）`E_sbus`** | `RCProtocolTest` | 新建 `E_sbus/`，manifest 新增 | 帧同步 + 通道中立位 | `scons --target=cuav_v5 --test=E_sbus -j$(nproc)` | **是** | 接收机；**本短计划不强制**，可放在 C2 之后 |

**Batch C 顺序**：C1 → C2（输出不依赖输入）。

---

## Batch D — WSPI：`E_wspi_flash`（CUAV V5 N/A → H7）

| 项 | 参考 | 允许修改的文件 | 成功判据 | 构建命令 | 实机 | 硬件阻塞 |
|----|------|----------------|----------|----------|------|----------|
| **D1 `E_wspi_flash` @ cuav_v5** | `jedec_test` | `test/drivers/E_wspi_flash/main.c`：保留 **N/A 文档化** 多步说明，**删除**「仅 BUILD_ONLY 即 PASS」误导 | 构建 PASS；运行打印 `QUADSPI N/A on cuav_v5` + `TEST_PASS`；matrix **上板=N/A** | `scons --target=cuav_v5 --test=E_wspi_flash -j$(nproc)` | 否（N/A） | hwdef 无 WSPI |
| **D2 `E_wspi_flash` @ H7** | 同上 | 在 **pixhawk6c_mini**（或 H7 hwdef 板）同目录实现真实 QSPI JEDEC ID | JEDEC ID 匹配 flash 型号；页读写 smoke | `scons --target=pixhawk6c_mini --test=E_wspi_flash -j$(nproc)` | **是**（H7 板） | **板卡**：需 H7 + WSPI 布线；legacy BSP 在 archive，迁移 hwdef 未验证 |

**Batch D 顺序**：D1（文档化 N/A，与 Batch A–C 并行无依赖）→ D2 待 H7 bring-up 里程碑。

---

## 推荐全局执行顺序

```text
Batch A: D_uart_hal → D_spi_hal → D_i2c_hal
Batch B: D_storage (new) → E_fram (new) → E_sdcard
Batch C: D_rcoutput → D_rcinput [→ E_sbus optional]
Batch D: E_wspi_flash@F767(N/A doc) ∥ 任意时刻；E_wspi_flash@H7 延后
```

每项闭环后：**行动者**更新 `driver-validation-matrix.md` + `agent-trace`；稳定上板结论由父代理验收后写入 `status.md`。

## 子任务简报模板（父代理 → 下一行动者）

每项替换 BUILD_ONLY 时派发单测简报，包含：

- **允许路径**：仅该 `test/drivers/<name>/` + `_common` + `SConscript` + manifest（若新建）
- **禁止**：同时改 `UARTDriver`/`SPIDevice`/整机 `ArduCopter`；未验证前改 HAL 核心
- **完成判据**：上表「HAL smoke」列 + 构建 exit 0
- **验证**：OpenOCD 无 HardFault + UART7 日志（CDC 仅当测 serial0 时）

## 风险摘要

| 风险 | 缓解 |
|------|------|
| HAL 链接膨胀、ROM 超限 | 每测独立 `DefineGroup`，不拉全 ArduCopter；对照 L0 体量 |
| `test_stubs` 掩盖真实路径 | smoke 必须调用 `hal.storage`/`hal.serial` 等，禁止仅 `test_runner` 单步 |
| SD 无卡挂死 | `E_sdcard` 非阻塞 mount；无卡 `TEST_FAIL` 或 SKIP，不阻塞 Batch B 其余项 |
| RC 安全 | C1 文档化无桨；默认低油门 |
| 并行 scons 链接竞态 | 门禁脚本 **串行** `--test=`（见 open-issues） |
| WSPI 误报通过 | F767 仅 N/A；H7 才允许「已上板通过」 |

## 不在本阶段

- `D_scheduler`、`D_analogin`、`D_usb_serial`、`S_*` 子系统 smoke（后续里程碑）
- Cherry/native USB 默认化、全量 ArduCopter L0 回归（见 open-issues USB 节）
- Git commit（须用户明确要求）
