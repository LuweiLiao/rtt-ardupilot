# AP_HAL_RTT archive (non-build, retained)

Files moved here are **not** referenced by `SConscript`, `scons_ardupilot_sources.py`, or `#include` from build-critical sources. Nothing is deleted.

| Subdir | Contents |
|--------|----------|
| `stray-bsp/` | Legacy full-tree `rtt_bsp_pixhawk6c_mini`, `rtt_bsp_fmuv2` (not CUAV v5 baseline) |
| `backups/` | `.bak` snapshots (USB LLD, SPI CMSIS experiment) |
| `spi-cmsis/` | Alternate `SPIDevice` CMSIS experiment (`.cmsis` suffix) |

To restore a file for comparison, copy back to the HAL root manually; do not wire into the build without review.
