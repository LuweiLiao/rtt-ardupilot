# RTT ArduPilot CUAV V5 — MAVLink Boot 攻坚战计划

## 当前状态

### 已绕过（临时措施，工作正常）
| 绕过项 | 文件 | 状态 |
|--------|------|------|
| ADC 旁路 (return 0) | `AnalogIn.cpp` | ✅ |
| Storage 旁路 (RAM Stub) | `Storage.cpp` | ✅ |
| SPI DMA 禁用 (Polled) | `SPIDevice.cpp` | ✅ (571K transfers) |

### 当前阻塞点
**setup_stage=680** — IMU #0 (ICM20689) `start()` 挂死

SPI 传输正常（571K 次收发），但 IMU 驱动不返回。PC 在 `get_micros64()` 或 `rt_exit_critical` 之间切换，说明 delay() 或 semaphore 逻辑在跑但没进展。

### 根因假设（按可能性排序）

1. **SPI 轮询模式的时序问题**（最可能）
   - 之前用 DMA 时 ICM20689 能 init 成功
   - 改用 polled 后 SPI 读写时序不同（无 DMA 流水线），寄存器读回的值不对
   - WHO_AM_I 返回错误值 → driver 进入 internal retry 循环

2. **hal.scheduler->delay() 卡住**
   - RTT `delay_ms()` 在 ISR/Timer context 中不 yield
   - IMU 驱动多处 delay(100)/delay(5)/delay(1) 可能阻塞

3. **Semaphore 死锁**
   - `WITH_SEMAPHORE(_dev->get_semaphore())` 在 `start()` 内嵌套获取
   - 如果 `_fifo_reset()` 也尝试获取同一 semaphore → 死锁

---

## 方案 A：Hermes 本地调试（当前路线，我继续改代码）

### 步骤
1. **诊断 IMU start() 具体卡在哪**
   - 增加 `rtt_dbg_setup_stage` 标记点，细化定位到函数行
   - 或直接打 `::printf` 跟踪执行流

2. **尝试修复 SPI polled 模式**
   - 检查 SPI 寄存器配置 (CR1, CR2) 在与 polled 传输时的正确性
   - 特别是 speed/frequency 切换 (`set_speed()`)

3. **如果 IMU 无法修 → 彻底跳过**
   - hwdef 中设置 `define HAL_INS_PROBE_LIST ""` 或定义 `#define AP_INERTIALSENSOR_ALLOW_NO_SENSORS`
   - 让系统无 IMU 也能 boot 到 STANDBY + MAVLink

4. **验证标准**
   - setup_stage > 1000 (进入 loop)
   - MAVLink HEARTBEAT 稳定 (type=2, status=3)
   - 持续运行 30min 无 HardFault

### 风险
- 工程师疲惫（我自己改了 20+ 轮）
- 定位 IMU 驱动耗时长
- polled SPI 的根本问题（无 DMA）可能影响传感器数据质量

---

## 方案 B：引入 Claude Code 作为执行 Agent（你建议的方案）

### 工作流
```
Hermes (我)                Claude Code (新员工 CC)
  │                              │
  ├─ 拆解任务                    │
  ├─ 写 spec/plan                │
  ├─ kanban 调度                 │
  │                              │
  ├──→ [dispatch] 修复 SPI ──────┤ → inspect→修改→验证
  ├──→ [dispatch] 修复 IMU ──────┤ → gdb跟踪→改代码→烧录
  ├──→ [dispatch] 打通 MAVLink ──┤ → pymavlink→验证→稳定
  │                              │
  ├─ 汇总结果                    │
  └─ 交付飞书                    │
```

### 具体任务拆分

#### Task 1: SPI polled 模式稳定性修复
- **目标**：SPI polled 传输 100% 可靠，寄存器读回值正确
- **步骤**：
  1. 对比 ChibiOS SPI 传输代码和 RTT 的 `spi1_poll_transfer()`
  2. 检查 CS 极性、SPI clock 空闲状态 (CPOL/CPHA)
  3. 添加 SPI 传输后的寄存器验证 (readback check)
  4. 编译烧录 + 串口打印 SPI 读回值验证
- **验证**：读 ICM20689 WHO_AM_I 返回 0x89

#### Task 2: IMU ICM20689 start() 不返回修复
- **目标**：ICM20689 start() 正常返回，setup_stage 前进到 1000+
- **步骤**：
  1. GDB 单步跟踪 `start()` 执行到哪一步
  2. 检查 `_fifo_reset()`, `_register_write()`, `hal.scheduler->delay()`
  3. 修复发现的时序/锁问题
  4. 编译烧录验证
- **验证**：setup_stage > 1000, fast_loop_count > 0

#### Task 3: 无 IMU 后备方案（若 Task 2 失败）
- **目标**：至少让 MAVLink 跑起来
- **步骤**：
  1. hwdef 中彻底注释所有 IMU
  2. 或 `AP_InertialSensor.cpp` 中跳过 _start_backends()
  3. 编译验证
- **验证**：MAVLink HEARTBEAT 稳定输出, status=STANDBY

#### Task 4: 完整验证 + 飞书交付
- **目标**：固件稳定运行，交付文件
- **步骤**：
  1. 编译最终固件
  2. OpenOCD 烧录 + GDB 10分钟无 HardFault
  3. pymavlink 持续采集 100 个 HEARTBEAT
  4. 飞书发送 `.bin` + 验证报告

---

## 关键决策点

| 问题 | 建议 |
|------|------|
| 先修 SPI 还是先跳过 IMU？ | 先跳过 IMU 拿到 MAVLink（快），再回头修 IMU |
| Claude Code 要不要装？ | **建议装**——CC 有完整终端能直接 gdb/openocd，比我这里受限 sandbox 强 |
| kanban 还继续用吗？ | **用**——Hermes 管 kanban 调度，CC 只执行具体任务，结果汇报到 kanban |

## 文件变更清单
| 文件 | 修改 |
|------|------|
| `libraries/AP_HAL_RTT/AnalogIn.cpp` | ✅ ADC bypass (保留) |
| `libraries/AP_HAL_RTT/Storage.cpp` | ✅ Storage bypass (保留) |
| `libraries/AP_HAL_RTT/SPIDevice.cpp` | ✅ DMA disable (保留) |
| `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` | ⚠️ 需决定 IMU 配置 |
| `libraries/AP_HAL_RTT/..` (新增) | 按 Task 执行结果决定 |

---

## 我推荐立即执行

**计划**：先执行方案 A 第 3 步（彻底跳过 IMU）拿到 MAVLink，同时装好 Claude Code。拿到 MAVLink 基线后，再让 CC 去攻坚 IMU 驱动。

**30 分钟内能看到 MAVLink 心跳**，而不是继续在 IMU 泥潭里打转。

要我执行这个计划吗？
