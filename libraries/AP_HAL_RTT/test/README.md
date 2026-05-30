# AP_HAL_RTT layered module tests

Canonical sources for `scons --target=cuav_v5 --test=<name>` module-test firmware.

Resolution is implemented in `Tools/scripts/rtt_test_manifest.py` and wired from `hwdef/common/SConscript` (deployed BSP copies that file on `rtt_bsp_deploy`).

**Policy:** Only names listed in `TEST_LAYOUT` **and** with an existing `SConscript` are build-safe. Proposed names below are **documentation only** until a follow-up task adds directories and manifest entries.

---

## Naming prefixes (six-layer model)

| Prefix | Layer | Directory | Registered in manifest? |
|--------|-------|-----------|-------------------------|
| `H_*` | Host / static | `libraries/AP_HAL_RTT/tests/` (future) or Python CI | No — not board firmware |
| `L*` / `l0_boot` | STM32 internal | `bringup/`, `usb/` | Yes — see table below |
| `D_*` | HAL abstract | `drivers/D_*/` | Partial — see registered table |
| `E_*` | External module | `drivers/E_*/` | Partial — see registered table |
| `S_*` | Subsystem smoke | `subsystem/S_*/` | Partial — see registered table |
| (none) | Full vehicle | ArduCopter build | N/A |

See `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` and `.cursor/project/driver-validation-matrix.md` for per-driver hardware requirements.

---

## Layout

```text
test/
  _common/              test_runner, stubs, app_descriptor
  bringup/
    l0_boot/            minimal boot (legacy name)
    L0_system/          SysTick, FPU, fault regs
    L1_iwdg/
    L2_gpio/
    L3_uart/             STM32 UART registers (not UARTDriver)
    L4_spi/             SPI1 polled + board IMU WHO_AM_I
    L5_i2c/             (proposed) I2C register / probe
    L6_sdmmc/           (proposed) SDMMC init
  usb/
    L6_cherryusb_cdc_echo/   CherryUSB CDC echo (production USB gate)
    L7_hal_shim/             README only — full-build shim validation
    _legacy_native/
      L5_usb/                register-level USB init (backend=none)
      L6_cdc/                native DWC2 CDC PoC (not production)
  drivers/
    README.md
    D_uart_hal/         HAL smoke — UARTDriver via hal.serial() (registered)
    D_spi_hal/          HAL smoke — SPIDevice WHO_AM_I (registered)
    D_i2c_hal/          HAL smoke — I2CDevice IST8310 WAI (registered)
    D_storage/          HAL smoke — Storage read/write scratch (registered)
    D_scheduler/        HAL smoke — Scheduler timer proc + delay (registered)
    D_analogin/         HAL smoke — AnalogIn init/channel/timer_tick (registered)
    D_usb_serial/       HAL serial(0) CDC smoke (CherryUSB + UARTDriver; UART7 PASS)
    D_rcoutput/         HAL smoke — RCOutput CH0 safe PWM (registered)
    D_rcinput/          HAL smoke — RCInput init/read poll (registered)
    E_sdcard/           SD/FS smoke (registered; card for HW)
    E_wspi_flash/       BUILD_ONLY — N/A on cuav_v5 (registered)
    E_imu/              ICM20689 chip WHO_AM_I (registered; not full INS)
    E_ms5611/           MS5611 PROM/CRC (registered; needs SPIDEV in hwdef)
    E_ist8310/          IST8310 WAI + single meas + raw XYZ (registered)
    E_fram/             Ramtron RDID/RW (registered; needs SPIDEV in hwdef)
    E_sbus/             (proposed) SBUS frames
  subsystem/
    README.md
    S_param_storage/    storage-backed param path (NOT full AP_Param)
    S_sensors/          IMU + MS5611 chip combo (E_* boundaries)
    S_mavlink_usb/      HAL serial(0) CDC + MAVLink HEARTBEAT (CherryUSB; host pymavlink)
    S_compass/          AP_Compass init + field read (registered)
```

---

## scons names — **registered** (`--test=`)

| `--test=` | Directory | Build notes (matrix 2026-05-28) |
|-----------|-----------|-----------------------------------|
| `l0_boot` | `bringup/l0_boot/` | 待构建 |
| `L0_system` | `bringup/L0_system/` | 已构建（≠ 上板通过） |
| `L1_iwdg` | `bringup/L1_iwdg/` | 待构建 |
| `L2_gpio` | `bringup/L2_gpio/` | 待构建 |
| `L3_uart` | `bringup/L3_uart/` | 待构建 |
| `L4_spi` | `bringup/L4_spi/` | 已构建 |
| `L5_usb` | `usb/_legacy_native/L5_usb/` | legacy |
| `L6_cdc` | `usb/_legacy_native/L6_cdc/` | legacy |
| `L7_cherryusb_cdc` | `usb/L6_cherryusb_cdc_echo/` | 已构建；legacy CLI alias |
| `L6_cherryusb_cdc_echo` | `usb/L6_cherryusb_cdc_echo/` | alias of above |
| `D_uart_hal` | `drivers/D_uart_hal/` | **已构建 / 上板 PASS** — HAL smoke (`main.cpp`, serial6 TX); RX/loopback N/A |
| `D_spi_hal` | `drivers/D_spi_hal/` | **已构建 / 上板 PASS** — HAL smoke (`main.cpp`, ICM20689 WHO_AM_I); MS5611/FRAM not in D-layer |
| `D_i2c_hal` | `drivers/D_i2c_hal/` | **已构建 / 上板 PASS** — HAL smoke (`main.cpp`, IST8310 WAI) |
| `D_storage` | `drivers/D_storage/` | **已构建 / 上板 PASS** — HAL smoke (tail 8B scratch RW+restore); RAM stub on cuav_v5 |
| `D_scheduler` | `drivers/D_scheduler/` | **已构建 / 上板 PASS** — HAL smoke (`scheduler` init + timer proc + delay); callback count>0 |
| `D_analogin` | `drivers/D_analogin/` | **已构建 / 上板 PASS** — HAL smoke (`analogin` init/ch6 + `_timer_tick`; raw/mV diagnostics); not full ADC calibration |
| `D_usb_serial` | `drivers/D_usb_serial/` | **已构建 / 上板 PASS** — HAL `serial(0)` CherryUSB CDC (`rtt_test_hal_usb_serial_link.py`); ACM `1209:5741` @921600 beacon+echo；CFSR/HFSR=0 |
| `D_rcoutput` | `drivers/D_rcoutput/` | **已构建** — HAL smoke (CH0 1000–1200 µs; read/read_last_sent); PWM 波形未验证 |
| `D_rcinput` | `drivers/D_rcinput/` | **已构建** — HAL smoke (`rcin` API; no channels→FAIL); SBUS/PPM for pass; 上板未验证 |
| `E_sdcard` | `drivers/E_sdcard/` | **已构建 / 上板 PASS** — SD/FS smoke (POSIX on `/APM`); needs microSD |
| `E_wspi_flash` | `drivers/E_wspi_flash/` | **已构建** — N/A on cuav_v5 at runtime |
| `E_imu` | `drivers/E_imu/` | **已构建 / 上板 PASS** — chip ICM20689 WHO_AM_I (+ PWR_MGMT_1); not AP_InertialSensor |
| `E_ms5611` | `drivers/E_ms5611/` | **已构建 / 上板 PASS** — MS5611 PROM/CRC; requires `SPIDEV ms5611` |
| `E_fram` | `drivers/E_fram/` | **已构建 / 上板 PASS** — FM25V02A RDID + RDSR + 4B RW; requires SPI2 CMSIS path |
| `E_ist8310` | `drivers/E_ist8310/` | **已构建 / 上板 PASS** — IST8310 WAI 0x10 + raw XYZ; not AP_Compass |
| `S_param_storage` | `subsystem/S_param_storage/` | **已构建 / 上板 PASS** — storage tail-16B scratch; **NOT** full AP_Param |
| `S_sensors` | `subsystem/S_sensors/` | **已构建 / 上板 PASS** — ICM20689 + MS5611 thin combo; no INS/Baro full stack |
| `S_mavlink_usb` | `subsystem/S_mavlink_usb/` | **已构建 / 上板 PASS** — HAL `serial(0)` + `mavlink_msg_heartbeat_pack` @921600；主机 `tests/rtt_test_S_mavlink_usb_host.py` HEARTBEAT msgid=0；CFSR/HFSR=0；非整机 L0/参数 |
| `S_compass` | `subsystem/S_compass/` | **已构建 / 上板 PASS** — `Compass::init`+field；IST8310 probe；非 vehicle cal |

---

## scons names — **proposed** (do not pass to scons until implemented)

Add `SConscript` + `TEST_LAYOUT` entry in the same change set.

### HAL abstract (`D_*`) — not yet registered

Registered HAL-smoke entries: `D_uart_hal`, `D_spi_hal`, `D_i2c_hal`, `D_storage`, `D_scheduler`, `D_analogin`, `D_usb_serial`, `D_rcoutput`, `D_rcinput` — see table above. BUILD_ONLY: `E_wspi_flash` (cuav_v5 N/A).

### External module (`E_*`) — not yet registered

| Proposed `--test=` | Purpose |
|--------------------|---------|
| `E_sbus` | SBUS decode (**receiver required**) |

Registered: `E_sdcard`, `E_wspi_flash`, `E_imu`, `E_ms5611`, `E_fram`, `E_ist8310` (see registered table).

### Subsystem (`S_*`)

Registered: `S_param_storage`, `S_sensors`, `S_mavlink_usb`, `S_compass` — see table above. `S_rc_chain` **excluded** (RC deferred).

### Bring-up extensions (`L*`)

| Proposed `--test=` | Purpose |
|--------------------|---------|
| `L5_i2c` | I2C peripheral register / bus scan |
| `L6_sdmmc` | SDMMC controller + card detect (**card required**) |

---

## Backward compatibility

`libraries/AP_HAL_RTT/hwdef/common/tests/` keeps **README + symlinks** into this tree so older docs and `copytree` deploy layouts still resolve the same files.

---

## USB policy

- **Production gate:** CherryUSB (`L7_cherryusb_cdc` / `L6_cherryusb_cdc_echo`)
- **Legacy native:** `L5_usb`, `L6_cdc` under `_legacy_native/` — debugging only

See `libraries/AP_HAL_RTT/docs/usb-stack-boundary.md`.
