# FRAM Init Failure — Root Cause Analysis

## Symptom
Storage falls back to Stub (volatile, no persistence). `_fram.init()` fails.

## Root Cause
**`libraries/AP_Ramtron/` does not exist in the project.**

The RTT Storage.cpp guards FRAM usage with `#if HAL_WITH_RAMTRON`, and `hwdef/cuav_v5/hwdef.dat` line 188 defines `HAL_WITH_RAMTRON 1`. However, the `AP_Ramtron` library itself is not present in the tree — no source files, no headers, no SConscript.

So `_fram.init()` compiles (the type exists in Storage.h as `AP_Ramtron _fram;`), but either:
1. The header resolves to an empty/stub, or
2. The library is missing from the build system entirely and `init()` returns false immediately.

## Comparison with ChibiOS
ChibiOS `hwdef/fmuv5/hwdef.dat` also defines `HAL_WITH_RAMTRON 1` with identical SPI2 pin config — but ChibiOS has the full `libraries/AP_Ramtron/` library available in the ArduPilot tree.

## hwdef SPI2 Config (correct)
```
SPIDEV ramtron  SPI2  DEVID1  RAMTRON_CS  MODE3  8*MHZ  8*MHZ
PI1  SPI2_SCK   SPI2  AF5
PI2  SPI2_MISO  SPI2  AF5
PI3  SPI2_MOSI  SPI2  AF5
```
This matches the ChibiOS reference — SPI2 pin mux is fine.

## SPIDevice.cpp
No FRAM/SPI2/DEVICE_SPI references found in `SPIDevice.cpp`. This is a secondary concern — once the Ramtron library is added, SPIDevice needs to properly handle the `ramtron` SPIDEV entry (bus selection, CS, mode). This should be verified after adding the library.

## Fix Required
1. **Copy `libraries/AP_Ramtron/`** from upstream ArduPilot into the project
2. **Add AP_Ramtron to the RTT build** (SConscript / Makefile — verify it gets compiled under `HAL_WITH_RAMTRON`)
3. **Verify SPIDevice.cpp** handles `ramtron` device on SPI2 with MODE3, DEVID1
4. **Verify RAMTRON_CS pin** is defined and configured as GPIO output
