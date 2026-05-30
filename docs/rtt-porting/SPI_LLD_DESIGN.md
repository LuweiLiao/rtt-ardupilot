# AP_HAL_RTT — SPI full-LLD design

Authoritative design for the RT-Thread SPI low-level driver (LLD) path on
CUAV V5 / STM32F767, and the floor-A/C/D commits on branch `rtt-spi-full-lld`.

> ## ⚠️ STATUS CORRECTION (2026-05-30, hardware-verified)
> **The SPI LLD is NOT active on cuav_v5. The earlier claim "implemented and
> gate-passed, cpu_idle 17–21%→99% from LLD" is INVALID.**
>
> Hardware gate (commit 71b7cb40c2, reverted 8cc2a1ef0a) proved:
> `spi_lld_lookup(SPI1)=NULL`, `g_spi1_lld_stats.init_count=0`,
> `xfer_count` does not grow; SPI1 still runs the CMSIS poll path
> (`spi1_xfer_calls` ~1929/s).
>
> Root cause: the registration guard in `rt_board_init.c` is
> `#if defined(BSP_USING_SPI1) && defined(BSP_SPI1_TX_USING_DMA) && defined(BSP_SPI1_RX_USING_DMA)`.
> **`BSP_USING_SPIn` is never generated** (the `.config` `CONFIG_BSP_USING_SPI*`
> lines are commented out; `rtt_hwdef.py` emits only `RT_USING_SPIn` +
> `BSP_SPIn_*_USING_DMA`). So the guard fails on its FIRST term for **SPI1, SPI4
> AND SPI2** — `spi_lld_register()` for every bus is compiled out. The whole
> `drv_spi_lld` path is dormant code.
>
> **What actually runs:** `SPIDevice.cpp` CMSIS bypass (`_dev=nullptr` for bus
> 1/2/4) — register poll + busy-wait DMA for large fullduplex. IMU/Baro/FRAM all
> work this way; functional gates (RAW_IMU, EKF, MAVFTP 6/6, CFSR=0) pass on this
> path. cpu_idle measured ~99% AT REST on the CMSIS path (so the "SPI polling eats
> 80% CPU" premise is itself unverified and must be re-measured under load).
>
> The sections below remain the DESIGN/CONTRACT for IF the LLD is ever activated.
> Activation requires generating `BSP_USING_SPIn` so the guard fires — which also
> makes RT-Thread `drv_spi.c` instantiate the bus driver and may conflict with the
> CMSIS bypass. This must be measured/justified before attempting, not toggled.
> See `open-issues.md` "SPI1 Floor C 实际未激活".

This document also captures the hard rule from the failed Fix#4-B: **a fix lives
in the SPI HAL/LLD, never by reshaping AP_InertialSensor transactions.**

## 0. Baseline / rollback

Branch: `clean/rtt-spi-full-lld` (reviewable replay; historical checkpoint
`4db4c12fe7` is **not** the clean baseline — see `df5e85bb48`).

- Frozen baseline (P0+P1+Fix1+Fix3): `df5e85bb48`
  `milestone(AP_HAL_RTT): lock P0 P1 Fix1 Fix3 clean baseline before SPI LLD`
  (rt-thread submodule `d5dd08dda3` on `LuweiLiao/ardu-rtthread` `staging/pogo`)
- SPI LLD floors on `clean/rtt-spi-full-lld`:
  - `a5fca2b901` Floor A — NVIC layering + OTG ISR nest (rt-thread `7cb31cf288`)
  - `6bf787cdf9` Floor C — SPI1 (IMU) → drv_spi_lld (rt-thread `4cb13abd8c`)
  - `866388d9ac` Floor D — SPI4 (MS5611) → drv_spi_lld (rt-thread `4cb13abd8c`)
- Rollback (use **clean** floor predecessors only):
  - Undo Floor D only: `git reset --hard 6bf787cdf9`
  - Undo Floor C (+ D): `git reset --hard a5fca2b901`
  - Undo all SPI LLD floors: `git reset --hard df5e85bb48`
  - Partial file restore from baseline: `git checkout df5e85bb48 -- <paths>`

## 1. ChibiOS SPI LLD ↔ RTT SPI LLD comparison

| Concern | ChibiOS (`AP_HAL_ChibiOS/SPIDevice.cpp` + OTGv1 SPI LLD) | RTT (`AP_HAL_RTT/SPIDevice.cpp` + `drv_spi_lld.c`) |
|---------|----------------------------------------------------------|----------------------------------------------------|
| acquire_bus / release_bus | per-bus semaphore (`bus.semaphore`) | per-bus `rt_mutex` (DeviceBus) |
| set_chip_select | GPIO assert + `spiStart` config | `_spi_regs_configure()` + CS GPIO; `cs_held` reuse |
| transfer / transfer_fullduplex | `spiStartExchangeI()` then thread suspend | `_spi_dma_xfer()` → `_spi_lld_dma_xfer()` → `spi_lld_xfer()` |
| small transfer | same path | `_spi_poll_small()` (len ≤ `SPI_DMA_THRESHOLD`, TXE/RXNE poll) |
| DMA completion | DMA TC ISR → `osalThreadResumeI` (thread wakes) | RX DMA TC ISR (`spi_lld_dma_rx_irq`) → `rt_completion_done` |
| wait model | `osalThreadSuspendTimeoutS` (sleep, no CPU spin) | `rt_completion_wait(timeout)` (sleep, no CPU spin) |
| timeout | `TIME_MS2I` timeout on suspend | `rt_completion_wait` timeout (drv_spi_lld:334) |
| error recovery | abort DMA / reset peripheral | RX-stall workaround (drv_spi_lld:287-324), EP teardown (443-455), BSY `rt_thread_yield` (401-415) |
| DMA safety | `bouncebuffer_setup` + `mem_is_dma_safe` | `_spi_buf_dma_safe()` + `DeviceBus::bouncebuffer_setup/finish` |

Key prior defect (Fix#4-A-low): RTT used **CPU busy-wait on `DMA_SxCR_EN`** for
completion. That burned ~80% CPU (SPI1 IMU @1kHz) and, when combined with same
NVIC priority as OTG + TX dual-master, crashed USB CDC. The LLD path replaces the
busy-wait with ISR→completion sleep, matching ChibiOS semantics.

## 2. RTT SPI LLD target contract

1. **Zero change above the HAL.** `AP_InertialSensor`, `GCS_*`, `AP_Param`,
   `AP_Filesystem` production logic are not modified to work around SPI.
2. **`AP_HAL::SPIDevice` interface semantics unchanged** — `transfer`,
   `transfer_fullduplex`, `set_chip_select`, `get_semaphore` behave identically;
   only the internal completion mechanism changes.
3. All board/peripheral specifics live in `hwdef.dat`, the BSP (`rt_board_init.c`,
   `drv_spi*.c`), and the LLD — not hard-coded in HAL-portable paths.
4. New diagnostics must be switchable/guarded, not left on the main path.

## 3. STM32F767 SPI1/SPI4 DMA scheme

| Bus | Device | RX DMA | TX DMA |
|-----|--------|--------|--------|
| SPI1 | ICM/BMI IMUs | DMA2 Stream2 ch3 | DMA2 Stream5 ch3 |
| SPI4 | MS5611 baro | DMA2 Stream0 ch4 | DMA2 Stream1 ch4 |
| SPI2 | FRAM (RAMTRON) | none (polled) — LLD conversion deferred (low-freq, no DMA stream) |

- **ISR ownership**: SPI1/SPI4 RX/TX DMA IRQ vectors are owned by `drv_spi.c`,
  routed to `spi_lld_dma_rx_irq` / `spi_lld_dma_tx_irq` when `spi_bus_obj[].lld != NULL`.
  **No `HAL_DMA_IRQHandler` dual-master** on those streams while LLD is active.
- **ISR entry/exit**: all RT-Thread-scheduling ISRs (OTG_FS, SPI DMA) wrap their
  body in `rt_interrupt_enter()` / `rt_interrupt_leave()` so context switches are
  deferred correctly.
- **Wakeup**: `spi_lld_xfer()` arms DMA then `rt_completion_wait(timeout)`; the RX
  TC ISR calls `rt_completion_done()`. The SPI thread sleeps (no CPU spin).
- **Timeout / abort**: `rt_completion_wait` timeout (default 100ms) → disable
  stream, clear DMAEN, flush flags (drv_spi_lld teardown); RX-stall workaround
  drains residual bytes. Caller sees failure and retries.
- **Registration**: `rt_board_init.c` registers SPI1 (and SPI4) LLD and attaches
  `spi_bus_obj[].lld` **before** enabling the DMA NVIC vectors (avoids null-ptr
  HardFault — lesson from earlier SPI4 NVIC-too-early crash).

## 4. NVIC priority table (lower number = higher priority)

| IRQ | Priority | Rationale |
|-----|----------|-----------|
| SDMMC1 | 2 | SD real-time (unchanged) |
| SD DMA streams | 3 | SD DMA (unchanged) |
| **OTG_FS (USB)** | **4** | **must preempt SPI DMA so CDC is never starved** |
| main thread | 5 | RT-Thread thread prio |
| **SPI1/SPI4 RX/TX DMA** | **6** | **below OTG so USB CDC stays responsive** |

Rule: **OTG_FS priority strictly higher than SPI DMA.** This is the single change
that, together with the ISR-completion model, made the SPI LLD coexist with USB
(Fix#4-A-low crashed because OTG and SPI DMA were both prio 5 + TX dual-master).

## 5. D-cache / DMA coherency

- **Current build: D-cache is globally disabled** (`rt_board_init.c`), so no
  `SCB_CleanDCache`/`InvalidateDCache` is required today. DMA reads/writes are
  coherent by construction.
- **Buffer placement**: DTCM (`0x20000000`) is **not** DMA-accessible; SRAM1
  (`0x20020000`) is. The heap lives in SRAM1, so most driver buffers are DMA-safe.
- **`_spi_buf_dma_safe()`** validates address range + alignment; if a caller
  buffer is in DTCM / misaligned, `DeviceBus::bouncebuffer_setup/finish` copies
  through a DMA-safe SRAM1 bounce buffer.
- **If D-cache is re-enabled later** (future): TX buffers must be
  `SCB_CleanDCache_by_Addr` before arming; RX buffers `SCB_InvalidateDCache_by_Addr`
  after completion; both 32-byte aligned/padded, or routed through the bounce buffer.

## 6. Regression gate (must all pass; failure → rollback to milestone)

- CDC open: 20s connection, no disconnect
- HEARTBEAT received; STANDBY/ACTIVE reached
- RAW_IMU: accel Z ≈ ±1000 mg, gyro ≈ 0 at rest
- ATTITUDE: updating; EKF aligns and uses IMU
- Baro (SPI4): SCALED_PRESSURE valid (~1006 hPa)
- param full download ×3, report median (target: keep ≥ baseline; toward ChibiOS ~69/s)
- MAVFTP single round (record x/6; T3/T4 SD-FS issues tracked separately, not a SPI blocker)
- CFSR / HFSR = 0 (no HardFault)
- USB `rtt_uart_usb_diag_write_fails` not growing; `cherry_tx_*` epdis recovery ≈ 0
- `rtt_cpu_idle_pct` rises (busy-wait removed)

## Phase 4 step mapping (as executed)

| Step | Plan | As executed |
|------|------|-------------|
| 0 | instrumentation only | counters already present (`rtt_dbg_cherry_*`, `cpu_idle`, `spi1_xfer_calls`) |
| 1 | SPI1 DMA ISR completion, macro-gated | first attempted as `RTT_SPI_DMA_IRQ_WAIT` (failed → crashed USB) → superseded |
| — | IRQ-layer prerequisite | **Floor A** `a5fca2b901` (NVIC + ISR nest) — fixed the USB-crash root |
| 3 | enable on IMU, AP_InertialSensor unchanged | **Floor C** `6bf787cdf9` (SPI1 via `spi_lld_xfer`, zero AP_InertialSensor change) |
| 3b | extend to baro | **Floor D** `866388d9ac` (SPI4) |
| 4 | full gate, rollback on fail | **NOT achieved on cuav_v5** — see STATUS CORRECTION at top; LLD never registered (BSP_USING_SPIn absent), CMSIS path runs instead. "cpu_idle 99% from LLD" invalid. |

## Remaining (separate projects, not SPI-HAL)

- **SD root mount FailErrno** (blocks MAVFTP List/Create/T4) — SD/FS/DFS, unrelated to SPI/USB.
- **param 42→69/s** — CPU now free; bottleneck moved to GCS_Param/MAVLink scheduling (HAL-above; requires explicit approval to touch).
- **SPI2 (FRAM)** — low-frequency, no DMA stream; LLD conversion deferred (marginal benefit).
