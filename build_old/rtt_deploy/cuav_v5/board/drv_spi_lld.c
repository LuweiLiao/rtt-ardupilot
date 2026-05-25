/*
 * drv_spi_lld.c — STM32F7 SPI Low-Level DMA driver
 *
 * Root cause fixed
 * ----------------
 * STM32 HAL SPI DMA completion (SPI_DMATransmitReceiveCplt) calls
 * SPI_EndRxTxTransaction() which busy-waits on FTLVL and BSY flags inside the
 * DMA IRQ handler.  At low SPI speeds (e.g. 1 MHz IMU probe) this busy-wait
 * runs for hundreds of microseconds, blocking the USB OTG interrupt (priority 6)
 * and causing MAVLink TX stalls that eventually trigger a USB CDC disconnect.
 *
 * This driver:
 *   DMA RX IRQ: disable streams, clear DMA flags, signal completion object.
 *               Total ISR path < 10 instructions; USB OTG is never blocked.
 *   Thread:     wait completion, then poll BSY with rt_thread_yield().
 *               BSY is almost always already clear when we get here.
 *
 * Build note
 * ----------
 * Only compiled for SOC_SERIES_STM32F7.
 * Enabled per-bus by calling spi_lld_register(&bus_lld) from board init.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "board.h"

#ifdef SOC_SERIES_STM32F7

#include "drv_spi_lld.h"
#include "stm32f7xx.h"
#include "stm32f7xx_ll_dma.h"
#include "stm32f7xx_ll_spi.h"

#define LOG_TAG "spi.lld"
#include <drv_log.h>

/* ─────────────────────────────────────── LLD registry ──────────────── */

#define SPI_LLD_MAX_BUSES  4
static spi_lld_bus_t *s_buses[SPI_LLD_MAX_BUSES];
static uint32_t       s_bus_count;

spi_lld_debug_stats_t g_spi1_lld_stats = {};
spi_lld_debug_stats_t g_spi4_lld_stats = {};
volatile uint32_t g_spi1_clock_restores = 0; /* counts how many times SPI1EN was re-enabled */

static inline spi_lld_debug_stats_t *_stats_for_spi(const SPI_TypeDef *spi)
{
    if (spi == SPI1) {
        return &g_spi1_lld_stats;
    }
    if (spi == SPI4) {
        return &g_spi4_lld_stats;
    }
    return RT_NULL;
}

void spi_lld_register(spi_lld_bus_t *lld)
{
    RT_ASSERT(lld != RT_NULL);
    if (s_bus_count < SPI_LLD_MAX_BUSES)
    {
        s_buses[s_bus_count++] = lld;
    }
}

spi_lld_bus_t *spi_lld_lookup(const SPI_TypeDef *spi)
{
    for (uint32_t i = 0; i < s_bus_count; i++)
    {
        if (s_buses[i]->spi == spi) return s_buses[i];
    }
    return RT_NULL;
}

/* ─────────────────────────────────────── flag helpers ─────────────── */

/*
 * Pre-compute ISR/IFCR pointers and bit masks for a DMA stream.
 * STM32F7 DMA register map (RM0410 §8.5.3):
 *   DMAx base   → LISR [0], HISR [4], LIFCR [8], HIFCR [12]
 *   DMAx_Stream0 = DMAx_BASE + 0x10, stride 0x18 (24 bytes each)
 *
 * Flag bit layout per stream within LISR/HISR:
 *   Stream 0 → [5:0],  Stream 1 → [11:6],
 *   Stream 2 → [21:16],Stream 3 → [27:22]   (in LISR)
 *   Stream 4 → [5:0],  Stream 5 → [11:6],
 *   Stream 6 → [21:16],Stream 7 → [27:22]   (in HISR)
 *
 * Within the 6-bit group: FEIF(0), rsvd(1), DMEIF(2), TEIF(3), HTIF(4), TCIF(5)
 */
static void _stream_precompute(DMA_TypeDef *dma,
                                DMA_Stream_TypeDef *stream,
                                volatile uint32_t **p_isr,    /* may be NULL */
                                volatile uint32_t **p_ifcr,
                                uint32_t *p_te_mask,          /* may be NULL */
                                uint32_t *p_all_mask)
{
    /* Stream index 0-7 relative to DMAx.
     * DMAx register layout: LISR[0], HISR[4], LIFCR[8], HIFCR[12]
     * Stream0 starts at DMAx+0x10, stride = sizeof(DMA_Stream_TypeDef) = 24 bytes. */
    uint32_t idx = ((uint32_t)stream - ((uint32_t)dma + 0x10U)) / sizeof(DMA_Stream_TypeDef);

    volatile uint32_t *isr_reg;
    volatile uint32_t *ifcr_reg;

    /* LISR/LIFCR for streams 0-3, HISR/HIFCR for streams 4-7 */
    if (idx < 4)
    {
        isr_reg  = &dma->LISR;
        ifcr_reg = &dma->LIFCR;
    }
    else
    {
        isr_reg  = &dma->HISR;
        ifcr_reg = &dma->HIFCR;
        idx -= 4;
    }

    if (p_isr)  *p_isr  = isr_reg;
    if (p_ifcr) *p_ifcr = ifcr_reg;

    /* Bit shift for this stream within the LISR/HISR half */
    static const uint32_t shifts[4] = {0, 6, 16, 22};
    uint32_t sh = shifts[idx & 3];  /* & 3 for safety */

    if (p_te_mask)  *p_te_mask  = (0x08UL << sh); /* TEIF bit */
    if (p_all_mask) *p_all_mask = (0x3DUL << sh); /* FEIF|DMEIF|TEIF|HTIF|TCIF */
}

/* ─────────────────────────────────────── DMA stream helpers ─────── */

/* Disable a DMA stream and wait until EN clears (HW must finish current burst) */
static void _disable_stream_wait(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    /* HW clears EN after the current word completes; a few cycles max */
    uint32_t timeout = 10000U;
    while ((s->CR & DMA_SxCR_EN) && --timeout) { __NOP(); }
}

/* ─────────────────────────────────────── bus init ─────────────────── */

rt_err_t spi_lld_bus_init(spi_lld_bus_t *lld)
{
    RT_ASSERT(lld && lld->spi && lld->dma_rx && lld->dma_tx);

    spi_lld_debug_stats_t *stats = _stats_for_spi(lld->spi);
    if (stats != RT_NULL) {
        stats->init_count++;
        stats->last_sr = lld->spi->SR;
        stats->last_cr2 = lld->spi->CR2;
        stats->last_error = SPI_LLD_DEBUG_ERR_NONE;
    }

    rt_completion_init(&lld->cpt);
    lld->error = 0;

    /* Determine which DMA controller owns each stream */
    DMA_TypeDef *dma_rx_ctrl = ((uint32_t)lld->dma_rx < (uint32_t)DMA2_Stream0) ? DMA1 : DMA2;
    DMA_TypeDef *dma_tx_ctrl = ((uint32_t)lld->dma_tx < (uint32_t)DMA2_Stream0) ? DMA1 : DMA2;

    _stream_precompute(dma_rx_ctrl, lld->dma_rx,
                       &lld->rx_isr, &lld->rx_ifcr,
                       &lld->rx_te_mask, &lld->rx_all_mask);
    _stream_precompute(dma_tx_ctrl, lld->dma_tx,
                       NULL, &lld->tx_ifcr,
                       NULL, &lld->tx_all_mask);

    /*
     * DO NOT enable NVIC here.
     * The DMA interrupts are enabled by the HAL path in stm32_spi_init()
     * (called from spi_configure() -> configure()) which runs AFTER
     * rt_hw_spi_bus_init() has assigned spi_bus_obj[x].lld.
     * Enabling NVIC here (before lld is assigned to spi_bus_obj) would
     * allow stale DMA interrupts to fire and hit the HAL path with an
     * uninitialized handle, causing a HardFault.
     *
     * Ensure DMA streams are stopped and flags cleared to prevent any
     * stale interrupt from firing when NVIC is eventually enabled.
     */
    _disable_stream_wait(lld->dma_rx);
    _disable_stream_wait(lld->dma_tx);
    if (lld->rx_ifcr) *lld->rx_ifcr = lld->rx_all_mask;
    if (lld->tx_ifcr) *lld->tx_ifcr = lld->tx_all_mask;

    LOG_I("spi_lld bus init ok: spi=%p rx_stream=%p tx_stream=%p", lld->spi, lld->dma_rx, lld->dma_tx);
    return RT_EOK;
}

/* ─────────────────────────────────────── DMA program ─────────── */

static void _setup_rx_stream(spi_lld_bus_t *lld, uint8_t *buf_rx, uint16_t len)
{
    DMA_Stream_TypeDef *s = lld->dma_rx;
    _disable_stream_wait(s);
    *lld->rx_ifcr = lld->rx_all_mask;     /* clear all RX flags */

    s->PAR  = (uint32_t)&lld->spi->DR;
    s->M0AR = (uint32_t)buf_rx;
    s->NDTR = len;
    s->FCR  = 0;                           /* direct mode */
    /* Direction: PERIPH→MEM (bits [7:6] = 00), MINC, TCIE, TEIE, channel */
    s->CR = lld->ch_rx
           | DMA_SxCR_MINC
           | DMA_SxCR_TCIE
           | DMA_SxCR_TEIE;
    s->CR |= DMA_SxCR_EN;
}

static void _setup_tx_stream(spi_lld_bus_t *lld, const uint8_t *buf_tx, uint16_t len)
{
    DMA_Stream_TypeDef *s = lld->dma_tx;
    _disable_stream_wait(s);
    *lld->tx_ifcr = lld->tx_all_mask;     /* clear all TX flags */

    s->PAR  = (uint32_t)&lld->spi->DR;
    s->M0AR = (uint32_t)buf_tx;
    s->NDTR = len;
    s->FCR  = 0;
    /* Direction: MEM→PERIPH (bit 6 = DIR_0), MINC, TCIE, TEIE, channel */
    s->CR = lld->ch_tx
           | DMA_SxCR_MINC
           | DMA_SxCR_TCIE
           | DMA_SxCR_TEIE
           | DMA_SxCR_DIR_0;
    s->CR |= DMA_SxCR_EN;
}

/* ─────────────────────────────────────── public API ────────────────  */

rt_err_t spi_lld_xfer(spi_lld_bus_t *lld,
                       const uint8_t *buf_tx, uint8_t *buf_rx, uint16_t len)
{
    RT_ASSERT(lld != RT_NULL && len > 0);

    SPI_TypeDef *spi = lld->spi;
    spi_lld_debug_stats_t *stats = _stats_for_spi(spi);

    /* Re-enable peripheral clock if disabled.  On some RT-Thread /
     * STM32F7 configurations the SPI1 clock (APB2ENR bit 12) gets
     * cleared after the initial IMU probe completes, preventing any
     * further SPI1 access until the clock is re-enabled here.
     * Using |= is a no-op when the bit is already set. */
    if (spi == SPI1) {
        if (!(RCC->APB2ENR & RCC_APB2ENR_SPI1EN)) g_spi1_clock_restores++;
        RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    }
    else if (spi == SPI2) RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    else if (spi == SPI3) RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    else if (spi == SPI4) RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
    else if (spi == SPI5) RCC->APB2ENR |= RCC_APB2ENR_SPI5EN;

    if (stats != RT_NULL) {
        stats->xfer_count++;
        stats->last_len = len;
        stats->last_sr = spi->SR;
        stats->last_cr2 = spi->CR2;
        stats->last_error = SPI_LLD_DEBUG_ERR_NONE;
    }
    lld->error = 0;

    /* Re-initialize completion before each transfer to prevent stale
     * wakeup from a previous timeout/aborted transfer. */
    rt_completion_init(&lld->cpt);

    /* Flush any stale SPI FIFO data */
    (void)spi->DR;
    (void)spi->SR;

    /* Setup RX first to avoid losing the first incoming byte */
    _setup_rx_stream(lld, buf_rx, len);
    _setup_tx_stream(lld, buf_tx, len);

    /* Enable SPI peripheral and DMA requests (TX and RX together to start the clock).
     * HAL_SPI_Init() does NOT set SPE — only the HAL transfer functions do.
     * The LLD bypasses those, so we must ensure SPE is set here.
     * Also ensure FRXTH=1 (RX FIFO threshold at 1/4 = 1 byte) so DMA requests
     * fire per-byte rather than waiting for 2 bytes.  The drv_spi.c configure()
     * path sets this, but HAL_SPI_Init() may clear it via MODIFY_REG. */
    SET_BIT(spi->CR2, SPI_CR2_FRXTH);
    SET_BIT(spi->CR1, SPI_CR1_SPE);
    SET_BIT(spi->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);

    /*
     * WORKAROUND: On some STM32F7 configurations, the RX DMA stream never
     * fires its transfer-complete interrupt even though the TX DMA completes
     * and the RX FIFO contains data (FRLVL > 0).  Root cause unknown.
     * Poll the RX FIFO level and manually drain data if DMA stalls.
     */
    {
        const rt_tick_t t0 = rt_tick_get();
        while (rt_tick_get() - t0 < rt_tick_from_millisecond(50))
        {
            /* Check if RX DMA completed (NDTR reached 0) */
            if ((lld->dma_rx->CR & DMA_SxCR_EN) == 0 || lld->dma_rx->NDTR == 0)
                goto dma_rx_ok;

            /* If TX DMA completed and RX FIFO has all expected data, drain it manually */
            if ((lld->dma_tx->CR & DMA_SxCR_EN) == 0)
            {
                uint32_t rx_level = (spi->SR >> 9) & 0x3;  /* FRLVL[1:0] */
                if (rx_level > 0 && lld->dma_rx->NDTR == len)
                {
                    /* DMA RX never started — drain FIFO manually */
                    for (uint16_t i = 0; i < len; i++)
                    {
                        while (!(spi->SR & SPI_SR_RXNE)) { __NOP(); }
                        buf_rx[i] = (uint8_t)spi->DR;
                    }
                    /* Disable streams, clear flags */
                    _disable_stream_wait(lld->dma_rx);
                    _disable_stream_wait(lld->dma_tx);
                    *lld->rx_ifcr = lld->rx_all_mask;
                    *lld->tx_ifcr = lld->tx_all_mask;
                    CLEAR_BIT(spi->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
                    CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
                    if (stats != RT_NULL) stats->last_error = SPI_LLD_DEBUG_ERR_NONE;
                    return RT_EOK;
                }
            }
            rt_thread_yield();
        }
    }
dma_rx_ok:
    ;

    /* Determine DMA controller for this bus (for ISR/IFCR register dump) */
    DMA_TypeDef *dma_rx_ctrl = ((uint32_t)lld->dma_rx < (uint32_t)DMA2_Stream0) ? DMA1 : DMA2;
    (void)dma_rx_ctrl; /* used only in debug prints below */

    /* Block caller thread until RX-done ISR fires */
    rt_err_t err = rt_completion_wait(&lld->cpt, rt_tick_from_millisecond(100));
    if (err != RT_EOK)
    {
        if (stats != RT_NULL) {
            stats->dma_timeout_count++;
            stats->last_sr = spi->SR;
            stats->last_cr2 = spi->CR2;
            stats->last_error = SPI_LLD_DEBUG_ERR_DMA_TIMEOUT;
        }
        /* Dump hardware state for root-cause analysis */
        rt_kprintf("[SPI-LLD-TIMEOUT] spi=%p len=%u\n", spi, (unsigned)len);
        rt_kprintf("  SPI CR1=0x%08x CR2=0x%08x SR=0x%08x\n",
                   (unsigned)spi->CR1, (unsigned)spi->CR2, (unsigned)spi->SR);
        rt_kprintf("  RX stream=%p CR=0x%08x NDTR=%u M0AR=0x%08x PAR=0x%08x\n",
                   lld->dma_rx,
                   (unsigned)lld->dma_rx->CR, (unsigned)lld->dma_rx->NDTR,
                   (unsigned)lld->dma_rx->M0AR, (unsigned)lld->dma_rx->PAR);
        rt_kprintf("  TX stream=%p CR=0x%08x NDTR=%u M0AR=0x%08x\n",
                   lld->dma_tx,
                   (unsigned)lld->dma_tx->CR, (unsigned)lld->dma_tx->NDTR,
                   (unsigned)lld->dma_tx->M0AR);
        rt_kprintf("  DMA2 LISR=0x%08x HISR=0x%08x\n",
                   (unsigned)DMA2->LISR, (unsigned)DMA2->HISR);
        rt_kprintf("  rx_isr=0x%08x rx_te_mask=0x%08x rx_all_mask=0x%08x\n",
                   lld->rx_isr ? (unsigned)*lld->rx_isr : 0u,
                   (unsigned)lld->rx_te_mask, (unsigned)lld->rx_all_mask);
        /* Check NVIC state for RX DMA IRQ */
        {
            IRQn_Type irq = lld->irq_rx;
            uint32_t iser_idx = ((uint32_t)irq) >> 5;
            uint32_t iser_bit = ((uint32_t)irq) & 0x1FU;
            uint32_t iser_val = NVIC->ISER[iser_idx];
            uint32_t ispr_val = NVIC->ISPR[iser_idx];
            rt_kprintf("  NVIC: irq_rx=%d ISER[%u]=0x%08x (bit%u=%u) ISPR[%u]=0x%08x\n",
                       (int)irq, iser_idx, iser_val, iser_bit,
                       (iser_val >> iser_bit) & 1u,
                       iser_idx, ispr_val);
        }
        LOG_E("spi_lld_xfer: DMA timeout (len=%u)", len);
        _disable_stream_wait(lld->dma_rx);
        _disable_stream_wait(lld->dma_tx);
        CLEAR_BIT(spi->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
        /* Reset SPI */
        CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
        SET_BIT(spi->CR1, SPI_CR1_SPE);
        return -RT_ETIMEOUT;
    }

    if (lld->error)
    {
        if (stats != RT_NULL) {
            stats->dma_error_count++;
            stats->last_sr = spi->SR;
            stats->last_cr2 = spi->CR2;
            stats->last_error = SPI_LLD_DEBUG_ERR_DMA_ERROR;
        }
        LOG_E("spi_lld_xfer: DMA error");
        return -RT_EIO;
    }

    /*
     * Wait for BSY to clear in thread context.
     * DMA TC fires after the last byte is pushed into the SPI TX FIFO.
     * The FIFO empties and BSY clears within a few SPI clock cycles.
     * At 10 MHz SPI, one byte = 800 ns → BSY clears within ~1 µs.
     * rt_thread_yield() is called as a precaution but almost never needed.
     */
    const rt_tick_t t0 = rt_tick_get();
    while (spi->SR & SPI_SR_BSY)
    {
        if ((rt_tick_get() - t0) > rt_tick_from_millisecond(5))
        {
            if (stats != RT_NULL) {
                stats->bsy_timeout_count++;
                stats->last_sr = spi->SR;
                stats->last_cr2 = spi->CR2;
                stats->last_error = SPI_LLD_DEBUG_ERR_BSY_TIMEOUT;
            }
            LOG_E("spi_lld_xfer: BSY timeout");
            break;
        }
        rt_thread_yield();
    }

    return RT_EOK;
}

/* ─────────────────────────────────────── ISR handlers ─────────────── */

/*
 * Call from DMA RX IRQ handler.
 * This is the hot path: must be < 10 instructions before completion_done().
 */
void spi_lld_dma_rx_irq(spi_lld_bus_t *lld)
{
    SPI_TypeDef *spi = lld->spi;
    spi_lld_debug_stats_t *stats = _stats_for_spi(spi);

    if (stats != RT_NULL) {
        stats->rx_irq_count++;
        stats->last_sr = spi->SR;
        stats->last_cr2 = spi->CR2;
    }

    /* Check for DMA transfer error */
    if (*lld->rx_isr & lld->rx_te_mask)
    {
        lld->error = 1;
        if (stats != RT_NULL) {
            stats->last_error = SPI_LLD_DEBUG_ERR_DMA_ERROR;
        }
    }

    /* Disable DMA streams (no yield — in ISR) */
    lld->dma_rx->CR &= ~DMA_SxCR_EN;
    lld->dma_tx->CR &= ~DMA_SxCR_EN;

    /* Disable SPI DMA requests */
    CLEAR_BIT(spi->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);

    /* Clear all DMA flags */
    *lld->rx_ifcr = lld->rx_all_mask;
    *lld->tx_ifcr = lld->tx_all_mask;

    /* Wake the waiting thread */
    rt_completion_done(&lld->cpt);
}

/*
 * Call from DMA TX IRQ handler.
 * TX done fires first (TX FIFO not yet empty when DMA ends); we don't use
 * this for synchronization — just clear flags and return.
 */
void spi_lld_dma_tx_irq(spi_lld_bus_t *lld)
{
    spi_lld_debug_stats_t *stats = _stats_for_spi(lld->spi);
    if (stats != RT_NULL) {
        stats->tx_irq_count++;
        stats->last_sr = lld->spi->SR;
        stats->last_cr2 = lld->spi->CR2;
    }
    *lld->tx_ifcr = lld->tx_all_mask;
}

#endif /* SOC_SERIES_STM32F7 */
