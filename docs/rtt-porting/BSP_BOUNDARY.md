# CUAV V5 rt-thread BSP boundary

Resolves the contradiction: *"is `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5`
a deploy artifact or a maintained fork source?"*

## A. Generated artifact or real source?

**Both — but per-file, not per-directory.** The directory is a committed source
BSP in the fork (`LuweiLiao/ardu-rtthread` `staging/pogo`, 62 tracked files), and
the scons deploy depends on some of its files. But the canonical source of the
board-init / SPI-LLD files is `libraries/AP_HAL_RTT/hwdef/common/`, not the fork.

| File (under `bsp/stm32/stm32f765-cuav-v5/`) | Role | Canonical source | Used by scons build? |
|---|---|---|---|
| `board/CubeMX_Config/Src/stm32f7xx_hal_msp.c` | **fork source (required)** | fork BSP | yes — `rtt_bsp_deploy.py` `msp_src_rel` overlay |
| `board/ports/*` (cherryusb, sdcard_port, phy_reset) | **fork source (required)** | fork BSP | yes — `ports_rel` overlay |
| `board/rt_board_init.c` | **deploy/sync target (mirror)** | `hwdef/common/board/rt_board_init.c` | **no** — build uses hwdef/common copy |
| `board/drv_spi_lld.c` / `.h` | deploy/sync target (mirror) | `hwdef/common/board/drv_spi_lld.c` | no — build uses hwdef/common copy |
| `board/linker_scripts/link.lds`, `rtconfig.h` | deploy/sync target | `hwdef/common` | no — regenerated/synced |
| `bsp/stm32/libraries/HAL_Drivers/drivers/drv_spi.c` | **fork source (required)** | fork BSP (no hwdef/common copy) | yes — compiled, linked `drv_spi.o` |

Evidence:
- `Tools/scripts/rtt_bsp_deploy.py` (cuav_v5, hwdef mode): copies `hwdef/common`
  → `build/rtt_deploy/cuav_v5`, then overlays only `msp_src_rel` + `ports_rel`
  from the fork BSP. It never copies the fork's `rt_board_init.c`.
- `build/rtt_deploy/cuav_v5/board/rt_board_init.c` is byte-identical to
  `hwdef/common/board/rt_board_init.c` (and differs from the fork BSP copy by
  ~208 lines — the fork copy is a stale mirror).
- `Tools/ardupilotwaf/rtt.py` `_deploy_cuav_v5_bsp_if_needed` syncs
  `board/rt_board_init.c`, `drv_spi_lld.c`, `link.lds`, `rtconfig.h` **from**
  `RTT_BSP_CUAV_V5_SRC = libraries/AP_HAL_RTT/hwdef/common` **into** the fork BSP.

## B. If artifact: should `4cb13abd8c` be reverted/migrated?

`4cb13abd8c` ("floor-c: enable SPI1 spi_lld register on CUAV V5 board init")
edits only the fork's `board/rt_board_init.c`. That file is a **mirror**, so the
edit is **functionally dead for the scons build** — the build reads
`hwdef/common/board/rt_board_init.c`, where the same functional change already
lives as floor C `6bf787cdf9`.

Decision: **no revert / no force-push.** It is a harmless redundant mirror, not a
source of truth, and reverting it would rewrite already-pushed fork history. The
canonical change is `6bf787cdf9` (hwdef/common). The fork mirror may be left as-is
or refreshed by the next waf `_deploy_cuav_v5_bsp_if_needed` sync.

## C. If source: why did the README call it an artifact?

The old `README.md` over-generalized: it said the whole `stm32f765-cuav-v5` dir is
"构建时部署生成、不提交". That is wrong — the dir IS committed in the fork, and the
scons deploy depends on its `stm32f7xx_hal_msp.c` + `board/ports/*`. The accurate
statement is the per-file split in table A.

Fix commit: `docs(rtt-porting): clarify CUAV v5 BSP boundary` on
`clean/rtt-spi-full-lld` (this file + README §"rt-thread 子模块 BSP 边界").

## D. Which source actually generates `rt_board_init.c` for clean full-LLD?

The reverify product (`build/rtt_deploy/cuav_v5/rtthread.bin`, flashed via
st-flash) is built by **scons**. Its `rt_board_init.c` comes from:

```
libraries/AP_HAL_RTT/hwdef/common/board/rt_board_init.c   (floor C 6bf787cdf9)
  → rtt_bsp_deploy.py copytree(hwdef/common, build/rtt_deploy/cuav_v5)
  → build/rtt_deploy/cuav_v5/board/rt_board_init.c   (byte-identical)
```

The fork BSP `bsp/stm32/stm32f765-cuav-v5/board/rt_board_init.c` (`4cb13abd8c`)
is **not** on this path.

## E. Minimal fix (no code change)

1. Correct `README.md` to the per-file boundary (done).
2. Add this `BSP_BOUNDARY.md` as the authoritative reference (this file).
3. Going forward, board-init / drv_spi_lld changes for CUAV v5 land **only** in
   `hwdef/common`; treat the fork BSP `rt_board_init.c` / `drv_spi_lld.c` as
   sync targets, never edit them as source.

No firmware code change, no branch reset, no force-push.
