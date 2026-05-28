# CherryUSB board glue (production)

Board-specific CherryUSB device stack glue for STM32F7 OTG FS (CUAV V5 class boards).

| File | Role |
|------|------|
| `usb_config.h` | CherryUSB compile-time options |
| `usb_dc_glue.c` | Device controller + **OTG_FS_IRQHandler** (single IRQ owner with shim) |
| `usb_dc_glue.h` | Glue declarations |

Linked when `RTT_USB_BACKEND=cherryusb` via `Tools/scripts/rtt_usb_backend.py` → `cherryusb_extra_sources()`.

Vendor sources: `../thirdparty/cherryusb/`. HAL CDC API: `../hal_usb_cherryusb_shim.c`.
