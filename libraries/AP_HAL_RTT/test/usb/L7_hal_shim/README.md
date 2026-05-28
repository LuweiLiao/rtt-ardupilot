# L7 — HAL CherryUSB shim (production path)

There is **no standalone module-test firmware** in this directory yet.

Production USB on CUAV V5 uses:

- `RTT_USB_BACKEND=cherryusb` (default for full ArduPilot after Cherry gate)
- `libraries/AP_HAL_RTT/hal_usb_cherryusb_shim.c`
- `libraries/AP_HAL_RTT/cherryusb_board/` board glue

## Validation

| Layer | Command / gate |
|-------|----------------|
| Isolated CherryUSB CDC echo | `scons --target=cuav_v5 --test=L7_cherryusb_cdc` → `test/usb/L6_cherryusb_cdc_echo/` |
| Full vehicle USB + MAVLink | CDC heartbeat + STANDBY + OpenOCD (L0 gate) |

Native register-level CDC (`L6_cdc`) lives under `test/usb/_legacy_native/L6_cdc/` and is **not** the production gate.
