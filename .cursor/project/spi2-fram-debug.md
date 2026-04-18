# SPI2 FRAM Init Failure Debug

## Problem
`_fram.init()` returns false despite "ramtron" SPI device being registered (SPI2, CS=PF5, DEVID1).

## Init Flow
```
Storage::_storage_open()
  → _fram.init()              // AP_RAMTRON.cpp
    → hal.spi->get_device_ptr("ramtron")  // SPIDeviceManager: finds device, returns SPIDevice
    → dev->read_registers(0x9F, rdid, 9)  // Device.cpp: transfer(0x9F, 1, rdid, 9)
    → id lookup fails → "Unknown RAMTRON device" → return false
```

## Key Finding: Most Likely Root Cause

**SPI2 uses RT-Thread HAL polling path, same as SPI1 had before the workaround.**

SPI1 had a known bug where `HAL_SPI_TransmitReceive` returns incorrect data for multi-byte reads on STM32F7. The workaround was direct register-level polling in `spi1_poll_transfer()` — but this is only enabled for `_desc.bus == 1`.

**SPI2 (bus=2) has NO such workaround.** It goes through `rt_spi_transfer_message()` → RTT HAL → `HAL_SPI_TransmitReceive`, which likely returns garbage (0xFF or zeros) for the RDID read.

### Evidence
1. `rtconfig.h`: `BSP_USING_SPI2=y`, but DMA is **disabled** (`BSP_SPI2_RX_USING_DMA` commented out)
2. `SPIDevice.cpp` line ~230: only `if (_desc.bus == 1)` gets the register workaround
3. `rt_board_init.c`: no SPI2 LLD DMA context registered (only SPI1 and SPI4)
4. SPI1 comment: *"The RTT HAL polling path (HAL_SPI_TransmitReceive) returns incorrect data for multi-byte reads on SPI1"* — the same HAL is used for SPI2

## Configuration Summary

| Item | Value | Status |
|------|-------|--------|
| SPI2 enabled | `CONFIG_BSP_USING_SPI2=y` | ✅ |
| SPI2 pins | PI1=SCK, PI2=MISO, PI3=MOSI (AF5) | ✅ |
| SPI2 CS | PF5 (RAMTRON_CS) | ✅ |
| SPI2 attach | spi21 via INIT_DEVICE_EXPORT | ✅ |
| SPI2 DMA | **DISABLED** | ⚠️ |
| SPI2 register workaround | **NOT APPLIED** (only SPI1) | 🔴 |
| hwdef SPIDEV | `ramtron SPI2 DEVID1 RAMTRON_CS MODE3 8MHz` | ✅ |
| CUAV V5 FRAM chip | FM25V02A on SPI2 | ✅ (matches ChibiOS hwdef) |

## Diagnostic Steps (GDB)

1. Break at `AP_RAMTRON::init()` and check `rdid[0..8]` after `read_registers`:
   - If all 0xFF → SPI bus not responding (pin/config issue)
   - If all 0x00 → same
   - If first byte is 0x9F (echo of command) → MOSI/MISO crossed or CS timing issue
   - FM25V02A RDID should return: manufacturer=0x7F (Cypress), id1=0x22, id2=0x08

2. Check `rtt_dbg_setup_stage`:
   - 501 = FRAM init attempted
   - 502 = FRAM init failed, trying Flash

3. Check SPI-ATTACH log: `[SPI-ATTACH] spi2 -> spi21 cs=0x... ret=0`

## Fix Options

### Option A: Extend SPI1 register workaround to SPI2 (Recommended)
In `SPIDevice.cpp`, change `if (_desc.bus == 1)` to also cover bus 2, or generalize for all STM32F7 buses without DMA.

### Option B: Enable SPI2 DMA
Uncomment `BSP_SPI2_RX_USING_DMA` / `BSP_SPI2_TX_USING_DMA` in `rtconfig.h` and verify DMA path works. Note: SPI1 DMA was disabled for a reason — may need investigation.

### Option C: Debug the HAL polling path
Investigate why `HAL_SPI_TransmitReceive` fails on STM32F7 SPI (FIFO issue suspected). This fixes the root cause for all buses.

## Secondary Check: FRAM Power
Verify VDD_3V3_SENSORS_EN is HIGH before SPI init. The FRAM is powered from the sensor rail. `_sensor_power_init()` runs as `INIT_PREV_EXPORT` (before `INIT_DEVICE_EXPORT` for SPI attach), so this should be fine — but verify in serial log.
