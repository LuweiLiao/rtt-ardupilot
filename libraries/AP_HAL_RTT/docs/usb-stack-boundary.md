# USB stack layout (AP_HAL_RTT)

**Goal:** one OTG_FS owner per image; vendor code under `thirdparty/`, board glue under `cherryusb_board/`, ArduPilot integration at HAL root.

## Layers

| Layer | Path | Role |
|-------|------|------|
| Vendor | `thirdparty/cherryusb/` | CherryUSB subset (core, CDC class, DWC2 port, RT-Thread OSAL). See `README.vendor`. |
| Board glue | `cherryusb_board/` | `usb_config.h`, `usb_dc_glue.c` — **OTG_FS_IRQHandler**, PHY/clock/pins for CUAV-class boards. |
| HAL shim | `hal_usb_cherryusb_shim.c` | Default production backend (`RTT_USB_BACKEND` unset or `cherryusb`): CDC read/write, init, ties UARTDriver to Cherry stack. |
| Native stack | `hal_usb_lld_rtt.c`, `usb_cdc_rtt.c` | Explicit fallback/backend isolation path (`RTT_USB_BACKEND=native`). |
| Backend selector | `Tools/scripts/rtt_usb_backend.py` | `filter_ap_hal_rtt_source()`, `cherryusb_extra_sources()`, BSP USB Kconfig undef when app owns USB. |

## Not in the build (archived / removed)

- Root-level duplicate Cherry trees (`class/`, `common/`, `core/`, `osal/`, `port/`, `cherryusb/`) — **must not** reappear; vendor lives only under `thirdparty/cherryusb/`.
- PoC stacks (`hal_usb_tinyusb_*`, root `usb_dc_glue.*`, `tusb_config.h`) — track in `.cursor/usb-cleanup-inventory.md` until clean patches land.

## Tests

`libraries/AP_HAL_RTT/test/usb/L6_cherryusb_cdc_echo/board/` may duplicate `cherryusb_board/` glue for isolated CherryUSB images (`scons --test=L7_cherryusb_cdc`); production full-build uses `cherryusb_board/` + `rtt_usb_backend.cherryusb_extra_sources()`. Legacy path `hwdef/common/tests/test_L7_cherryusb_cdc/` is a symlink.

## Follow-up (no SConscript change in layout pass)

- Root `hal_spi_lld.c` and `hal_spi_lld_rtt.c` are both picked up by root `*.c` glob — confirm only one should link (see `open-issues` USB cleanup).
- Optional: symlink or shared copy for L7 test glue vs `cherryusb_board/` to avoid drift (currently same content).
