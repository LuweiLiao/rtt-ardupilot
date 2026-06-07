/*
 * RTT adaptation of ChibiOS STM32 SPI LLD (SPIv2).
 * ChibiOS reference: modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv2/hal_spi_lld.c
 *
 * Extracted from ChibiOS hal_spi_lld.c (lines 597-722) and adapted for
 * RT-Thread / AP_HAL_RTT on STM32F767 (CUAV V5).
 *
 * Key differences from ChibiOS original:
 *  - Direct SPI_TypeDef* parameter instead of SPIDriver struct
 *  - Static DMA config table instead of runtime dmaStreamAlloc()
 *  - Poll for DMA completion instead of ISR-based signaling
 *  - No ChibiOS OSAL dependencies (osalDbgAssert, osalSysHalt removed)
 *  - No DMAMUX support (F767 has none)
 *  - No circular buffer support
 */

#include "hal_spi_lld.h"

/* CMSIS register access for STM32F7 */
#include <stm32f7xx.h>

/* ========================================================================== */
/* Local constants                                                             */
/* ========================================================================== */

/* DMA stream stride between consecutive stream registers */
#define DMA_STREAM_STRIDE       0x18U

/* ========================================================================== */
/* DMA configuration table — 1-based bus index                                */
/* ========================================================================== */

/* Verified against CUAV V5 hwdef.dat & STM32F767 reference manual:
 *   SPI1: RX=DMA2_Stream2 CH=3, TX=DMA2_Stream5 CH=3
 *   SPI4: RX=DMA2_Stream0 CH=4, TX=DMA2_Stream1 CH=4
 */
static const struct rtt_spi_dma_config _dma_cfg[7] = {
    [0] = { NULL, 0, NULL, 0 },                       /* unused */
    [1] = { DMA2_Stream2, 3, DMA2_Stream5, 3 },        /* SPI1 */
    [2] = { NULL, 0, NULL, 0 },                        /* SPI2 (RTT framework) */
    [3] = { NULL, 0, NULL, 0 },                        /* SPI3 (RTT framework) */
    [4] = { DMA2_Stream0, 4, DMA2_Stream1, 4 },        /* SPI4 */
    [5] = { NULL, 0, NULL, 0 },                        /* SPI5 (unused) */
    [6] = { NULL, 0, NULL, 0 },                        /* SPI6 (unused) */
};

/* ========================================================================== */
/* DMA mode bitmasks — set up once by rtt_spi_lld_init()                      */
/* ========================================================================== */
/* RX: PERIPH→MEM, TCIE, DMEIE, TEIE                                         */
/* TX: MEM→PERIPH, DMEIE, TEIE  (no TCIE — RX completion = transaction done) */
/* PSIZE/MSIZE set per-transfer in exchange function                           */
/* Priority level = 1 (medium), matches ChibiOS STM32_SPI_SPI1_DMA_PRIORITY   */

static uint32_t _rx_mode[7];
static uint32_t _tx_mode[7];

/* ========================================================================== */
/* Local helpers                                                              */
/* ========================================================================== */

/* Compute stream index within DMA controller (0-7). */
static inline uint32_t _stream_index(DMA_Stream_TypeDef *s)
{
    return ((uint32_t)s - (uint32_t)DMA2) / DMA_STREAM_STRIDE;
}

/* Disable a DMA stream and wait for EN to clear. */
static inline void _stream_disable(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    uint32_t tout = 10000;
    while ((s->CR & DMA_SxCR_EN) && --tout) { __NOP(); }
}

/* Clear all interrupt flags for a given DMA stream (LIFCR or HIFCR). */
static inline void _stream_clear_flags(DMA_Stream_TypeDef *s)
{
    uint32_t idx = _stream_index(s);
    uint32_t group = idx & 3U;                      /* stream 0-3 in group */
    uint32_t shift = group * 6U;
    uint32_t mask = (DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 |
                     DMA_LIFCR_CTEIF0  | DMA_LIFCR_CDMEIF0) << shift;
    if (idx < 4U) {
        DMA2->LIFCR = mask;
    } else {
        DMA2->HIFCR = mask;
    }
}

/* Check TEIF/DMEIF for a stream in LISR or HISR. Returns true if error. */
static inline bool _stream_has_error(DMA_Stream_TypeDef *s)
{
    uint32_t idx = _stream_index(s);
    uint32_t group = idx & 3U;
    uint32_t shift = group * 6U;
    uint32_t teif_mask = DMA_LISR_TEIF0 << shift;
    uint32_t dmeif_mask = DMA_LISR_DMEIF0 << shift;
    if (idx < 4U) {
        return (DMA2->LISR & (teif_mask | dmeif_mask)) != 0;
    } else {
        return (DMA2->HISR & (teif_mask | dmeif_mask)) != 0;
    }
}

/* ========================================================================== */
/* Exported functions                                                         */
/* ========================================================================== */

void rtt_spi_lld_init(void)
{
    static bool init_done = false;
    if (init_done) {
        return;
    }

    /* Enable DMA2 clock (shared by all SPI DMA streams on STM32F7) */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    /* ── SPI1: RX=DMA2_Stream2 CH=3, TX=DMA2_Stream5 CH=3 ── */
    _rx_mode[1] = (3U << DMA_SxCR_CHSEL_Pos) |        /* CHSEL=3 */
                  DMA_SxCR_TCIE |
                  DMA_SxCR_DMEIE |
                  DMA_SxCR_TEIE;
    /* PL=1 (medium priority) */
    _rx_mode[1] |= (1U << DMA_SxCR_PL_Pos);
    /* DIR=00 (P2M) is default, no need to set explicitly */

    _tx_mode[1] = (3U << DMA_SxCR_CHSEL_Pos) |        /* CHSEL=3 */
                  DMA_SxCR_DIR_0 |                     /* M2P */
                  DMA_SxCR_DMEIE |
                  DMA_SxCR_TEIE;
    _tx_mode[1] |= (1U << DMA_SxCR_PL_Pos);

    /* ── SPI4: RX=DMA2_Stream0 CH=4, TX=DMA2_Stream1 CH=4 ── */
    _rx_mode[4] = (4U << DMA_SxCR_CHSEL_Pos) |        /* CHSEL=4 */
                  DMA_SxCR_TCIE |
                  DMA_SxCR_DMEIE |
                  DMA_SxCR_TEIE;
    _rx_mode[4] |= (1U << DMA_SxCR_PL_Pos);

    _tx_mode[4] = (4U << DMA_SxCR_CHSEL_Pos) |        /* CHSEL=4 */
                  DMA_SxCR_DIR_0 |
                  DMA_SxCR_DMEIE |
                  DMA_SxCR_TEIE;
    _tx_mode[4] |= (1U << DMA_SxCR_PL_Pos);

    init_done = true;
}

bool rtt_spi_lld_exchange(void *spi_ptr, int bus_index,
                          const uint8_t *txbuf, uint8_t *rxbuf, size_t n)
{
    SPI_TypeDef *spi = (SPI_TypeDef *)spi_ptr;

    rtt_spi_lld_init();   /* ensure initialized */

    if (bus_index < 0 || bus_index > 6) {
        return false;
    }
    const struct rtt_spi_dma_config *dma = &_dma_cfg[bus_index];
    DMA_Stream_TypeDef *rx = (DMA_Stream_TypeDef *)dma->rx_stream;
    DMA_Stream_TypeDef *tx = (DMA_Stream_TypeDef *)dma->tx_stream;
    if (rx == NULL || tx == NULL) {
        return false;
    }

    /* Build per-transfer mode masks with 8-bit data size */
    uint32_t rx_mode = _rx_mode[bus_index];    /* PSIZE=00, MSIZE=00 */
    uint32_t tx_mode = _tx_mode[bus_index];
    rx_mode |= DMA_SxCR_MINC;
    tx_mode |= DMA_SxCR_MINC;
    /* Ensure PSIZE=MSIZE=00 (byte) */
    rx_mode &= ~(DMA_SxCR_PSIZE_Msk | DMA_SxCR_MSIZE_Msk);
    tx_mode &= ~(DMA_SxCR_PSIZE_Msk | DMA_SxCR_MSIZE_Msk);

    /* ── Disable DMA streams (required before reconfiguration) ── */
    _stream_disable(rx);
    _stream_disable(tx);

    /* ── Clear stale interrupt flags for both streams ── */
    _stream_clear_flags(rx);
    _stream_clear_flags(tx);

    /* ── Configure RX stream ── */
    rx->PAR  = (uint32_t)&spi->DR;
    rx->M0AR = (uint32_t)rxbuf;
    rx->NDTR = n;
    rx->FCR  = 0;
    rx->CR   = rx_mode;

    /* ── Configure TX stream ── */
    tx->PAR  = (uint32_t)&spi->DR;
    tx->M0AR = (uint32_t)txbuf;
    tx->NDTR = n;
    tx->FCR  = 0;
    tx->CR   = tx_mode;

    __DSB();

    /* ── Enable RX first, then TX (ChibiOS convention) ── */
    rx->CR |= DMA_SxCR_EN;
    tx->CR |= DMA_SxCR_EN;
    __DSB();

    /* ── Poll for completion ── */
    /* ChibiOS LLD enables SPI_CR2_RXDMAEN|TXDMAEN once in spi_lld_start(),
     * so by the time we reach here the SPI DMA requests are already enabled.
     * We only need to enable the DMA streams — the SPI peripheral automatically
     * asserts DMA requests on TXE/RXNE.
     *
     * The hardware clears DMA_SxCR_EN when NDTR reaches 0 (transfer complete).
     * We poll both EN bits; when both are 0, the transaction is done. */
    uint32_t timeout = 20000U + n * 32U;
    while (timeout--) {
        if (!(rx->CR & DMA_SxCR_EN) && !(tx->CR & DMA_SxCR_EN)) {
            /* Wait for BSY — the last byte may still be shifting in */
            uint32_t bsy = 10000;
            while ((spi->SR & SPI_SR_BSY) && --bsy) { __NOP(); }

            /* Check for DMA errors */
            if (_stream_has_error(rx) || _stream_has_error(tx)) {
                _stream_clear_flags(rx);
                _stream_clear_flags(tx);
                return false;
            }
            return true;
        }
        __NOP();
    }

    /* Timeout — force-disable both streams */
    _stream_disable(rx);
    _stream_disable(tx);
    return false;
}

uint16_t rtt_spi_lld_polled_exchange(void *spi_ptr, uint16_t frame)
{
    SPI_TypeDef *spi = (SPI_TypeDef *)spi_ptr;

    /*
     * ChibiOS reference (hal_spi_lld.c:700-722):
     * Byte-size access for transactions that are <= 8-bit.
     * Our IMU SPI uses 8-bit data frames (SPI_CR2_DS = 0111).
     */
    volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
    *spidr = (uint8_t)frame;
    while ((spi->SR & SPI_SR_RXNE) == 0U) { }
    return (uint16_t)*spidr;
}
