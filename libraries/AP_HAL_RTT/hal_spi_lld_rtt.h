/*
 * RTT adaptation of ChibiOS STM32 SPI LLD (SPIv2).
 * ChibiOS reference: modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv2/hal_spi_lld.c
 *
 * Key differences from ChibiOS:
 *  - Replace SPIDriver struct with SPI_TypeDef* + static DMA config table
 *  - Remove ChibiOS OSAL (osalDbgAssert, osalSysHalt)
 *  - Remove DMAMUX support (STM32F767 has none)
 *  - ISR-free: poll DMA EN bits for completion instead of interrupt
 *  - No circular buffer support (SPI_SUPPORTS_CIRCULAR not needed)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DMA stream/channel mapping for each SPI bus (1-based) ---- */
struct rtt_spi_dma_config {
    void *rx_stream;   /* DMA_Stream_TypeDef *RX */
    uint32_t ch_rx;    /* DMA channel for RX */
    void *tx_stream;   /* DMA_Stream_TypeDef *TX */
    uint32_t ch_tx;    /* DMA channel for TX */
};

/*
 * spi_lld_init_rtt — enable DMA clocks, build mode bitmasks.
 * Safe to call multiple times.
 */
void spi_lld_init_rtt(void);

/*
 * spi_lld_exchange_rtt — full-duplex DMA exchange.
 * Polls for DMA completion (no ISR).
 *
 * @param spi        SPI peripheral register base (SPI1, SPI4, etc.)
 * @param bus_index  AP bus index (1=SPI1, 4=SPI4)
 * @param txbuf      TX data buffer
 * @param rxbuf      RX data buffer
 * @param n          number of bytes
 * @return           true on success, false on timeout/error
 */
bool spi_lld_exchange_rtt(void *spi, int bus_index,
                          const void *txbuf, void *rxbuf, size_t n);

/*
 * spi_lld_abort_rtt — abort DMA for the given bus.
 * Disables both RX/TX DMA streams.
 */
void spi_lld_abort_rtt(void *spi, int bus_index);

/*
 * spi_lld_polled_rtt — single-byte polled exchange.
 * Extracted from ChibiOS spi_lld_polled_exchange().
 * Uses 8-bit register access.
 *
 * @param spi    SPI peripheral register base (SPI1, SPI4, etc.)
 * @param frame  byte to send
 * @return       received byte
 */
uint16_t spi_lld_polled_rtt(void *spi, uint16_t frame);

#ifdef __cplusplus
}
#endif
