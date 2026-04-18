# hwdef Diff Report: ChibiOS fmuv5/CUAVv5 vs RTT cuav_v5

**Date:** 2026-04-18
**Source files:**
- ChibiOS base: `libraries/AP_HAL_ChibiOS/hwdef/fmuv5/hwdef.dat`
- ChibiOS CUAV: `libraries/AP_HAL_ChibiOS/hwdef/CUAVv5/hwdef.dat` (includes fmuv5)
- RTT: `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat`

**Note on syntax:** ChibiOS hwdef uses `"PA5 SPI1_SCK SPI1"` (no AF number; resolved by ChibiOS driver).
RTT hwdef uses `"PA5 SPI1_SCK SPI1 AF5"` (explicit AF number for STM32 HAL init).

---

## a) SPI Bus Pins

| SPI Bus | Signal | ChibiOS Pin | RTT Pin | Status |
|---------|--------|-------------|---------|--------|
| SPI1 | SCK | PG11 | ❌ **Missing** | 🔴 CRITICAL |
| SPI1 | MISO | PA6 | ❌ **Missing** | 🔴 CRITICAL |
| SPI1 | MOSI | PD7 | ❌ **Missing** | 🔴 CRITICAL |
| SPI2 | SCK | PI1 | ❌ **Missing** | 🔴 CRITICAL (FRAM broken) |
| SPI2 | MISO | PI2 | ❌ **Missing** | 🔴 CRITICAL (FRAM broken) |
| SPI2 | MOSI | PI3 | ❌ **Missing** | 🔴 CRITICAL (FRAM broken) |
| SPI4 | SCK | PE2 | ❌ **Missing** | 🔴 CRITICAL (MS5611 baro broken) |
| SPI4 | MISO | PE13 | ❌ **Missing** | 🔴 CRITICAL (MS5611 baro broken) |
| SPI4 | MOSI | PE6 | ❌ **Missing** | 🔴 CRITICAL (MS5611 baro broken) |

**Impact:** No SPI bus pin definitions means `rtt_hwdef.py` won't generate HAL MSP init for any SPI bus. SPI1/2/4 all non-functional.

**Patch needed (RTT syntax with AF):**
```
# SPI1 - internal sensors (AF5)
PG11 SPI1_SCK SPI1 AF5
PA6  SPI1_MISO SPI1 AF5
PD7  SPI1_MOSI SPI1 AF5

# SPI2 - FRAM (AF5)
PI1  SPI2_SCK SPI2 AF5
PI2  SPI2_MISO SPI2 AF5
PI3  SPI2_MOSI SPI2 AF5

# SPI4 - sensors2 (AF5)
PE2  SPI4_SCK SPI4 AF5
PE13 SPI4_MISO SPI4 AF5
PE6  SPI4_MOSI SPI4 AF5
```

---

## b) ADC Channels

| Channel | ChibiOS | RTT | Status |
|---------|---------|-----|--------|
| PA0 BATT_VOLTAGE_SENS | ✅ ADC1 SCALE(1) | ✅ ADC1 SCALE(1) | ✅ Match |
| PA1 BATT_CURRENT_SENS | ✅ ADC1 SCALE(1) | ✅ ADC1 SCALE(1) | ✅ Match |
| PA2 BATT2_VOLTAGE_SENS | ✅ ADC1 SCALE(1) | ✅ ADC1 SCALE(1) | ✅ Match |
| PA3 BATT2_CURRENT_SENS | ✅ ADC1 SCALE(1) | ✅ ADC1 SCALE(1) | ✅ Match |
| PB0 RSSI_IN | ✅ ADC1 SCALE(1) | ✅ ADC1 SCALE(1) | ✅ Match |
| PC0 VDD_5V_SENS | ✅ ADC1 SCALE(2) | ✅ ADC1 SCALE(2) | ✅ Match |
| PC1 SCALED_V3V3 | ✅ ADC1 SCALE(2) | ✅ ADC1 SCALE(2) | ✅ Match |
| PC4 SPARE1_ADC1 | ✅ ADC1 SCALE(1) | ✅ ADC1 SCALE(1) | ✅ Match |
| PA4 SPARE2_ADC1 | ✅ ADC1 SCALE(1) | ❌ **Missing** | 🟢 NICE_TO_HAVE |

ADC channels match well. Only SPARE2 missing.

---

## c) Missing/Extra Defines

| Define | ChibiOS | RTT | Status |
|--------|---------|-----|--------|
| `CONFIG_HAL_BOARD_SUBTYPE` | `HAL_BOARD_SUBTYPE_CHIBIOS_FMUV5` | ❌ Missing | 🟡 IMPORTANT |
| `HAL_CHIBIOS_ARCH_FMUV5` | `1` | N/A (RTT-specific) | — |
| `HAL_DEFAULT_INS_FAST_SAMPLE` | `1` | ❌ Missing | 🟡 IMPORTANT (IMU performance) |
| `HAL_HAVE_SAFETY_SWITCH` | `1` | ❌ Missing | 🟡 IMPORTANT |
| `HAL_WITH_RAMTRON` | `1` | ✅ `1` | ✅ Match |
| `HAL_STORAGE_SIZE` | `32768` | `16384` | 🟡 DIFFERENT (RTT is half) |
| `HAL_OS_FATFS_IO` | `1` | ❌ Missing (RTT uses POSIX FS) | — (different arch) |
| `HAL_WITH_IO_MCU_DSHOT` | `1` | ❌ Missing | 🟢 NICE_TO_HAVE |
| `HAL_GPIO_SPEKTRUM_PWR` | `73` | ❌ Missing | 🟢 NICE_TO_HAVE |
| `HAL_SPEKTRUM_PWR_ENABLED` | `1` | ❌ Missing | 🟢 NICE_TO_HAVE |
| `HAL_HEATER_GPIO_PIN` | `80` | ❌ Missing (pin defined as GPIO) | 🟢 NICE_TO_HAVE |
| `HAL_GPIO_A_LED_PIN` | `90` | ❌ Missing | 🟡 IMPORTANT (status LED) |
| `HAL_GPIO_B_LED_PIN` | `92` | ❌ Missing | 🟡 IMPORTANT (status LED) |
| `AP_NOTIFY_GPIO_LED_2_ENABLED` | `1` | ❌ Missing | 🟢 NICE_TO_HAVE |
| `HAL_COMPASS_AUTO_ROT_DEFAULT` | `2` | ✅ `2` | ✅ Match |
| `HAL_BATT_VOLT_SCALE` | `18.0` | ✅ `18.0` | ✅ Match |
| `HAL_BATT_CURR_SCALE` | `24.0` | ✅ `24.0` | ✅ Match |
| `HAL_BATT_VOLT_PIN` | `0` | ✅ `0` | ✅ Match |
| `HAL_BATT_CURR_PIN` | `1` | ✅ `1` | ✅ Match |
| `HAL_BATT2_VOLT_PIN` | `2` | ✅ `2` | ✅ Match |
| `HAL_BATT2_CURR_PIN` | `3` | ✅ `3` | ✅ Match |
| `HAL_WITH_EKF_DOUBLE` | — (default 1) | `0` | 🟢 Intentional (RTT memory constraint) |
| `DMA_PRIORITY` | `SDMMC* UART8* ADC* SPI* TIM*` | N/A | — (ChibiOS-specific) |
| `ROMFS io_firmware_dshot.bin` | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE |

---

## d) SPIDEV Entries

| Device | ChibiOS | RTT | Status |
|--------|---------|-----|--------|
| ms5611 | SPI4 DEVID1 MS5611_CS MODE3 20MHz | ✅ Match | ✅ |
| icm20689 | SPI1 DEVID1 ICM20689_CS MODE3 2/8MHz | ✅ Match | ✅ |
| icm20602 | SPI1 DEVID2 ICM20602_CS MODE3 2/8MHz | ✅ Match | ✅ |
| icm42688 | SPI1 DEVID2 ICM42688_CS MODE3 2/8MHz | ✅ Match (CUAVv5) | ✅ |
| bmi055_g | SPI1 DEVID3 BMI055_G_CS MODE3 10MHz | SPI1 DEVID4 | 🟡 DEVID mismatch |
| bmi055_a | SPI1 DEVID4 BMI055_A_CS MODE3 10MHz | SPI1 DEVID5 | 🟡 DEVID mismatch |
| ramtron | SPI2 DEVID1 FRAM_CS MODE3 8MHz | ✅ Match (RAMTRON_CS alias) | ✅ |

**Note:** In ChibiOS fmuv5, bmi055_g=DEVID3, bmi055_a=DEVID4. In RTT, bmi055_g=DEVID4, bmi055_a=DEVID5. The DEVID number is just an index in the SPI device table — as long as it's unique, it works. **No functional issue.**

---

## e) IMU/BARO/COMPASS Probes

| Sensor | ChibiOS (fmuv5 + CUAVv5 merged) | RTT | Status |
|--------|------|-----|--------|
| IMU Invensense icm20689 | ✅ | ✅ | ✅ Match |
| IMU Invensense icm20602 | ✅ | ✅ | ✅ Match |
| IMU Invensensev3 icm42688 | ✅ (CUAVv5) | ✅ | ✅ Match |
| IMU BMI055 | ✅ | ✅ | ✅ Match |
| IMU BMI088 | ✅ | ✅ | ✅ Match |
| BARO MS5611 | ✅ | ✅ | ✅ Match |
| COMPASS IST8310 internal | ✅ I2C:ALL_INTERNAL | ✅ PROBE_MAG_I2C (bus 0) | ✅ Equivalent |
| COMPASS IST8310 external | ✅ I2C:ALL_EXTERNAL | ❌ **Missing** | 🟡 IMPORTANT |
| HAL_PROBE_EXTERNAL_I2C_COMPASSES | ✅ defined | ❌ Commented out | 🟡 IMPORTANT |

**Compass note:** RTT uses `HAL_MAG_PROBE_LIST` (RTT-specific syntax) instead of ChibiOS `COMPASS` directive. The internal IST8310 is covered. External I2C compass probing is not enabled.

---

## f) UART Pin Differences

| UART | Signal | ChibiOS | RTT | Status |
|------|--------|---------|-----|--------|
| USART2 | RX | PD6 | PD6 AF7 | ✅ |
| USART2 | TX | PD5 | PD5 AF7 | ✅ |
| USART2 | CTS | PD3 | ❌ **Missing** | 🟡 IMPORTANT (flow control) |
| USART2 | RTS | PD4 | ❌ **Missing** | 🟡 IMPORTANT (flow control) |
| USART1 | RX | PB7 NODMA | PB7 AF7 | ✅ |
| USART1 | TX | PB6 NODMA | PB6 AF7 | ✅ |
| USART3 | RX | PD9 | PD9 AF7 | ✅ |
| USART3 | TX | PD8 | PD8 AF7 | ✅ |
| USART3 | CTS | PD11 | ❌ **Missing** | 🟡 IMPORTANT (flow control) |
| USART3 | RTS | PD12 | ❌ **Missing** | 🟡 IMPORTANT (flow control) |
| UART4 | RX | PD0 NODMA | PD0 AF8 | ✅ |
| UART4 | TX | PD1 NODMA | PD1 AF8 | ✅ |
| USART6 | RX | PG9 NODMA | PG9 AF8 | ✅ |
| USART6 | TX | PG14 NODMA (commented out!) | PG14 AF8 | ⚠️ ChibiOS has PG14 TX commented out (IOMCU SBUS conflict) |
| USART6 | CTS | PG15 | ❌ **Missing** | 🟡 IMPORTANT |
| USART6 | RTS | PG8 | ❌ **Missing** | 🟡 IMPORTANT |
| UART7 | RX | PF6 NODMA | PF6 AF8 | ✅ |
| UART7 | TX | PE8 NODMA | PE8 AF8 | ✅ |
| UART8 | RX | PE0 | PE0 AF8 | ✅ |
| UART8 | TX | PE1 | PE1 AF8 | ✅ |

**Key finding:** USART6 TX (PG14) is commented out in ChibiOS fmuv5 with note about IOMCU SBUS input conflict. RTT has it enabled. This could be a real hardware conflict — **investigate before enabling**.

All CTS/RTS flow control pins are missing from RTT. This means 422 telemetry at high baud rates will drop data.

**NODMA flag:** ChibiOS uses `NODMA` on several UARTs; RTT doesn't support this syntax but it's handled differently in RTT's driver layer.

---

## g) GPIO/Power Pin Differences

| Pin | ChibiOS | RTT | Status |
|-----|---------|-----|--------|
| PE3 VDD_3V3_SENSORS_EN | OUTPUT HIGH | ✅ OUTPUT HIGH | ✅ |
| PF12 nVDD_5V_HIPOWER_EN | OUTPUT HIGH | ❌ **Missing** | 🔴 CRITICAL (5V power rail) |
| PG4 nVDD_5V_PERIPH_EN | OUTPUT HIGH | ❌ **Missing** | 🔴 CRITICAL (5V peripheral power) |
| PG5 VDD_5V_RC_EN | OUTPUT HIGH | ✅ OUTPUT HIGH | ✅ |
| PG6 VDD_5V_WIFI_EN | OUTPUT HIGH | ❌ **Missing** | 🟡 IMPORTANT |
| PG7 VDD_3V3_SD_CARD_EN | OUTPUT HIGH | ✅ OUTPUT HIGH | ✅ |
| PA7 HEATER_EN | OUTPUT LOW GPIO(80) | ✅ OUTPUT LOW | ✅ |
| PG1 VDD_BRICK_nVALID | INPUT PULLUP | ❌ **Missing** | 🟡 IMPORTANT (power monitoring) |
| PG2 VDD_BRICK2_nVALID | INPUT PULLUP | ❌ **Missing** | 🟡 IMPORTANT |
| PG3 VBUS_nVALID | INPUT PULLUP | ❌ **Missing** | 🟡 IMPORTANT |
| PF13 VDD_5V_HIPOWER_nOC | INPUT PULLUP | ❌ **Missing** | 🟡 IMPORTANT |
| PE15 VDD_5V_PERIPH_nOC | INPUT PULLUP | ❌ **Missing** | 🟡 IMPORTANT |
| PB10 nSPI5_RESET_EXTERNAL1 | OUTPUT HIGH | ❌ **Missing** | 🟢 NICE_TO_HAVE |
| PH2 GPIO_CAN1_SILENT | OUTPUT LOW GPIO(70) | ❌ **Missing** | 🟡 IMPORTANT (CAN silent) |
| PH3 GPIO_CAN2_SILENT | OUTPUT LOW GPIO(71) | ❌ **Missing** | 🟡 IMPORTANT |
| PH4 GPIO_CAN3_SILENT | OUTPUT LOW GPIO(72) | ❌ **Missing** | 🟡 IMPORTANT |
| PB1 LED_RED | OUTPUT OPENDRAIN GPIO(90) | ❌ **Missing** | 🟡 IMPORTANT |
| PC6 LED_GREEN | OUTPUT GPIO(91) LOW | ❌ **Missing** | 🟡 IMPORTANT |
| PC7 LED_BLUE | OUTPUT GPIO(92) HIGH | ❌ **Missing** | 🟡 IMPORTANT |

**Critical:** `PF12 nVDD_5V_HIPOWER_EN` and `PG4 nVDD_5V_PERIPH_EN` are the 5V power enable pins. Without these, sensors on 5V (GPS, telemetry radios) won't have power.

---

## h) I2C Bus Configuration

| Bus | ChibiOS | RTT | Status |
|-----|---------|-----|--------|
| I2C1 (PB8/PB9) | ✅ | ❌ **Missing** | 🟡 IMPORTANT |
| I2C2 (PF1/PF0) | ✅ | ❌ **Missing** | 🟢 NICE_TO_HAVE |
| I2C3 (PH7/PH8) | ✅ | ✅ AF4 | ✅ Match |
| I2C4 (PF14/PF15) | ✅ | ❌ **Missing** | 🟢 NICE_TO_HAVE |
| I2C_ORDER | I2C3 I2C1 I2C2 I2C4 | N/A | — (ChibiOS-specific) |

**Note:** RTT only defines I2C3 pins. ChibiOS defines 4 I2C buses. I2C3 is the only one physically used on CUAV V5 for the internal IST8310 compass, so I2C1/2/4 are nice-to-have.

---

## i) PWM Output Differences

| Channel | ChibiOS | RTT | Status |
|---------|---------|-----|--------|
| PWM1 (TIM1_CH4 PE14) | ✅ GPIO(50) | ❌ **Missing** | 🔴 CRITICAL (AUX PWM) |
| PWM2 (TIM1_CH3 PA10) | ✅ GPIO(51) | ❌ **Missing** | 🔴 CRITICAL |
| PWM3 (TIM1_CH2 PE11) | ✅ GPIO(52) | ❌ **Missing** | 🔴 CRITICAL |
| PWM4 (TIM1_CH1 PE9) | ✅ GPIO(53) | ❌ **Missing** | 🔴 CRITICAL |
| PWM5 (TIM4_CH2 PD13) | ✅ GPIO(54) | ❌ **Missing** | 🔴 CRITICAL |
| PWM6 (TIM4_CH3 PD14) | ✅ GPIO(55) | ❌ **Missing** | 🔴 CRITICAL |
| PWM7 (TIM12_CH1 PH6) | ✅ GPIO(56) NODMA | ❌ **Missing** | 🔴 CRITICAL |
| PWM8 (TIM12_CH2 PH9) | ✅ GPIO(57) NODMA | ❌ **Missing** | 🔴 CRITICAL |
| Buzzer (TIM9_CH1 PE5) | ✅ GPIO(77) ALARM | ❌ **Missing** | 🟡 IMPORTANT |

**All 8 AUX PWM channels and buzzer are missing.** These are the FMU-side auxiliary PWM outputs used for servo/motor control when not using IOMCU.

**Patch needed (RTT syntax):**
```
# PWM AUX channels
PE14 TIM1_CH4 TIM1 AF1 PWM(1)
PA10 TIM1_CH3 TIM1 AF1 PWM(2)
PE11 TIM1_CH2 TIM1 AF1 PWM(3)
PE9  TIM1_CH1 TIM1 AF1 PWM(4)
PD13 TIM4_CH2 TIM4 AF2 PWM(5)
PD14 TIM4_CH3 TIM4 AF2 PWM(6)
PH6  TIM12_CH1 TIM12 AF9 PWM(7)
PH9  TIM12_CH2 TIM12 AF9 PWM(8)

# Buzzer
PE5 TIM9_CH1 TIM9 AF3 GPIO(77) ALARM
```

---

## j) SD Card Differences

| Pin | ChibiOS | RTT | Status |
|-----|---------|-----|--------|
| PC8 SDMMC_D0 | ✅ | ❌ **Missing** | 🟡 IMPORTANT |
| PC9 SDMMC_D1 | ✅ | ❌ **Missing** | 🟡 IMPORTANT |
| PC10 SDMMC_D2 | ✅ | ❌ **Missing** | 🟡 IMPORTANT |
| PC11 SDMMC_D3 | ✅ | ❌ **Missing** | 🟡 IMPORTANT |
| PC12 SDMMC_CK | ✅ | ❌ **Missing** | 🟡 IMPORTANT |
| PD2 SDMMC_CMD | ✅ | ❌ **Missing** | 🟡 IMPORTANT |

**All 6 SDMMC1 pins missing.** SD card logging won't work without these.

**Patch needed:**
```
# MicroSD
PC8  SDMMC1_D0 SDMMC1 AF12
PC9  SDMMC1_D1 SDMMC1 AF12
PC10 SDMMC1_D2 SDMMC1 AF12
PC11 SDMMC1_D3 SDMMC1 AF12
PC12 SDMMC1_CK SDMMC1 AF12
PD2  SDMMC1_CMD SDMMC1 AF12
```

---

## k) Safety Switch & Capture Pins

| Item | ChibiOS | RTT | Status |
|------|---------|-----|--------|
| `HAL_HAVE_SAFETY_SWITCH` | ✅ defined | ❌ Missing | 🟡 IMPORTANT |
| PA5 FMU_CAP1 | ✅ INPUT GPIO(58) | ❌ Missing | 🟢 NICE_TO_HAVE |
| PB3 FMU_CAP2 | ✅ INPUT GPIO(59) | ❌ Missing | 🟢 NICE_TO_HAVE |
| PB11 FMU_CAP3 | ✅ INPUT GPIO(60) | ❌ Missing | 🟢 NICE_TO_HAVE |
| PI0 FMU_SPARE_4 | ✅ INPUT GPIO(61) | ❌ Missing | 🟢 NICE_TO_HAVE |

---

## l) DRDY Pins (ChibiOS only — data ready interrupts for IMUs)

| Pin | Label | Status in RTT |
|-----|-------|---------------|
| PE7 DRDY8_NC | ❌ Missing | 🟢 NICE_TO_HAVE |
| PB4 DRDY1_ICM20689 | ❌ Missing | 🟡 IMPORTANT (IMU performance) |
| PB14 DRDY2_BMI055_GYRO | ❌ Missing | 🟡 IMPORTANT |
| PB15 DRDY3_BMI055_ACC | ❌ Missing | 🟡 IMPORTANT |
| PC5 DRDY4_ICM20602 | ❌ Missing | 🟡 IMPORTANT |
| PC13 DRDY5_BMI055_GYRO | ❌ Missing | 🟡 IMPORTANT |
| PD10 DRDY6_BMI055_ACC | ❌ Missing | 🟡 IMPORTANT |
| PD15 DRDY7_EXTERNAL1 | ❌ Missing | 🟢 NICE_TO_HAVE |

DRDY pins are important for IMU performance (avoid polling). However, RTT may handle these differently (polling vs interrupt-driven).

---

## l) CAN Bus

| Pin | ChibiOS | RTT | Status |
|-----|---------|-----|--------|
| PI9 CAN1_RX | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE |
| PH13 CAN1_TX | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE |
| PB12 CAN2_RX | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE |
| PB13 CAN2_TX | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE |
| PH2 CAN1_SILENT | ✅ | ❌ Missing | See GPIO section |
| PH3 CAN2_SILENT | ✅ | ❌ Missing | See GPIO section |

---

## l) SWD Debug Pins

| Pin | ChibiOS | RTT | Status |
|-----|---------|-----|--------|
| PA13 JTMS-SWDIO | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE (debug only) |
| PA14 JTCK-SWCLK | ✅ | ❌ Missing | 🟢 NICE_TO_HAVE (debug only) |

---

## l) SPEKTRUM Power

| Pin | ChibiOS | RTT | Status |
|-----|---------|-----|--------|
| PE4 SPEKTRUM_PWR | ✅ OUTPUT HIGH GPIO(73) | ❌ Missing | 🟢 NICE_TO_HAVE |

---

## Summary — Priority Patch List

### 🔴 CRITICAL (system won't function properly)

1. **SPI bus pins missing** — SPI1, SPI2, SPI4 all lack SCK/MISO/MOSI definitions
   - Without these, no IMU/baro/FRAM will initialize
2. **5V power enable pins missing** — PF12 (nVDD_5V_HIPOWER_EN), PG4 (nVDD_5V_PERIPH_EN)
   - Sensors on 5V rail won't get power
3. **All 8 PWM AUX channels missing** — TIM1/TIM4/TIM12 pin definitions
   - FMU-side PWM output non-functional

### 🟡 IMPORTANT (feature missing, system boots)

4. **SD card pins missing** — all 6 SDMMC1 pins
5. **UART CTS/RTS missing** — USART2, USART3, USART6 flow control pins
6. **HAL_HAVE_SAFETY_SWITCH missing**
7. **Status LEDs missing** — PB1 (RED), PC6 (GREEN), PC7 (BLUE)
8. **Power monitoring GPIOs missing** — PG1, PG2, PG3, PF13, PE15 (nVALID/nOC)
9. **CAN silent pins missing** — PH2, PH3, PH4
10. **VDD_5V_WIFI_EN (PG6) missing**
11. **HAL_GPIO_A_LED_PIN / HAL_GPIO_B_LED_PIN missing**
12. **HAL_DEFAULT_INS_FAST_SAMPLE missing**
13. **External I2C compass probing disabled**
14. **I2C1/I2C2/I2C4 buses missing** (only I2C3 defined)
15. **DRDY pins missing** (affects IMU polling performance)
16. **Buzzer pin missing** (PE5 TIM9_CH1)

### 🟢 NICE_TO_HAVE

17. SPARE2 ADC (PA4)
18. Capture pins (PA5, PB3, PB11, PI0)
19. CAN bus pins
20. SWD debug pins
21. SPEKTRUM power pin
22. Dshot ROMFS firmware
23. CONFIG_HAL_BOARD_SUBTYPE define

### ⚠️ Potential Conflict

- **USART6 TX (PG14):** ChibiOS fmuv5 comments this out to avoid conflict with IOMCU SBUS input. RTT has it enabled. **Verify this doesn't cause SBUS issues.**
