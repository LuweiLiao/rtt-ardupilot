# ICM20689 WHO_AM_I Mismatch — Complete Diagnosis

## Task: t_cea704cf (child of t_3c4330fd)
## Investigator: imu-agent
## Date: 2025-05-23

---

## 1. Evidence Summary

### Observed Symptom
- ICM20689 probe fails with WHO_AM_I mismatch
- Expected: 0x98 (MPU_WHOAMI_ICM20689)
- Actual reads (from rtt_spi1_rt.last_recv_0): 0x48, 0x5E, 0x58 (inconsistent!)
- System stuck at setup_stage=680 in `_backends[0]->start()`
- SPI1 hardware confirmed working: 112k+ transfers, non-zero MISO data

### SPI Path Verified Correct
The register-level polling path in SPIDevice.cpp has been verified:
- Correct SPI mode: MODE3 (CPOL=1, CPHA=1) ✓
- Correct data width: 8-bit ✓
- Halves-duplex: send(0xF5=0x75|0x80) → receive(dummy) → send(0x00) → receive(WHO_AM_I) ✓
- CS assertion/deassertion timing: correct GPIO BSRR manipulation ✓
- CS pin for ICM20689 = PF2 = 82 = `spi11` ✓

## 2. Critical Finding: RTT hwdef != actual CUAV V5 hardware

### ChibiOS CUAVv5/hwdef.dat (REAL HARDWARE):
```
include ../fmuv5/hwdef.dat    # ICM20689 (PF2), ICM20602 (PF3), BMI...

# THEN OVERRIDES DEVID2 with:
PF11 ICM42688_CS CS SPEED_VERYLOW
SPIDEV icm42688 SPI1 DEVID2 ICM42688_CS MODE3 2*MHZ 8*MHZ

# Probe ORDER (3 IMUs):
IMU Invensense SPI:icm20689          ROTATION_NONE              # PF2
IMU Invensense SPI:icm20602          ROTATION_NONE              # PF3
IMU Invensensev3 SPI:icm42688        ROTATION_PITCH_180_YAW_270 # PF11
```

### RTT cuav_v5/hwdef.dat (CURRENT):
```
SPIDEV icm20689    SPI1 DEVID1  ICM20689_CS   MODE3  2*MHZ  8*MHZ    # PF2
SPIDEV icm20602    SPI1 DEVID2  ICM20602_CS   MODE3  2*MHZ  8*MHZ    # PF3
SPIDEV bmi055_g    SPI1 DEVID3  BMI055_G_CS   MODE3 10*MHZ 10*MHZ   # PF4
SPIDEV bmi055_a    SPI1 DEVID4  BMI055_A_CS   MODE3 10*MHZ 10*MHZ   # PG10
# NO ICM42688 entry!

IMU Invensense SPI:icm20689 ROTATION_NONE              # Only probe
# ICM20602 DISABLED
# No ICM42688 probe
```

### Generated HAL_INS_PROBE_LIST (from build_old/rtt_cuav_v5/hwdef.h):
```c
#define HAL_INS_PROBE1  ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this,hal.spi->get_device("icm20689"),ROTATION_NONE))
#define HAL_INS_PROBE2  ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this,hal.spi->get_device("icm20602"),ROTATION_NONE))
#define HAL_INS_PROBE_LIST HAL_INS_PROBE1;HAL_INS_PROBE2
```

### WHO_AM_I Reference Table
| Chip           | WHO_AM_I | Driver           | In RTT hwdef? |
|----------------|----------|------------------|----------------|
| ICM20689       | 0x98     | Invensense (gen1)| YES (probed)   |
| ICM20602       | 0x12     | Invensense (gen1)| YES (disabled) |
| ICM-42688_P    | 0x47     | Invensensev3     | NO             |
| ICM-42688_V    | 0xDB     | Invensensev3     | NO             |
| ICM-42605      | 0x42     | Invensensev3     | NO             |

## 3. Root Cause Analysis

### Hypothesis A (Most Likely): Wrong hardware layout — PF2 has a different chip
The actual CUAV V5 hardware may have a revision where:
- PF2 carries either ICM-42688 or a variant (WHO_AM_I close to 0x47)
- OR the chip is from a different manufacturer entirely

Supporting evidence:
- 0x48 reading is exactly 1 bit off from ICM-42688_P's 0x47 (possible SPI timing marginality)
- Inconsistent reads (0x48, 0x5E, 0x58) suggest noise/timing, not a clean chip ID
- ChibiOS CUAVv5 explicitly adds ICM-42688 support — RTT port doesn't

### Hypothesis B: ICM20689 damaged or disconnected
Possible but less likely given MS5611 on SPI4 works fine.

### Hypothesis C: SPI timing marginality
At SPEED_LOW: BR=5 → 1.6875MHz. At SPEED_HIGH: BR=3 → 6.75MHz.
These speeds should be fine for ICM20689. But if the board has a different chip, timing might not be optimal.

## 4. Recommended Actions

### Step 1: GDB Probe of IMU Registers (BEFORE any AP init)
Connect via OpenOCD and read the following before `continue`:
```gdb
# Read WHO_AM_I from SPI1 with ICM20689 CS (PF2) selected
# Use register-level SPI access via STM32F7 registers
# Read these IMU registers:
# - 0x75 (WHO_AM_I) — expected 0x98 or 0x47
# - 0x6B (PWR_MGMT_1) — check power state
# - 0x6A (USER_CTRL) — check I2C_IF_DIS
# - 0x37 (INT_PIN_CFG) — check BYPASS_EN
```

### Step 2: Add ICM-42688 to RTT hwdef
Add to hwdef.dat:
```
PF11 ICM42688_CS CS
SPIDEV icm42688  SPI1 DEVID2  ICM42688_CS  MODE3  2*MHZ  8*MHZ
IMU Invensensev3 SPI:icm42688 ROTATION_PITCH_180_YAW_270
```
This matches ChibiOS CUAVv5 exactly.

### Step 3: If ICM20689 is correct chip, probe other registers
Read MPUREG_PWR_MGMT_1 (0x6B), MPUREG_INT_PIN_CFG (0x37), MPUREG_USER_CTRL (0x6A)
If ALL return garbage, the chip on PF2 is not an Invensense IMU.

### Step 4: Verify SPI1 CS line isolation
Disconnect PF2 and probe another device on SPI1 (e.g., BMI055_G on PF4).
If PF4 reads correct WHO_AM_I and PF2 still reads garbage, the ICM20689 chip is bad/not present.

## 5. File Inventory

Files analyzed:
- libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat — RTT CUAV V5 hwdef
- libraries/AP_HAL_ChibiOS/hwdef/CUAVv5/hwdef.dat — ChibiOS CUAV V5 hwdef
- libraries/AP_HAL_ChibiOS/hwdef/fmuv5/hwdef.dat — Base FMUv5 hwdef
- libraries/AP_InertialSensor/AP_InertialSensor_Invensense_registers.h — WHO_AM_I constants
- libraries/AP_InertialSensor/AP_InertialSensor_Invensense.cpp — Driver implementation
- libraries/AP_InertialSensor/AP_InertialSensor_Invensensev3.cpp — ICM-42688 driver
- libraries/AP_HAL_RTT/SPIDevice.cpp — RTT SPI driver
- libraries/AP_HAL_RTT/SPIDevice.h — RTT SPI device header
- libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py — hwdef generator
- build_old/rtt_cuav_v5/hwdef.h — Generated header

