# Driver-level module tests (`D_*` / `E_*`)

HAL abstract and external-module tests live here. **STM32 register bring-up** stays in `../bringup/` (`L*`); **multi-driver chains** go in `../subsystem/` (`S_*`).

## Status (2026-05-28)

| `--test=` | Directory | Runtime | Build gate | Hardware |
|-----------|-----------|---------|------------|----------|
| `D_uart_hal` | `D_uart_hal/` | **HAL smoke** (`hal.serial` begin/printf/write) | **已构建** | UART7 console; optional SERIAL0; **no** RX/loopback |
| `D_spi_hal` | `D_spi_hal/` | **HAL smoke** (`get_device` + WHO_AM_I 0x75/0x98) | **已构建** | On-board ICM20689; **no** MS5611 in cuav_v5 hwdef table |
| `D_i2c_hal` | `D_i2c_hal/` | **HAL smoke** (`i2c_mgr->get_device` + IST8310 WAI 0x00→0x10) | **已构建** | I2C3 bus 0 @ 0x0E; **on-board not verified** |
| `D_storage` | `D_storage/` | **HAL smoke** (`hal.storage` init + tail-8B RW + restore) | **已构建** | Scratch at `HAL_STORAGE_SIZE-8`; **RAM stub** on cuav_v5; not SD/FRAM |
| `D_scheduler` | `D_scheduler/` | **HAL smoke** (`scheduler` init + timer proc + delay) | **已构建 / 已上板通过** | UART7 callback count>0; CFSR/HFSR=0 |
| `D_analogin` | `D_analogin/` | **HAL smoke** (`analogin` init/ch6 + `_timer_tick`; raw/mV diagnostics) | **已构建 / 已上板通过** | ch6 SCALED_V3V3; `raw_counts=2064`, `voltage_latest=3326 mV`; **not** full ADC calibration |
| `D_usb_serial` | `D_usb_serial/` | **HAL smoke** (CherryUSB shim + `hal.serial(0)` TX; 3s beacon + optional echo loop) | **已构建 / 上板 CDC TX PASS** | `test_stubs_no_usb.c` + `rtt_test_hal_usb_serial_link.py`；ACM `1209:5740` @ 921600 可见 `CDC beacon`；CFSR/HFSR=0；L7 未改 |
| `D_rcoutput` | `D_rcoutput/` | **HAL smoke** (`rcout` init/enable_ch/set_freq/write/read/read_last_sent/cork/push) | **已构建** | CH0 only 1000–1200 µs; **no scope/ESC/props** |
| `D_rcinput` | `D_rcinput/` | **HAL smoke** (`rcin` init/new_input/num_channels/read; 3s poll) | **已构建** | **SBUS/PPM required**; no signal→**TEST_FAIL**; `AP_RCPROTOCOL=0` in image |
| `E_sdcard` | `E_sdcard/` | **SD/FS smoke** (mount `/APM`, POSIX RW on `.rtt_e_sdcard_smoke`) | **已构建** | **microSD required**; no card → **TEST_FAIL** |
| `E_wspi_flash` | `E_wspi_flash/` | N/A on cuav_v5 (documented) | **已构建** | QUADSPI boards only |
| `E_imu` | `E_imu/` | **Chip smoke** (ICM20689 WHO_AM_I + PWR_MGMT_1; **not** AP_InertialSensor) | **已构建 / 已上板通过** | WHO_AM_I=0x98; CFSR/HFSR=0 |
| `E_ms5611` | `E_ms5611/` | **PROM/CRC** on SPI4 `ms5611` | **已构建 / 已上板通过** | `SPIDEV ms5611` restored; PROM/CRC PASS; CFSR/HFSR=0 |
| `E_fram` | `E_fram/` | **RDID (9B) + RDSR + 4B RW** on SPI2 `ramtron`; FM25V02A id 0x22/0x08 | **已构建** | **已上板通过**（2026-05-29；需 SPI2 CMSIS 轮询路径） |
| `E_ist8310` | `E_ist8310/` | **Chip smoke** (IST8310 WAI + CNTL1 single meas + raw XYZ; **not** AP_Compass) | **已构建 / 上板 PASS**（2026-05-29） | 板载 I2C3 @0x0E；无芯片→**TEST_FAIL** |

**Do not** treat `TEST_PASS` without on-board hardware as full driver verification. `D_uart_hal` / `D_spi_hal` call real `AP_HAL` APIs; WHO_AM_I / UART TX still need flash + hardware to confirm.

Matrix aliases (`D_uart`, `D_spi`, …) in `.cursor/project/driver-validation-matrix.md` map to these trees; CLI names use the `_hal` suffix where noted above.

## Naming

| Prefix | Example dirs | Layer |
|--------|--------------|-------|
| `D_*` | `D_uart_hal/`, `D_spi_hal/`, … | AP_HAL driver API |
| `E_*` | `E_sdcard/`, `E_wspi_flash/`, … | Specific chips / media |

Full list and L*/S* names: `../README.md`.

## Implementation checklist (per test)

1. Copy pattern from `../bringup/L0_system/` (`test_runner`, `main.c`, `SConscript`).
2. Reuse `AP_HAL/examples/*` call flow when linking HAL (often `.cpp` + wider `DefineGroup`).
3. Document required hardware in `main.c` header and matrix row.
4. Register in `Tools/scripts/rtt_test_manifest.py` only after `SConscript` exists.
5. Run `scons --target=cuav_v5 --test=<name>` — set matrix to **已构建** before any **已上板通过** claim.

## Next phase (HAL smoke)

BUILD_ONLY placeholders must be replaced with real `AP_HAL` calls per example semantics. **Execution plan** (batches A–D, success criteria, hardware blockers): `.cursor/project/driver-validation-hal-smoke-plan.md`.

## Build examples

```bash
scons --target=cuav_v5 --test=D_uart_hal -j$(nproc)
scons --target=cuav_v5 --test=D_spi_hal -j$(nproc)
scons --target=cuav_v5 --test=D_i2c_hal -j$(nproc)
scons --target=cuav_v5 --test=D_storage -j$(nproc)
scons --target=cuav_v5 --test=D_scheduler -j$(nproc)
scons --target=cuav_v5 --test=D_analogin -j$(nproc)
scons --target=cuav_v5 --test=D_usb_serial -j$(nproc)
scons --target=cuav_v5 --test=D_rcoutput -j$(nproc)
scons --target=cuav_v5 --test=D_rcinput -j$(nproc)
scons --target=cuav_v5 --test=E_sdcard -j$(nproc)
scons --target=cuav_v5 --test=E_wspi_flash -j$(nproc)
scons --target=cuav_v5 --test=E_imu -j$(nproc)
scons --target=cuav_v5 --test=E_ms5611 -j$(nproc)
scons --target=cuav_v5 --test=E_fram -j$(nproc)
scons --target=cuav_v5 --test=E_ist8310 -j$(nproc)
```
