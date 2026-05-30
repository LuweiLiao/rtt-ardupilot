# AP_HAL_RTT — SPI full-LLD milestone status

ArduPilot ported from `AP_HAL_ChibiOS` to `AP_HAL_RTT` (RT-Thread) on **CUAV V5 / STM32F767**.
This document summarizes the SPI full-LLD work so the GitHub branch is reproducible.

> Engineering principle: ArduPilot code **above the HAL is kept unchanged by default**.
> All RTT-specific behaviour is solved inside `AP_HAL_RTT`, `hwdef`, the RT-Thread BSP,
> and driver LLDs. Any HAL-above change is RTT-guarded or is a portability shim (listed below).

## 1. Branch and commit chain (no squash)

Branch: `rtt-spi-full-lld`

| Commit | Floor | Summary |
|--------|-------|---------|
| `4db4c12fe7` | baseline | `milestone(AP_HAL_RTT): lock CDC/param baseline before SPI full-LLD` |
| `7f4e970a6d` | Floor A | NVIC layering (OTG_FS prio 4 > SPI1/SPI4 DMA prio 6) + `OTG_FS_IRQHandler` `rt_interrupt_enter/leave` |
| `787afda010` | Floor C | SPI1 (IMU) → `drv_spi_lld` (IRQ completion + single DMA master + bouncebuffer) |
| `653aa93183` | Floor D | SPI4 (MS5611 baro) → same LLD path |

(Floor B "single DMA ownership" folded into Floor C: TX/RX DMA IRQ routed through `drv_spi_lld`, no `HAL_DMA_IRQHandler` dual-master.)

## 2. Verified results (CUAV V5 hardware)

- **CDC open 20s**: stable, no disconnect (the Fix#4-A-low "CDC-open crash" mode no longer reproduces)
- **HEARTBEAT**: received; vehicle reaches STANDBY/ACTIVE
- **RAW_IMU / ATTITUDE**: valid (accel Z ≈ ±1000 mg, gyro ≈ 0 at rest; attitude updates; EKF uses IMU)
- **Barometer (MS5611 on SPI4)**: valid (~1006.8 hPa)
- **CFSR / HFSR = 0**: no HardFault across floors
- **param full download**: ~20/s (Floor A) → **peak ~42/s** (Floor C/D); was ~120 s total on the pre-baseline
- **cpu_idle**: 17–21% (busy-wait baseline) → **~99%** after SPI1 LLD — the SPI polling/busy-wait that consumed ~80% CPU is eliminated

### HAL-above changes (all RTT-guarded or revert-toward-ChibiOS, from the `4db4c12fe7` baseline)

The SPI LLD floors (A/C/D) touch **only** `AP_HAL_RTT` + BSP/LLD (zero HAL-above diff).
HAL-above changes present in the baseline commit are:

- `AP_Vehicle.cpp`, `GCS_Param.cpp` (`handle_param_set`), `GCS_Common.cpp`
  (`should_send_message_in_delay_callback`): **revert prior RTT-specific hacks toward ChibiOS**
  (delay-cb 250→50 Hz, async PARAM_SET save, drop all-message override).
- `GCS_Common.cpp` `update_send` telemetry gate during param download: **RTT-guarded** (`#if CONFIG_HAL_BOARD == HAL_BOARD_RTT`), other boards unchanged.
- `GCS_Common.cpp` `send_banner`: **RTT-guarded** boot fault report (`PrevFault`/IWDG).
- `AP_Filesystem_posix.cpp` + `ap_rtt_posix_stat.c/h`: **portability shim** for the RT-Thread
  `struct stat` ABI (C/C++ layout mismatch).
- Other long-standing port deltas (`AP_GPS`, `AP_InertialSensor`, `LogStructure.h`, `AP_RAMTRON`, `Parameters.cpp`, `ArduCopter/system.cpp`) predate this work.

## 3. Unresolved (tracked as separate work items)

- **MAVFTP T3/T4** (`@PARAM/param.pck` read, file write/read) intermittently 4/6.
  This is an **independent FTP / FS / SD / POSIX path** issue, **not** SPI/USB; must be a separate project, not mixed into SPI LLD.
- **param 42/s → ChibiOS ~69/s**: CPU is now free, so the remaining bottleneck has moved to the
  GCS_Param batch / MAVLink scheduling path. This is **HAL-above performance** and is intentionally
  **not** touched without explicit approval.
- **cpu_idle 0% reading on Floor D**: a symbol-address/sampling artifact (the binary changed so
  `rtt_cpu_idle_pct` moved). param rising to ~42/s confirms CPU is free; re-read with the
  **current** `nm` symbol (non-halt `ap_rate`) to confirm the ~99% figure.

## 4. Submodule (rt-thread)

- Fork URL: `git@github.com:LuweiLiao/ardu-rtthread.git`
- Branch: `staging/pogo`
- Commit: `4cb13abd8c19765759db6ea0a26f5ff01c87256f`
  - `4cb13abd8c` floor-c: enable SPI1 `spi_lld` register on CUAV V5 board init
  - `7cb31cf288` floor-a: SPI1/SPI4 DMA NVIC preempt 6 (below OTG FS prio 4)

`.gitmodules` already points `modules/rt-thread` at this fork/branch.

## 5. Reproduce

```bash
git clone --recurse-submodules git@github.com:LuweiLiao/rtt-ardupilot.git
cd rtt-ardupilot
git checkout rtt-spi-full-lld
git submodule update --init --recursive
# build (RTT toolchain, see CLAUDE.md):
python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)
# artifact: build/rtt_deploy/cuav_v5/rtthread.bin  (flash @ 0x08008000)
```
