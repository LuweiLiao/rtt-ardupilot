#pragma once
/*
 * CMSIS SPI DMA RX IRQ hook for AP_HAL_RTT _spi_dma_xfer (Fix#4-A-low).
 * Called from BSP SPI1/SPI4 DMA RX IRQ handlers before HAL/LLD dispatch.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Return 1 if this IRQ was handled (CMSIS path active transfer). */
int rtt_spi_cmsis_dma_rx_irq_handler(uint8_t bus);

#ifdef __cplusplus
}
#endif
