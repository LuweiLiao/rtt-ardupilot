# AP_HAL_RTT layered module tests

Canonical sources for `scons --target=cuav_v5 --test=<name>` module-test firmware.

Resolution is implemented in `Tools/scripts/rtt_test_manifest.py` and wired from `hwdef/common/SConscript` (deployed BSP copies that file on `rtt_bsp_deploy`).

## Layout

```text
test/
  _common/              test_runner, stubs, app_descriptor
  bringup/
    l0_boot/            minimal boot (legacy name)
    L0_system/          SysTick, FPU, fault regs
    L1_iwdg/
    L2_gpio/
    L3_uart/
    L4_spi/
  usb/
    L6_cherryusb_cdc_echo/   CherryUSB CDC echo (production USB gate)
    L7_hal_shim/             README only — full-build shim validation
    _legacy_native/
      L5_usb/                register-level USB init (backend=none)
      L6_cdc/                native DWC2 CDC PoC (not production)
  drivers/              reserved: scheduler, uart, spi, imu, …
```

## scons names (`--test=`)

| `--test=` | Directory |
|-----------|-----------|
| `l0_boot` | `bringup/l0_boot/` |
| `L0_system` | `bringup/L0_system/` |
| `L1_iwdg` | `bringup/L1_iwdg/` |
| `L2_gpio` | `bringup/L2_gpio/` |
| `L3_uart` | `bringup/L3_uart/` |
| `L4_spi` | `bringup/L4_spi/` |
| `L5_usb` | `usb/_legacy_native/L5_usb/` |
| `L6_cdc` | `usb/_legacy_native/L6_cdc/` |
| `L7_cherryusb_cdc` | `usb/L6_cherryusb_cdc_echo/` (legacy CLI name) |

## Backward compatibility

`libraries/AP_HAL_RTT/hwdef/common/tests/` keeps **README + symlinks** into this tree so older docs and `copytree` deploy layouts still resolve the same files.

## USB policy

- **Production gate:** CherryUSB (`L7_cherryusb_cdc` / `L6_cherryusb_cdc_echo`)
- **Legacy native:** `L5_usb`, `L6_cdc` under `_legacy_native/` — debugging only

See `libraries/AP_HAL_RTT/docs/usb-stack-boundary.md`.
