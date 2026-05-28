# Stray full-tree RTT BSP (archived)

Legacy **whole BSP directories** that used to live at `libraries/AP_HAL_RTT/rtt_bsp_*`.
They are **not** used by the current **CUAV v5** scons baseline (`hwdef/common` + `hwdef/cuav_v5`).

| Directory | MCU / role | Build entry |
|-----------|------------|-------------|
| `rtt_bsp_pixhawk6c_mini` | STM32H743 Pixhawk6C Mini | scons legacy deploy (`rtt_bsp_deploy.py`); waf `rtt.py` |
| `rtt_bsp_fmuv2` | STM32F427 fmuv2 | waf `rtt.py` only (not in root `SConstruct` target list) |

**Policy:** Do not add new `rtt_bsp_*` trees under `libraries/AP_HAL_RTT/`. New boards should use `hwdef/<board>/hwdef.dat` + `hwdef/common`.

**Status:** Archived for history and optional legacy waf/scons paths. **Not** hardware-validated on the current HAL/USB stack; migration to `hwdef/common` is open work.
