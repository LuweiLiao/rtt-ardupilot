# hwdef.dat Audit: RTT cuav_v5 vs ChibiOS fmuv5

**Date:** 2026-04-19  
**Files compared:**
- ChibiOS: `libraries/AP_HAL_ChibiOS/hwdef/fmuv5/hwdef.dat`
- RTT: `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat`

## Summary

| Category | Count | Description |
|----------|-------|-------------|
| **MUST FIX** | 4 | Missing pins/defines that cause functional problems |
| **OK TO DIFFER** | 20 | Intentional RTT-specific differences |
| **NICE TO HAVE** | 3 | Minor differences, no functional impact |
| **NOT APPLICABLE** | 5 | ChibiOS-specific, cannot apply to RTT |

---

## MUST FIX — Will cause functional problems

### 1. Missing `PH2 GPIO_CAN1_SILENT`
- **ChibiOS:** `PH2 GPIO_CAN1_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(70)`
- **RTT:** Absent
- **Impact:** CAN1 silent mode not controllable. CAN1 transceiver stays in normal mode, may cause bus contention.
- **Fix:** Add `PH2  GPIO_CAN1_SILENT  OUTPUT  PUSHPULL  SPEED_LOW  LOW  GPIO(70)` to RTT hwdef

### 2. Missing `IOMCU_UART UART8`
- **ChibiOS:** `IOMCU_UART UART8`
- **RTT:** Commented out (`# IOMCU_UART UART8`)
- **Impact:** IOMCU communication not initialized. RC input via SBUS on IOMCU won't work.
- **Note:** RTT may have IOMCU support disabled intentionally — verify intent.

### 3. Missing `PE5 TIM9_CH1` alarm pin
- **ChibiOS:** `PE5 TIM9_CH1 TIM9 GPIO(77) ALARM`
- **RTT:** Absent
- **Impact:** Missing alarm output on PE5. Used for external device timing/notification.

### 4. Missing SWD debug pins
- **ChibiOS:** `PA13 JTMS-SWDIO SWD`, `PA14 JTCK-SWCLK SWD`
- **RTT:** Absent
- **Impact:** SWD debug interface not configured. Low priority for production but needed for development/debugging.
- **Note:** May be intentionally omitted if RTT configures SWD elsewhere (CubeMX).

### 5. `COMPASS` lines missing from RTT
- **ChibiOS:** Two COMPASS lines for IST8310 (internal + external)
- **RTT:** Only `HAL_MAG_PROBE_LIST` define, no `COMPASS` lines
- **Impact:** Compass auto-detection may not work correctly. The `HAL_MAG_PROBE_LIST` define may serve the same purpose — verify.

---

## OK TO DIFFER — Intentional RTT-specific

| Item | ChibiOS | RTT | Reason |
|------|---------|-----|--------|
| `CONFIG_HAL_BOARD_SUBTYPE` | `HAL_BOARD_SUBTYPE_CHIBIOS_FMUV5` | (absent) | RTT has its own board subtype system |
| `HAL_CHIBIOS_ARCH_FMUV5` | 1 | (absent) | ChibiOS-specific arch define |
| `HAL_OS_FATFS_IO` | 1 | 0 | RTT uses POSIX FS, not FatFS |
| `HAL_PROBE_EXTERNAL_I2C_COMPASSES` | defined | commented out | RTT uses `HAL_MAG_PROBE_LIST` instead |
| `AP_FILESYSTEM_POSIX_ENABLED` | (absent) | 1 | RTT uses POSIX filesystem |
| `AP_FILESYSTEM_POSIX_HAVE_STATFS` | (absent) | 1 | RTT POSIX FS feature |
| `AP_FILESYSTEM_POSIX_HAVE_UTIME` | (absent) | 0 | RTT POSIX FS — utime not supported |
| `HAL_BOARD_LOG_DIRECTORY` | (absent) | "/logs" | RTT-specific log path |
| `HAL_BOARD_STORAGE_DIRECTORY` | (absent) | "/APM/STORAGE" | RTT-specific storage path |
| `HAL_BOARD_TERRAIN_DIRECTORY` | (absent) | "/APM/TERRAIN" | RTT-specific terrain path |
| `HAL_LOGGING_FILESYSTEM_ENABLED` | (absent) | 1 | RTT enables FS logging explicitly |
| `HAL_LOGGING_MAVLINK_ENABLED` | (absent) | 1 | RTT enables MAVLink logging explicitly |
| `SCRIPTING_DIRECTORY` | (absent) | "/APM/scripts_rtt" | RTT-specific script path |
| `STM32F767xx` | (absent) | 1 | RTT HAL driver define |
| `USE_HAL_DRIVER` | (absent) | 1 | STM32 HAL driver (CubeMX) |
| `STORAGE_FLASH_PAGE` | (absent) | 10 | RTT flash storage page |
| `AP_FEATURE_BOARD_DETECT` | (absent) | 1 | RTT board detection |
| `BOARD_TYPE_DEFAULT` | (absent) | 24 | RTT board type ID |
| `DEFAULT_SERIAL0_BAUD` | (absent) | 921600 | RTT default baud rate |
| `HAL_BARO_ALLOW_INIT_NO_BARO` | (absent) | 1 | Allow boot without baro (dev convenience) |
| `AP_SCRIPTING_ENABLED` | (absent) | 0 | Scripting disabled in RTT |
| `HAL_WITH_EKF_DOUBLE` | (absent) | 0 | Single-precision EKF (RAM constraint) |

### Pin AF (Alternate Function) Annotations
- **ChibiOS** omits AF numbers (uses implicit mapping)
- **RTT** includes explicit AF numbers (e.g., `AF5`, `AF7`, `AF8`, `AF10`, `AF4`)
- This is **expected** — RTT HAL uses explicit AF configuration via STM32 HAL

### SPI1_MOSI pin location
- **ChibiOS:** `PD7 SPI1_MOSI SPI1`
- **RTT:** `PB5 SPI1_MOSI SPI1 AF5`
- **Impact:** Different physical pin for SPI1 MOSI. Verify against CUAV V5 schematic.

### OTG USB pin naming
- **ChibiOS:** `PA11 OTG_FS_DM OTG1`, `PA12 OTG_FS_DP OTG1`
- **RTT:** `PA11 OTG1_DM OTG1 AF10`, `PA12 OTG1_DP OTG1 AF10`, plus `PA8 OTG1_SOF`, `PA9 OTG1_VBUS`, `PA10 OTG1_ID`
- **Impact:** RTT has more complete USB pin configuration. OK to differ.

### GPIO_CAN2_SILENT pin location
- **ChibiOS:** `PH3 GPIO_CAN2_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(71)`
- **RTT:** `PI8 GPIO_CAN2_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(71)`
- **Impact:** Different physical pin for CAN2 silent control. Verify against schematic.

### SPI CS SPEED_VERYLOW
- **ChibiOS:** `PF2 ICM20689_CS CS SPEED_VERYLOW`, `PF3 ICM20602_CS CS SPEED_VERYLOW`, `PF5 FRAM_CS CS SPEED_VERYLOW`
- **RTT:** Same CS pins but without `SPEED_VERYLOW`
- **Impact:** May cause SPI issues with sensitive IMUs. Consider adding speed constraint.

### TIM14 alarm (RTT has, ChibiOS doesn't)
- **RTT:** `PF9 TIM14_CH1 TIM14 GPIO(77) ALARM`
- **ChibiOS:** Absent (uses TIM9 on PE5 instead)
- **OK:** Different timer peripheral for alarm functionality.

### PWM pins (RTT-only)
- `PE14 TIM1_CH4 TIM1 PWM(1) GPIO(50)`
- `PA10 TIM1_CH3 TIM1 PWM(2) GPIO(51)`
- These are additional PWM outputs not in ChibiOS fmuv5. OK if intentionally added.

### FRAM_CS vs RAMTRON_CS naming
- **ChibiOS:** `PF5 FRAM_CS CS`
- **RTT:** `PF5 RAMTRON_CS CS`
- Same pin, different label. RTT uses `RAMTRON_CS` in SPIDEV too. Consistent within RTT.

---

## NICE TO HAVE — Minor differences

### 1. `FLASH_RESERVE_END_KB 512` only in RTT
- ChibiOS has `FLASH_RESERVE_START_KB 32` only
- RTT adds `FLASH_RESERVE_END_KB 512`
- **Impact:** Reserves flash tail. Low priority.

### 2. `HAL_COMPASS_AUTO_ROT_DEFAULT 2` present in both — OK, no difference
- Both files define this. No action needed.

### 3. Whitespace/formatting differences
- RTT uses more whitespace/alignment in pin definitions
- Purely cosmetic, no functional impact

---

## NOT APPLICABLE — ChibiOS-specific

| Item | Description |
|------|-------------|
| `CONFIG_HAL_BOARD_SUBTYPE` | ChibiOS board subtype enum |
| `HAL_CHIBIOS_ARCH_FMUV5` | ChibiOS arch identifier |
| `HAL_OS_FATFS_IO 1` | ChibiOS FatFS integration |
| `NODMA` UART tags | ChibiOS DMA control tag |
| Implicit AF mapping | ChibiOS resolves AF internally |

---

## Priority Action Items

1. **🔴 Add `PH2 GPIO_CAN1_SILENT`** — CAN1 silent mode broken
2. **🔴 Verify `IOMCU_UART UART8`** — RC input depends on this
3. **🟡 Add `PE5 TIM9_CH1` alarm** — if alarm output needed
4. **🟡 Verify SWD pins** — debug capability
5. **🟡 Verify SPI1_MOSI pin (PD7 vs PB5)** against schematic
6. **🟡 Verify GPIO_CAN2_SILENT pin (PH3 vs PI8)** against schematic
7. **🟢 Consider adding `SPEED_VERYLOW` to IMU/FRAM CS pins**
8. **🟢 Verify `COMPASS` lines vs `HAL_MAG_PROBE_LIST`** equivalence
