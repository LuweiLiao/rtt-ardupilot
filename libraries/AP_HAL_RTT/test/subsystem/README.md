# Subsystem smoke (`S_*`)

Multi-driver / param / comm chains between HAL smoke (`D_*` / `E_*`) and full ArduCopter.

**Policy:** Only names in `Tools/scripts/rtt_test_manifest.py` `TEST_LAYOUT` with an existing `SConscript` are build-safe.

---

## Registered (`--test=`)

| Name | Directory | Runtime | Notes |
|------|-----------|---------|-------|
| `S_param_storage` | `S_param_storage/` | **Storage HAL** | Tail-16B scratch R/W+restore; **NOT** full `AP_Param` (no vehicle `var_info`). HAL gate: `D_storage`. |
| `S_sensors` | `S_sensors/` | **IMU + Baro chips** | Thin combo of `E_imu` + `E_ms5611` boundaries; **no** `AP_InertialSensor` / `AP_Baro`. MS5611 **FAIL** if hwdef lacks `SPIDEV ms5611`. |
| `S_mavlink_usb` | `S_mavlink_usb/` | **Runtime** | CherryUSB + `hal.serial(0)` @921600; 1 Hz MAVLink HEARTBEAT (`ardupilotmega` headers); host `tests/rtt_test_S_mavlink_usb_host.py`. **Not** full GCS/param/L0. |
| `S_compass` | `S_compass/` | **AP_Compass** | **已构建 / 上板 PASS**（2026-05-29）：`Compass::init` + `read`/`get_field`; scheduler + IST8310 periodic; `COMPASS_MOT=0` / `AP_CUSTOMROTATIONS=0`; `HAL_COMPASS_ALLOW_INIT_NO_MAG` allows count=0 PASS; **not** full vehicle cal/GCS. Chip gate: `E_ist8310`. |

Build (serial — do not parallel `--test=` with other module tests):

```bash
scons --target=cuav_v5 --test=S_param_storage -j$(nproc)
scons --target=cuav_v5 --test=S_sensors -j$(nproc)
scons --target=cuav_v5 --test=S_mavlink_usb -j$(nproc)
scons --target=cuav_v5 --test=S_compass -j$(nproc)
```

---

## Proposed (not in manifest)

| Name | Purpose | Blocker |
|------|---------|---------|
| `S_rc_chain` | RCIn → RCOut | **Excluded** per project policy (RC deferred) |

---

## Layering

```text
L* / D* / E*  →  single HAL API or single chip
S*            →  minimal chain (storage+param path, IMU+baro, mavlink usb, compass)
Full vehicle  →  ArduCopter + L0 pymavlink / OpenOCD
```

See `docs/AP_HAL_RTT_DRIVER_VALIDATION.md` and `.cursor/project/driver-validation-matrix.md`.
