# Task: ChibiOS SPI LLD → RTT Port, Phase 1

## Background
RTT ArduPilot port on CUAV V5 (STM32F767). Current SPI driver (SPIDevice.cpp) has DMA bugs and polled mode hangs. ChibiOS hal_spi_lld.c (726 lines, zero RTOS calls) works correctly on the same hardware.

## Files to study
- source: modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv2/hal_spi_lld.c (726 lines)
- source: modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv2/hal_spi_lld.h (header)
- source: modules/ChibiOS/os/common/ext/ST/STM32F7xx/stm32f767xx.h (CMSIS)
- target: libraries/AP_HAL_RTT/SPIDevice.cpp (995 lines, current RTT driver)

## DMA mapping (F767)
- SPI1: RX=DMA2_Stream2 CH=3, TX=DMA2_Stream5 CH=3
- SPI4: RX=DMA2_Stream0 CH=4, TX=DMA2_Stream1 CH=4

## Steps
1. mkdir -p libraries/AP_HAL_RTT/drivers/
2. Extract core ChibiOS hal_spi_lld.c register code
3. Create hal_spi_lld_rtt.c with:
   - Replace SPIDriver* with SPI_TypeDef* direct
   - Static DMA config table
   - Remove osalDbgAssert and ChibiOS macros
   - Export: spi_lld_init_rtt(), spi_lld_exchange_rtt(), spi_lld_abort_rtt(), spi_lld_polled_rtt()
4. Modify SPIDevice.cpp _spi_dma_xfer() to call spi_lld_exchange_rtt()
5. Compile: scons --v=ArduCopter --target=cuav-v5 -j$(nproc)
6. Flash: openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000 verify" -c "reset run"
7. Verify: setup_stage, fast_loop_count, MAVLink heartbeat

## Verification
After flash + 20s wait:
- Read 0x2001b3d0: should be > 680 (past IMU init)
- MAVLink on /dev/ttyACM1: status should be >= 3 (STANDBY)
- No HardFault