/*
 * drv_spi_lld.h — STM32F7 SPI Low-Level DMA driver
 *
 * Replaces the HAL SPI DMA transfer path with direct LL/register operations
 * to eliminate the SPI_EndRxTxTransaction() busy-wait from the DMA ISR.
 *
 * HAL path:  DMA IRQ → busy-wait FIFO+BSY in ISR (blocks USB OTG IRQ)
 * LLD path:  DMA IRQ → clear flags → rt_completion_done (< 5 µs total)
 *            Thread:  → poll BSY with rt_thread_yield (almost never spins)
 */

#pragma once
#ifdef SOC_SERIES_STM32F7

#include <stdint.h>
#include <rtthread.h>
#include <ipc/completion.h>
#include "stm32f7xx.h"
#include "stm32f7xx_ll_dma.h"
#include "stm32f7xx_ll_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Describes one SPI bus's LLD context.
 * Pre-store ISR/IFCR pointers and bit masks so the ISR itself has zero
 * computation — just a load + store + completion signal.
 */
typedef struct spi_lld_bus {
    SPI_TypeDef        *spi;
    DMA_Stream_TypeDef *dma_rx;
    DMA_Stream_TypeDef *dma_tx;
    uint32_t            ch_rx;          /* DMA_CHANNEL_x raw value for SxCR */
    uint32_t            ch_tx;
    IRQn_Type           irq_rx;
    IRQn_Type           irq_tx;

    /* Pre-computed flag registers/masks for zero-cost ISR */
    volatile uint32_t  *rx_isr;         /* &DMAx->LISR or HISR */
    volatile uint32_t  *rx_ifcr;        /* &DMAx->LIFCR or HIFCR */
    uint32_t            rx_te_mask;     /* TEIF bit for this RX stream */
    uint32_t            rx_all_mask;    /* all clearable bits for RX stream */
    volatile uint32_t  *tx_ifcr;
    uint32_t            tx_all_mask;

    struct rt_completion cpt;
    volatile uint8_t    error;
} spi_lld_bus_t;

typedef struct {
    volatile uint32_t init_count;
    volatile uint32_t xfer_count;
    volatile uint32_t rx_irq_count;
    volatile uint32_t tx_irq_count;
    volatile uint32_t dma_timeout_count;
    volatile uint32_t dma_error_count;
    volatile uint32_t bsy_timeout_count;
    volatile uint32_t last_len;
    volatile uint32_t last_sr;
    volatile uint32_t last_cr2;
    volatile uint32_t last_error;
} spi_lld_debug_stats_t;

#define SPI_LLD_DEBUG_ERR_NONE         0U
#define SPI_LLD_DEBUG_ERR_DMA_TIMEOUT  1U
#define SPI_LLD_DEBUG_ERR_DMA_ERROR    2U
#define SPI_LLD_DEBUG_ERR_BSY_TIMEOUT  3U

/*
 * Initialise one LLD bus context and enable NVIC vectors.
 * Must be called after HAL_SPI_Init() for the same bus so the SPI peripheral
 * is already configured (mode, data size, baud rate, etc.).
 */
rt_err_t spi_lld_bus_init(spi_lld_bus_t *lld);

/*
 * Full-duplex DMA transfer (send+receive simultaneously).
 * Both buf_tx and buf_rx must be 32-byte aligned; D-Cache flush/invalidate is
 * the caller's responsibility (same requirement as the HAL DMA path).
 * Returns RT_EOK on success, negative error code on timeout/DMA error.
 */
rt_err_t spi_lld_xfer(spi_lld_bus_t *lld,
                       const uint8_t *buf_tx, uint8_t *buf_rx, uint16_t len);

/*
 * ISR entry points — call from the concrete DMA stream IRQ handlers.
 */
void spi_lld_dma_rx_irq(spi_lld_bus_t *lld);
void spi_lld_dma_tx_irq(spi_lld_bus_t *lld);

/*
 * Lookup a registered LLD bus by SPI peripheral pointer.
 * Returns NULL if no LLD context has been registered for that bus.
 */
spi_lld_bus_t *spi_lld_lookup(const SPI_TypeDef *spi);

/*
 * Register an LLD context (called once per bus, typically from board init).
 */
void spi_lld_register(spi_lld_bus_t *lld);

extern spi_lld_debug_stats_t g_spi1_lld_stats;
extern spi_lld_debug_stats_t g_spi4_lld_stats;

#ifdef __cplusplus
}
#endif

#endif /* SOC_SERIES_STM32F7 */
