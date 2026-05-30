# AP_HAL_RTT documentation (non-build)

This tree holds audit notes, porting plans, and USB layout docs. Nothing here is compiled by `scons_ardupilot_sources.py`.

## HAL root layout (build policy)

- **HAL sources** stay at `libraries/AP_HAL_RTT/*.cpp` and `*.c` (board-agnostic ArduPilot link).
- **CUAV v5 (current baseline):** `hwdef/cuav_v5/hwdef.dat` + `hwdef/common/` template; deployed by `Tools/scripts/rtt_bsp_deploy.py` (hwdef mode). **No** `rtt_bsp_*` directory at HAL root.
- **Legacy full-tree BSP** (Pixhawk6C Mini, fmuv2): moved to `libraries/AP_HAL_RTT/archive/stray-bsp/`. Still referenced by waf `rtt.py` and legacy `pixhawk6c_mini` scons deploy only. Archiving does **not** mean those boards are verified on the current tree.

| Path | Purpose |
|------|---------|
| `audits/` | Module alignment / audit markdown from bring-up |
| `refs/` | Long-form porting and design notes |
| `usb-stack-boundary.md` | Vendor vs glue vs HAL shim (canonical USB layout) |
| `../archive/stray-bsp/` | Archived `rtt_bsp_pixhawk6c_mini`, `rtt_bsp_fmuv2` |
