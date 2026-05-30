# Legacy path — module tests moved

**Canonical tree:** `libraries/AP_HAL_RTT/test/`

This directory keeps **symlinks** so older references, deploy `copytree`, and scripts that mention `hwdef/common/tests/test_L*` still work.

| Legacy symlink | Canonical target |
|----------------|------------------|
| `common/` | `../../../../test/_common` |
| `test_l0_boot/` | `../../../../test/bringup/l0_boot` |
| `test_L0_system/` | `../../../../test/bringup/L0_system` |
| `test_L1_iwdg/` | `../../../../test/bringup/L1_iwdg` |
| `test_L2_gpio/` | `../../../../test/bringup/L2_gpio` |
| `test_L3_uart/` | `../../../../test/bringup/L3_uart` |
| `test_L4_spi/` | `../../../../test/bringup/L4_spi` |
| `test_L5_usb/` | `../../../../test/usb/_legacy_native/L5_usb` |
| `test_L6_cdc/` | `../../../../test/usb/_legacy_native/L6_cdc` |
| `test_L7_cherryusb_cdc/` | `../../../../test/usb/L6_cherryusb_cdc_echo` |

Build discovery uses `rtt_test_manifest.resolve_test_paths()` — it does **not** require the `test_` prefix in the filesystem.
