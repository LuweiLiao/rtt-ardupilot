# IWDG Watchdog 反复复位修复计划

## 目标

终结 CUAV V5 RTT 固件的硬件 IWDG 反复复位循环，使 `main()` → `hal.run()` 能稳定执行到 `_main_loop_entry()`，不再被 512ms 超时打断。

---

## 根因诊断（已验证）

| 环节 | 诊断结果 | 证据 |
|------|----------|------|
| `main()` 来源 | ✅ ArduCopter/Copter.cpp | 删除模板 main.c 后正确链接 |
| Flash 内容 | ✅ 与 ELF 一致 | OpenOCD mdw + objdump 比对一致 |
| `main()` 被调用 | ✅ 断点 0x08012430 命中 | GDB + OpenOCD hw breakpoint ✅ |
| `hal.run()` vtable | ✅ 0x0806d291 正确 | `HAL_RTT::run()` 地址验证通过 |
| **`rtt_dbg_hal_run_called`** | **❌ 0xDEADBEEF**（初始值） | 第273行 `0xAAAAAAAA` 从未写入，或写入后被复位清空 |
| **`rtt_dbg_main_loop_entry_called`** | **❌ 0xCAFEBABE**（初始值） | 同上 |
| RCC_CSR 复位标志 | ✅ 已清除（RMVF 执行过） | `0x40023874 = 0x00000003` |
| IWDG 寄存器 | 全0 | 复位后寄存器回到默认值 |
| 当前 CPU 位置 | **空闲线程清理 defunct 线程** | 主线程已终止 |

### 根本原因

**硬件 IWDG 在 CUAV V5 上默认激活**（FLASH_OPTCR_BYTE2 的 IWDG_SW = 0），从 POR 开始就以 **~512ms 超时**运行：

```
POR → IWDG启动(512ms) → 启动加载器(100-300ms) → RT-Thread init → 
rt_components_init(sensor/mmcsd/finsh/...) → 调度器启动 → 
main线程 → main() → hal.run() → ap_rtt_iwdg_init() → 第一次喂狗
```

这个链路过长（尤其是 `rt_components_init` 中的 SD 卡/文件系统/设备初始化），导致在第一次喂狗（`IWDG_KR=0xAAAA` 第148行）**之前** IWDG 就已超时复位。系统反复重启。

### 为什么 `rtt_dbg_hal_run_called` 是 DEADBEEF

`hal.run()` 第273行写入 `0xAAAAAAAA` 正好在 `ap_rtt_iwdg_init()` 之前。如果这一行执行了，全局变量会持久化到下一次复位。但读取结果是 DEADBEEF，说明**在进入 `hal.run()` 之前系统就复位了**，也就是 `main_thread_entry → rt_components_init` 过程中就超时了。

或者：`hal.run()` 执行了第273行 `=0xAAAAAAAA`，但在后续 IWDG 初始化/其他 init 期间超时复位，然后重新启动回到 DEADBEEF。

---

## 方案对比

### 方案A（推荐）：启动链最早喂狗

**思路**：在 RT-Thread 启动链的最早期加入 IWDG 喂狗，而不是等到 `hal.run()` 才喂。

**修改点**：
1. `board/rt_board_init.c` → `rt_hw_board_init()` 末尾：先喂 IWDG + 拉长超时
2. `modules/rt-thread/src/` 启动汇编（若需要更早）

**优点**：
- 改动最小、风险最低
- 不修改 RT-Thread 内核源码
- 遵循 ChibiOS `__late_init()` 模式

**缺点**：
- board init 中 LSI 可能还未稳定（但 `ap_rtt_iwdg_init` 会等 LSIRDY）
- 需要确认 board init 的时序

### 方案B：启动汇编喂狗

**思路**：在 Reset_Handler 或 startup 汇编中，在跳转到 C 代码之前直接喂狗。

**修改点**：
1. `modules/rt-thread/libcpu/arm/cortex-m7/startup_gcc.s`：在进入 `main`/`entry` 前插入 IWDG 寄存器操作

**优点**：
- 最早的喂狗时机（几乎是复位后第一条指令）
- 不受任何 C 初始化影响

**缺点**：
- 修改 RT-Thread 内核汇编文件（需要小心链接顺序）
- LSI 可能还没启动，第一次喂狗后得不到延长

### 方案C：关闭硬件 IWDG

**思路**：修改 FLASH_OPTCR 选项字节，将 IWDG_SW 设为 1（软件模式），这样 IWDG 就不会自动启动了。

**修改点**：
1. 通过 OpenOCD 或 bootloader 修改 option bytes
2. 或者代码中调用 FLASH_OB_Unlock/Program 在启动时修改

**优点**：
- 彻底消除硬件 IWDG 问题
- 看门狗完全由软件控制

**缺点**：
- **修改 option bytes 有砖机风险**
- 必须通过 OpenOCD 或专用工具操作
- 一次性的物理修改，调试期间够用但不适合生产

### 方案D（目前代码中已有但太晚）：`ap_rtt_iwdg_init()`

目前的位置（`hal.run()` 第279行）**太晚**。需要在它之前至少多喂一次狗。

---

## 选定方案：方案A（启动链喂狗）

### Step 1：读当前 board init 文件

```
文件: /data/firmare/pogo-apm/modules/rt-thread/bsp/stm32f7xx/board/rt_board_init.c
```

确认 `rt_hw_board_init()` 的末尾位置。

### Step 2：在 board init 末尾加入 IWDG 喂狗

在 `rt_hw_board_init()` 中，在 `rt_components_board_init()` 调用之后，插入：

```c
/* IWDG early feed — hardware IWDG starts at reset with ~512ms timeout.
 * Must feed BEFORE rt_components_init() runs (which takes >512ms to
 * init sensor power, DFS, MMCSD, SDIO, FATFS, FINSH, etc.).
 * This gives us up to 10s before the next feed is needed,
 * enough for all board-level initialization. */
#define IWDG_KR    (*(volatile uint32_t *)0x40003000)
#define IWDG_PR    (*(volatile uint32_t *)0x40003004)
#define IWDG_RLR   (*(volatile uint32_t *)0x40003008)
#define IWDG_SR    (*(volatile uint32_t *)0x4000300C)

/* Feed first — IWDG may be about to expire */
IWDG_KR = 0xAAAA;

/* Enable LSI (may already be on from bootloader) */
RCC->CSR |= RCC_CSR_LSION;
volatile uint32_t lsi_wait = 1000000;
while (!(RCC->CSR & RCC_CSR_LSIRDY) && --lsi_wait) {}

/* Unlock PR/RLR */
IWDG_KR = 0x5555;

/* Prescaler /256, reload 1250 → ~10s timeout */
IWDG_PR = 6;
IWDG_RLR = 1250;

/* Wait for sync */
volatile uint32_t sync_wait = 1000000;
while ((IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) && --sync_wait) {}

/* Final feed with new timeout */
IWDG_KR = 0xAAAA;
```

### Step 3：同步 `ap_rtt_iwdg_init()`

`hal.run()` 中的 `ap_rtt_iwdg_init()` 不再需要重复配置 IWDG（因为 Step 2 已经做了），但要保留它作为**定期喂狗**机制（因为 `Scheduler::set_system_initialized()` 会缩小超时到2s，且后续 main loop 需要喂狗）。

- 将 `ap_rtt_iwdg_init()` 简化为只喂狗（`IWDG_KR = 0xAAAA`），不移除它
- 或者保留完整版本但变成幂等操作（IWDG 配置只做一次，之后只喂狗）

### Step 4：编译 + 烧录 + 验证

```bash
cd /data/firmare/pogo-apm
scons --v=ArduCopter --target=cuav_v5 -j$(nproc)
# 烧录
openocd ... program build/rtt_cuav_v5/rtthread.bin 0x08008000
# 验证：断点在 main()，观察是否稳定，rtt_dbg_hal_run_called 读值
```

### Step 5：验证结果

| 检查项 | 预期 |
|--------|------|
| `rtt_dbg_hal_run_called` | **0xAAAAAAAA 或 0xBBBBBBBB** |
| `rtt_dbg_main_loop_entry_called` | **0x12345678** |
| 主线程存活 | 不再被 IWDG 复位中断 |
| 系统稳定运行 >30s | ✅ |
| OpenOCD 保持连接 | 不复位 |

---

## 涉及的文件

| 文件 | 操作 | 风险 |
|------|------|------|
| `modules/rt-thread/bsp/stm32f7xx/board/rt_board_init.c` | **修改**—末尾加 IWDG 喂狗 | 低（仅添加，不修改已有逻辑） |
| `libraries/AP_HAL_RTT/system.cpp`（`ap_rtt_iwdg_init`） | 可选简化—变为幂等 | 低 |
| `libraries/AP_HAL_RTT/HAL_RTT_Class.cpp`（`run()`） | 不改—已有 `ap_rtt_iwdg_init()` | 不改 |

---

## 风险与回退

- **风险**：board init 中插入喂狗可能与其他外设初始化冲突
- **回退**：git checkout 该文件即可恢复
- **备选方案**：若方案A无效（board init 本身已耗光512ms），降级到方案B（启动汇编喂狗）

---

## 后续步骤（Issue #2：IWDG 正常运行时定期喂狗）

在 `_main_loop_entry` 的 `while(1){loop()}` 中，需要**每次循环喂狗**。目前只有在 `set_system_initialized()` 时配置了2s超时，但 main loop 中没有定期喂狗。

这会在 IWDG 修复后暴露为第2个问题：main loop 跑久了会因 IWDG 超时而复位。但这是一个独立的 issue，不影响当前修复。

---

## 时间估计

| 步骤 | 估计 |
|------|------|
| Step 1-2: 代码修改 | ~5分钟 |
| Step 3: 编译 | ~3分钟 |
| Step 4: 烧录验证 | ~10分钟 |
| Step 5: 稳定性测试 | ~5分钟 |
| **总计** | **~25分钟** |
