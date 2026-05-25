/*
 * RTT adaptation of ChibiOS STM32 SPI LLD (SPIv2).
 * ChibiOS reference: modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv2/hal_spi_lld.c
 *
 * Changes from ChibiOS:
 *  - Replace SPIDriver struct with direct SPI_TypeDef* + static DMA config table
 *  - Remove ChibiOS OSAL dependencies (osalDbgAssert, CH_CFG_ST_RESET, etc.)
 *  - Remove DMAMUX support (not present on STM32F767)
 *  - Remove ISR-based completion signaling (poll EN bit instead)
 *  - Add polled exchange for single-byte transfers
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DMA stream/channel mapping for each SPI bus (1-based) */
struct rtt_spi_dma_config {
    void *rx_stream;   /* DMA_Stream_TypeDef * */
    uint32_t ch_rx;    /* DMA channel for RX */
    void *tx_stream;   /* DMA_Stream_TypeDef * */
    uint32_t ch_tx;    /* DMA channel for TX */
};

/*
 * Initialize LLD — enable DMA clock, configure mode bitmasks.
 * Safe to call multiple times (guarded internally).
 */
void rtt_spi_lld_init(void);

/*
 * Full-duplex DMA exchange on the given SPI bus.
 *
 * @param spi         SPI peripheral register base (SPI1, SPI4, etc.)
 * @param bus_index   AP bus index (1 = SPI1, 4 = SPI4)
 * @param txbuf       TX data buffer (must not be NULL)
 * @param rxbuf       RX data buffer (must not be NULL)
 * @param n           number of bytes to exchange
 * @return            true on success, false on timeout/error
 */
bool rtt_spi_lld_exchange(void *spi, int bus_index,
                          const uint8_t *txbuf, uint8_t *rxbuf, size_t n);

/*
 * Polled single-byte exchange — no DMA.
 * Extracted from ChibiOS spi_lld_polled_exchange().
 *
 * @param spi    SPI peripheral register base
 * @param frame  byte to send
 * @return       received byte
 */
uint16_t rtt_spi_lld_polled_exchange(void *spi, uint16_t frame);

#ifdef __cplusplus
}
#endif
