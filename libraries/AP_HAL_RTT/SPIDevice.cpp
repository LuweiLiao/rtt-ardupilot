/*
 * AP_HAL_RTT — SPI device driver
 * Board-independent: uses RTT_SPIDesc from hwdef-generated HAL_SPI_DEVICE_LIST.
 */

#include "SPIDevice.h"
#include "rtt_spi_dma_irq.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <rtthread.h>
#include <drivers/dev_spi.h>
#include "Scheduler.h"  /* APM_RTT_SPI_PRIORITY for DeviceBus thread */

#ifdef SOC_SERIES_STM32F7
#include <stm32f7xx.h>
#include "drv_spi_lld.h"

/* CMSIS polling path (_dev == nullptr): serialize bus access without masking
 * global IRQs. Long __disable_irq() sections during SPI polls can starve USB
 * OTG IRQ service while MAVLink parameter traffic is active. */
#define RTT_CMSIS_SPI_BUS_MAX 8U
static struct rt_mutex _cmsis_spi_bus_mtx[RTT_CMSIS_SPI_BUS_MAX];
static bool _cmsis_spi_bus_mtx_inited[RTT_CMSIS_SPI_BUS_MAX];

static void _cmsis_spi_bus_mtx_ensure(uint8_t bus)
{
    if (bus >= RTT_CMSIS_SPI_BUS_MAX || _cmsis_spi_bus_mtx_inited[bus]) {
        return;
    }
    char name[12];
    rt_snprintf(name, sizeof(name), "spib%u", (unsigned)bus);
    rt_mutex_init(&_cmsis_spi_bus_mtx[bus], name, RT_IPC_FLAG_PRIO);
    _cmsis_spi_bus_mtx_inited[bus] = true;
}

/* STM32F7 SPI1 GPIO pin configuration (register-level).
 * Called once, then guarded by _spi1_gpio_init_done.  The GPIO MODER/AFR
 * for MISO/MOSI may be clobbered by other peripheral init (e.g. USART6 on PA6
 * on some boards) so we restore on first transfer only — repeated init
 * creates glitches that confuse IMU slaves during CS-held burst reads.
 *
 * Pinout (CUAV V5, actual hardware from ChibiOS fmuv5/CUAVv5 hwdef):
 *   PG11=SCK(AF5), PA6=MISO(AF5), PD7=MOSI(AF5)
 *   PF11=ICM42688_CS */
static bool _spi1_gpio_init_done = false;
static void _spi1_gpio_init(void)
{
    if (_spi1_gpio_init_done) return;

    /* Enable GPIO clocks — GPIOA (PA6 MISO), GPIOD (PD7 MOSI), GPIOF (PF11 CS), GPIOG (PG11 SCK) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOFEN |
                    RCC_AHB1ENR_GPIOGEN;
    (void)RCC->AHB1ENR;
    /* Ensure SPI1 peripheral clock is enabled */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    /* PG11 SCK: MODE=AF(10), AF=AF5(0101) */
    GPIOG->MODER = (GPIOG->MODER & ~(3U << 22)) | (2U << 22);
    GPIOG->AFR[1] = (GPIOG->AFR[1] & ~(0xFU << 12)) | (5U << 12);

    /* PA6 MISO: MODE=AF(10), AF=AF5(0101) */
    GPIOA->MODER = (GPIOA->MODER & ~(3U << 12)) | (2U << 12);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFU << 24)) | (5U << 24);

    /* PD7 MOSI: MODE=AF(10), AF=AF5(0101) */
    GPIOD->MODER = (GPIOD->MODER & ~(3U << 14)) | (2U << 14);
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~(0xFU << 28)) | (5U << 28);

    /* CS pin: PF11 = ICM42688_CS, OUTPUT, INITIAL STATE HIGH */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 22)) | (1U << 22);  /* PF11 OUT */
    GPIOF->BSRR = (1U << 11);                                     /* set PF11 HIGH */

    /* CS pin: PF2 = ICM20689_CS, OUTPUT, INITIAL STATE HIGH */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 4)) | (1U << 4);    /* PF2 OUT */
    GPIOF->BSRR = (1U << 2);                                     /* set PF2 HIGH */

    _spi1_gpio_init_done = true;
}

/* STM32F7 SPI4 GPIO pin configuration (register-level).
 * Used for MS5611 barometer (and optionally external SPI devices).
 * Called once, then guarded by _spi4_gpio_init_done.
 *
 * Pinout (CUAV V5, from hwdef.dat):
 *   PE2=SCK(AF5), PE13=MISO(AF5), PE6=MOSI(AF5)
 *   PF10=MS5611_CS */
static bool _spi4_gpio_init_done = false;
static void _spi4_gpio_init(void)
{
    if (_spi4_gpio_init_done) return;

    /* Enable GPIO clocks for PORTE and PORTF */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;
    /* Ensure SPI4 peripheral clock is enabled (APB2) */
    RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
    (void)RCC->APB2ENR;

    /* PE2 SPI4_SCK: MODE=AF(10), AF=AF5(0101) */
    GPIOE->MODER = (GPIOE->MODER & ~(3U << 4)) | (2U << 4);
    GPIOE->AFR[0] = (GPIOE->AFR[0] & ~(0xFU << 8)) | (5U << 8);

    /* PE13 SPI4_MISO: MODE=AF(10), AF=AF5(0101) */
    GPIOE->MODER = (GPIOE->MODER & ~(3U << 26)) | (2U << 26);
    GPIOE->AFR[1] = (GPIOE->AFR[1] & ~(0xFU << 20)) | (5U << 20);

    /* PE6 SPI4_MOSI: MODE=AF(10), AF=AF5(0101) */
    GPIOE->MODER = (GPIOE->MODER & ~(3U << 12)) | (2U << 12);
    GPIOE->AFR[0] = (GPIOE->AFR[0] & ~(0xFU << 24)) | (5U << 24);

    /* PF10 MS5611_CS: OUTPUT, INITIAL STATE HIGH */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 20)) | (1U << 20);
    GPIOF->BSRR = (1U << 10);  /* set PF10 HIGH */

    _spi4_gpio_init_done = true;
}

/* STM32F7 SPI2 GPIO pin configuration (register-level).
 * Used for FM25V02 FRAM (ramtron) on CUAV V5 / fmuv5-class boards.
 *
 * Pinout (CUAV V5, from hwdef.dat / ChibiOS fmuv5):
 *   PI1=SCK(AF5), PI2=MISO(AF5), PI3=MOSI(AF5)
 *   PF5=RAMTRON_CS */
static bool _spi2_gpio_init_done = false;
static void _spi2_gpio_init(void)
{
    if (_spi2_gpio_init_done) {
        return;
    }

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOIEN | RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    (void)RCC->APB1ENR;

    /* PI1 SPI2_SCK: MODE=AF(10), AF=AF5 */
    GPIOI->MODER = (GPIOI->MODER & ~(3U << 2)) | (2U << 2);
    GPIOI->AFR[0] = (GPIOI->AFR[0] & ~(0xFU << 4)) | (5U << 4);

    /* PI2 SPI2_MISO */
    GPIOI->MODER = (GPIOI->MODER & ~(3U << 4)) | (2U << 4);
    GPIOI->AFR[0] = (GPIOI->AFR[0] & ~(0xFU << 8)) | (5U << 8);

    /* PI3 SPI2_MOSI */
    GPIOI->MODER = (GPIOI->MODER & ~(3U << 6)) | (2U << 6);
    GPIOI->AFR[0] = (GPIOI->AFR[0] & ~(0xFU << 12)) | (5U << 12);

    /* PF5 RAMTRON_CS: OUTPUT, INITIAL STATE HIGH */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 10)) | (1U << 10);
    GPIOF->BSRR = (1U << 5);

    _spi2_gpio_init_done = true;
}

void RTT::spi_cmsis_prepare_bus(uint8_t bus)
{
#ifdef SOC_SERIES_STM32F7
    _cmsis_spi_bus_mtx_ensure(bus);
    if (bus == 2) {
        _spi2_gpio_init();
    }
#else
    (void)bus;
#endif
}

/* ─────────────────────────────────────── SPI DMA transfer mode ─── */

/* DMA stream mapping for register-level SPI buses.
 * SPI1: RX=DMA2_Stream2 CH3, TX=DMA2_Stream5 CH3
 * SPI4: RX=DMA2_Stream0 CH4, TX=DMA2_Stream1 CH4
 *
 * ChibiOS reference (spi_lld_exchange, hal_spi_lld.c:597-626):
 *   Configures RX + TX DMA streams with MINC, TCIE, TEIE, enables both.
 *   Completion signaled via TCIF ISR → _spi_isr_code().
 *
 * RTT approach (Fix#4-A-low, RTT_SPI_DMA_IRQ_WAIT=1): RX DMA TC IRQ wakes
 *   SPI thread via rt_completion_wait; fallback to EN poll on timeout.
 *   Small transfers (≤ DMA_THRESHOLD bytes) use register polling
 *   to avoid DMA setup overhead.
 */
#define SPI_DMA_THRESHOLD   8

#ifndef RTT_SPI_DMA_IRQ_WAIT
#define RTT_SPI_DMA_IRQ_WAIT 0  /* reverted: IRQ-wait caused USB CDC disconnect on serial open */
#endif

struct spi_dma_desc {
    DMA_Stream_TypeDef *rx_stream;
    uint32_t            ch_rx;   /* channel number 0-7 */
    DMA_Stream_TypeDef *tx_stream;
    uint32_t            ch_tx;
};

static void _dma_stream_disable(DMA_Stream_TypeDef *s);
static void _spi_dma_abort(const struct spi_dma_desc *dma, SPI_TypeDef *spi);

#if RTT_SPI_DMA_IRQ_WAIT
#include <ipc/completion.h>

/* Fix#4-A-low: IRQ-driven DMA completion (replaces CPU spin on EN). */
volatile uint32_t rtt_dbg_spi_dma_irq_fallback_count = 0;

struct spi_dma_wait_ctx {
    struct rt_completion  cpt;
    volatile uint8_t    waiting;
    volatile uint32_t  *isr_reg;
    volatile uint32_t  *ifcr_reg;
    uint32_t            tcif_mask;
    uint32_t            ctcif_mask;
    IRQn_Type             irqn;
    bool                  nvic_on;
};

static spi_dma_wait_ctx _spi_dma_wait[RTT_CMSIS_SPI_BUS_MAX];

static void _spi_dma_wait_stream_flags(DMA_Stream_TypeDef *stream,
                                       volatile uint32_t **isr,
                                       volatile uint32_t **ifcr,
                                       uint32_t *tcif,
                                       uint32_t *ctcif)
{
    static const uint32_t lifcr_ctcif[] = {
        DMA_LIFCR_CTCIF0, DMA_LIFCR_CTCIF1,
        DMA_LIFCR_CTCIF2, DMA_LIFCR_CTCIF3,
    };
    static const uint32_t hifcr_ctcif[] = {
        DMA_HIFCR_CTCIF4, DMA_HIFCR_CTCIF5,
        DMA_HIFCR_CTCIF6, DMA_HIFCR_CTCIF7,
    };
    static const uint32_t lisr_tcif[] = {
        DMA_LISR_TCIF0, DMA_LISR_TCIF1,
        DMA_LISR_TCIF2, DMA_LISR_TCIF3,
    };
    static const uint32_t hisr_tcif[] = {
        DMA_HISR_TCIF4, DMA_HISR_TCIF5,
        DMA_HISR_TCIF6, DMA_HISR_TCIF7,
    };
    const uint32_t idx = ((uint32_t)stream - (uint32_t)DMA2) / 0x18U;
    if (idx < 4U) {
        *isr = &DMA2->LISR;
        *ifcr = &DMA2->LIFCR;
        *tcif = lisr_tcif[idx];
        *ctcif = lifcr_ctcif[idx];
    } else {
        *isr = &DMA2->HISR;
        *ifcr = &DMA2->HIFCR;
        *tcif = hisr_tcif[idx - 4U];
        *ctcif = hifcr_ctcif[idx - 4U];
    }
}

static void _spi_dma_wait_init_bus(uint8_t bus, const struct spi_dma_desc *dma)
{
    if (bus >= RTT_CMSIS_SPI_BUS_MAX || dma->rx_stream == NULL) {
        return;
    }
    spi_dma_wait_ctx *ctx = &_spi_dma_wait[bus];
    if (ctx->isr_reg != NULL) {
        return;
    }
    rt_completion_init(&ctx->cpt);
    _spi_dma_wait_stream_flags(dma->rx_stream, &ctx->isr_reg, &ctx->ifcr_reg,
                               &ctx->tcif_mask, &ctx->ctcif_mask);
    if (bus == 1) {
        ctx->irqn = DMA2_Stream2_IRQn;
    } else if (bus == 4) {
        ctx->irqn = DMA2_Stream0_IRQn;
    } else {
        return;
    }
    NVIC_SetPriority(ctx->irqn, 5U);
    NVIC_EnableIRQ(ctx->irqn);
    ctx->nvic_on = true;
}

extern "C" int rtt_spi_cmsis_dma_rx_irq_handler(uint8_t bus)
{
    if (bus >= RTT_CMSIS_SPI_BUS_MAX) {
        return 0;
    }
    spi_dma_wait_ctx *ctx = &_spi_dma_wait[bus];
    if (ctx->isr_reg == NULL) {
        return 0;
    }
    if ((*ctx->isr_reg & ctx->tcif_mask) == 0) {
        return 0;
    }
    *ctx->ifcr_reg = ctx->ctcif_mask;
    if (ctx->waiting) {
        rt_completion_done(&ctx->cpt);
    }
    /* CMSIS register-DMA owns this stream; never fall through to HAL. */
    return 1;
}

/* Shared post-DMA success/error path after EN cleared or IRQ wake. */
static bool _spi_dma_xfer_finish(const struct spi_dma_desc *dma, SPI_TypeDef *spi)
{
    spi->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN);
    bool err = false;
    {
        static const uint32_t lisr_teif[] = {
            DMA_LISR_TEIF0, DMA_LISR_TEIF1,
            DMA_LISR_TEIF2, DMA_LISR_TEIF3,
        };
        static const uint32_t hisr_teif[] = {
            DMA_HISR_TEIF4, DMA_HISR_TEIF5,
            DMA_HISR_TEIF6, DMA_HISR_TEIF7,
        };
        uint32_t rx_idx = ((uint32_t)dma->rx_stream - (uint32_t)DMA2) / 0x18U;
        uint32_t tx_idx = ((uint32_t)dma->tx_stream - (uint32_t)DMA2) / 0x18U;
        if (rx_idx < 4U) {
            if (DMA2->LISR & lisr_teif[rx_idx]) { err = true; }
        } else {
            if (DMA2->HISR & hisr_teif[rx_idx - 4U]) { err = true; }
        }
        if (tx_idx < 4U) {
            if (DMA2->LISR & lisr_teif[tx_idx]) { err = true; }
        } else {
            if (DMA2->HISR & hisr_teif[tx_idx - 4U]) { err = true; }
        }
        if (err) {
            if (rx_idx < 4U) DMA2->LIFCR = lisr_teif[rx_idx];
            else             DMA2->HIFCR = hisr_teif[rx_idx - 4U];
            if (tx_idx < 4U) DMA2->LIFCR = lisr_teif[tx_idx];
            else             DMA2->HIFCR = hisr_teif[tx_idx - 4U];
        }
    }
    if (err) {
        _spi_dma_abort(dma, spi);
        return false;
    }
    uint32_t bsy = 10000;
    while ((spi->SR & SPI_SR_BSY) && --bsy) { __NOP(); }
    if (bsy == 0) {
        spi->CR1 &= ~SPI_CR1_SPE;
        (void)spi->SR;
        (void)spi->DR;
        spi->CR1 |= SPI_CR1_SPE;
        bsy = 10000;
        while ((spi->SR & SPI_SR_BSY) && --bsy) { __NOP(); }
    }
    return true;
}

static bool _spi_dma_poll_en_complete(const struct spi_dma_desc *dma,
                                      SPI_TypeDef *spi,
                                      uint32_t timeout)
{
    while (timeout--) {
        if (!(dma->rx_stream->CR & DMA_SxCR_EN) &&
            !(dma->tx_stream->CR & DMA_SxCR_EN)) {
            return _spi_dma_xfer_finish(dma, spi);
        }
        __NOP();
    }
    _spi_dma_abort(dma, spi);
    return false;
}
#endif /* RTT_SPI_DMA_IRQ_WAIT */

/* Dynamic BR for register-level SPI1 path.  Updated by set_speed().
 * ChibiOS reference: derive_freq_flag_bus(), SPIDevice.cpp:259-281.
 * SPI1 PCLK2 = 108MHz on STM32F767 @ 216MHz SYSCLK.
 * BR = divider exponent: actual_freq = PCLK2 / 2^(BR+1).
 * Target speeds from hwdef.dat: ICM20689 low=2MHz high=8MHz.
 *   BR=3 → /16 = 6.75MHz (high speed)
 *   BR=5 → /64 = 1.6875MHz (low speed, < 2MHz target)
 */
#ifndef SPI1_PCLK2_HZ
#define SPI1_PCLK2_HZ 108000000U
#endif
#ifndef SPI4_PCLK2_HZ
#define SPI4_PCLK2_HZ 108000000U
#endif
#ifndef SPI2_PCLK1_HZ
#define SPI2_PCLK1_HZ 54000000U
#endif

/* bus index = AP bus number (1-based) */
static const struct spi_dma_desc _spi_dma_tbl[] = {
    {NULL, 0, NULL, 0},                    /* bus 0 */
    {DMA2_Stream2, 3, DMA2_Stream5, 3},    /* bus 1 = SPI1 */
    {NULL, 0, NULL, 0},                    /* bus 2 = SPI2 (RTT framework) */
    {NULL, 0, NULL, 0},                    /* bus 3 = SPI3 (RTT framework) */
    {DMA2_Stream0, 4, DMA2_Stream1, 4},    /* bus 4 = SPI4 */
};

static bool _spi_dma_clock_ok = false;
static void _spi_dma_clock_init(void)
{
    if (_spi_dma_clock_ok) return;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    _spi_dma_clock_ok = true;
}

/* Disable a DMA stream and wait for EN to clear. */
static void _dma_stream_disable(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    uint32_t tout = 10000;
    while ((s->CR & DMA_SxCR_EN) && --tout) { __NOP(); }
}

static void _spi_dma_abort(const struct spi_dma_desc *dma, SPI_TypeDef *spi)
{
    if (dma->rx_stream) _dma_stream_disable(dma->rx_stream);
    if (dma->tx_stream) _dma_stream_disable(dma->tx_stream);
    spi->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN);
    __DSB();
}

/* Full-duplex DMA transfer for register-level SPI buses.
 *
 * @param spi   SPI peripheral register base
 * @param bus   AP bus number (1 = SPI1, 4 = SPI4)
 * @param send  TX data (nullptr = send 0xFF, but still requires valid memory for DMA)
 * @param recv  RX buffer (nullptr = discard received bytes)
 * @param len   number of bytes
 * @return      true on success
 *
 * ChibiOS reference: spi_lld_exchange() at hal_spi_lld.c:597-626
 *   dmaStreamSetMemory0 + dmaStreamSetTransactionSize + dmaStreamSetMode + dmaStreamEnable
 */
#define STM32F7_DTCM_START  0x20000000UL
#define STM32F7_DTCM_END    0x20020000UL

static bool _spi_buf_dma_safe(const void *ptr, uint32_t len)
{
    if (ptr == nullptr || len == 0) {
        return true;
    }
    const uint32_t start = (uint32_t)ptr;
    const uint32_t end = start + len - 1U;
    if (end < STM32F7_DTCM_START || start >= STM32F7_DTCM_END) {
        return true;
    }
    return false;
}

static void _spi_regs_configure(SPI_TypeDef *spi, uint32_t br)
{
    CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
               SPI_CR1_CPOL | SPI_CR1_CPHA | br;
    spi->CR2 = SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2 | SPI_CR2_FRXTH;
    SET_BIT(spi->CR1, SPI_CR1_SPE);
    while (spi->SR & SPI_SR_RXNE) { (void)*(volatile uint8_t *)&spi->DR; }
    (void)spi->SR;
}

static bool _spi_poll_small(SPI_TypeDef *spi,
                            const uint8_t *send, uint8_t *recv, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t tout = 100000;
        while (!(spi->SR & SPI_SR_TXE) && --tout) { __NOP(); }
        if (tout == 0) return false;
        *((__IO uint8_t *)&spi->DR) = send ? send[i] : 0xFF;
        tout = 100000;
        while (!(spi->SR & SPI_SR_RXNE) && --tout) { __NOP(); }
        if (tout == 0) return false;
        if (recv) recv[i] = *((__IO uint8_t *)&spi->DR);
        else (void)*((__IO uint8_t *)&spi->DR);
    }
    uint32_t tout = 10000;
    while ((spi->SR & SPI_SR_BSY) && --tout) { __NOP(); }
    return tout != 0;
}

/*
 * SPI1/SPI4 LLD path: drv_spi_lld IRQ completion (rt_completion_wait) instead
 * of CMSIS DMA EN busy-wait.  NVIC/DMA IRQ routing is owned by drv_spi.c.
 */
static bool _spi_lld_dma_xfer(RTT::DeviceBus *bus_dev, SPI_TypeDef *spi,
                               const uint8_t *send, uint8_t *recv,
                               uint32_t len, uint32_t br)
{
    spi_lld_bus_t *lld = spi_lld_lookup(spi);
    if (lld == RT_NULL || len == 0 || len > 65535U) {
        return false;
    }

    _spi_regs_configure(spi, br);

    if (len <= SPI_DMA_THRESHOLD) {
        return _spi_poll_small(spi, send, recv, len);
    }

    static const uint32_t _lld_dummy_tx[4] = {0xFFFFFFFF, 0xFFFFFFFF,
                                            0xFFFFFFFF, 0xFFFFFFFF};
    uint32_t rx_scratch = 0;
    uint8_t *rx_buf = recv ? recv : (uint8_t *)&rx_scratch;
    const uint8_t *tx_buf = send ? send : (const uint8_t *)_lld_dummy_tx;

    const uint8_t *tx_dma = tx_buf;
    uint8_t *rx_dma = rx_buf;
    bool bounced = false;

    if (!_spi_buf_dma_safe(tx_buf, len) || !_spi_buf_dma_safe(rx_buf, len)) {
        if (bus_dev == nullptr) {
            return false;
        }
        if (!bus_dev->bouncebuffer_setup(tx_dma, (uint16_t)len, rx_dma, (uint16_t)len)) {
            return false;
        }
        bounced = true;
        if (send != nullptr && tx_dma != send) {
            memcpy(const_cast<uint8_t *>(tx_dma), send, len);
        }
    }

    const rt_err_t err = spi_lld_xfer(lld, tx_dma, rx_dma, (uint16_t)len);

    if (bounced) {
        bus_dev->bouncebuffer_finish(tx_dma, rx_dma, (uint16_t)len);
    }

    return err == RT_EOK;
}

static bool _spi_dma_xfer(RTT::DeviceBus *bus_dev, SPI_TypeDef *spi, uint8_t bus,
                           const uint8_t *send, uint8_t *recv, uint32_t len,
                           uint32_t br)
{
    if (bus == 1) {
        spi_lld_bus_t *lld = spi_lld_lookup(SPI1);
        if (lld != RT_NULL) {
            return _spi_lld_dma_xfer(bus_dev, spi, send, recv, len, br);
        }
    }
    if (bus == 4) {
        spi_lld_bus_t *lld = spi_lld_lookup(SPI4);
        if (lld != RT_NULL) {
            return _spi_lld_dma_xfer(bus_dev, spi, send, recv, len, br);
        }
    }

    if (bus >= ARRAY_SIZE(_spi_dma_tbl)) return false;
    const struct spi_dma_desc *dma = &_spi_dma_tbl[bus];
    if (dma->rx_stream == NULL || dma->tx_stream == NULL) return false;

    /* Configure SPI CR1/CR2 and enable SPE — matches spi1_poll_transfer()
     * cs_take path and ChibiOS spiStart() convention (SPIDevice.cpp:432-437).
     * Full-duplex transfers skip spi1_poll_transfer() entirely so we must
     * ensure SPE=1 here; otherwise the peripheral is disabled and TXE/RXNE
     * never assert, hanging the probe forever.
     * [Cybernetics Ch.4] Closed-loop: verify CR1.SPE=1 via GDB before DMA. */
    _spi_regs_configure(spi, br);

    /* Small transfers: register polling avoids DMA setup latency */
    if (len <= SPI_DMA_THRESHOLD) {
        return _spi_poll_small(spi, send, recv, len);
    }

    _spi_dma_clock_init();

    /* Static dummy 0xFF buffer for receive-only DMA transfers.
     * When send == nullptr, the TX DMA must send 0xFF (not recv buffer
     * contents) so that the SPI MOSI line drives idle-high.  DMA needs
     * a valid memory address even if the data is a dummy pattern.
     * ChibiOS reference: the SPI LLD's dummytx buffer provides 0xFF. */
    static const uint32_t _dma_dummy_tx_16[4] = {0xFFFFFFFF, 0xFFFFFFFF,
                                                  0xFFFFFFFF, 0xFFFFFFFF};

    /* Prepare usable buffers — DMA needs valid memory addresses */
    uint32_t rx_scratch = 0;
    uint8_t *rx_buf = recv ? recv : (uint8_t *)&rx_scratch;
    const uint8_t *tx_buf = send ? send : (const uint8_t *)_dma_dummy_tx_16;

    /* ── Set up RX stream: PERIPH → MEM, 8-bit, increment MEM addr ── */
    _dma_stream_disable(dma->rx_stream);
    dma->rx_stream->PAR  = (uint32_t)&spi->DR;
    dma->rx_stream->M0AR = (uint32_t)rx_buf;
    dma->rx_stream->NDTR = len;
    dma->rx_stream->FCR  = 0;
    dma->rx_stream->CR   = (dma->ch_rx << DMA_SxCR_CHSEL_Pos)
                           | DMA_SxCR_MINC
                           | DMA_SxCR_TCIE
                           | DMA_SxCR_TEIE;

    /* ── Set up TX stream: MEM → PERIPH, 8-bit, increment MEM addr ── */
    _dma_stream_disable(dma->tx_stream);
    dma->tx_stream->PAR  = (uint32_t)&spi->DR;
    dma->tx_stream->M0AR = (uint32_t)tx_buf;
    dma->tx_stream->NDTR = len;
    dma->tx_stream->FCR  = 0;
    dma->tx_stream->CR   = (dma->ch_tx << DMA_SxCR_CHSEL_Pos)
                           | DMA_SxCR_MINC
                           | DMA_SxCR_TCIE
                           | DMA_SxCR_TEIE
                           | DMA_SxCR_DIR_0;

    /* Ensure PSIZE=00(8-bit), MSIZE=00(8-bit) */
    dma->rx_stream->CR &= ~(DMA_SxCR_PSIZE_Msk | DMA_SxCR_MSIZE_Msk);
    dma->tx_stream->CR &= ~(DMA_SxCR_PSIZE_Msk | DMA_SxCR_MSIZE_Msk);

    /* Clear stale interrupt flags — use CMSIS bit definitions.
     * Stream 0-3 → LIFCR, Stream 4-7 → HIFCR.
     * Each stream's TCIF flag is at pos 5 + stream_in_group * 6. */
    {
        /* CTCIF bit positions for LIFCR streams 0-3 */
        static const uint32_t lifcr_ctcif[] = {
            DMA_LIFCR_CTCIF0, DMA_LIFCR_CTCIF1,
            DMA_LIFCR_CTCIF2, DMA_LIFCR_CTCIF3,
        };
        /* CTCIF bit positions for HIFCR streams 4-7 */
        static const uint32_t hifcr_ctcif[] = {
            DMA_HIFCR_CTCIF4, DMA_HIFCR_CTCIF5,
            DMA_HIFCR_CTCIF6, DMA_HIFCR_CTCIF7,
        };
        uint32_t rx_idx = ((uint32_t)dma->rx_stream - (uint32_t)DMA2) / 0x18U;
        uint32_t tx_idx = ((uint32_t)dma->tx_stream - (uint32_t)DMA2) / 0x18U;
        if (rx_idx < 4U) DMA2->LIFCR = lifcr_ctcif[rx_idx];
        else             DMA2->HIFCR = hifcr_ctcif[rx_idx - 4U];
        if (tx_idx < 4U) DMA2->LIFCR = lifcr_ctcif[tx_idx];
        else             DMA2->HIFCR = hifcr_ctcif[tx_idx - 4U];
    }

    __DSB();

#if RTT_SPI_DMA_IRQ_WAIT
    _spi_dma_wait_init_bus(bus, dma);
    spi_dma_wait_ctx *wait_ctx = (bus < RTT_CMSIS_SPI_BUS_MAX) ? &_spi_dma_wait[bus] : nullptr;
    if (wait_ctx != nullptr && wait_ctx->isr_reg != nullptr) {
        rt_completion_init(&wait_ctx->cpt);
        wait_ctx->waiting = 1;
        /* Clear stale TCIF before arming so a late IRQ cannot spuriously complete. */
        *wait_ctx->ifcr_reg = wait_ctx->ctcif_mask;
    }

    /* Enable RX stream first (then TX) — ChibiOS convention */
    dma->rx_stream->CR |= DMA_SxCR_EN;
    dma->tx_stream->CR |= DMA_SxCR_EN;
    __DSB();

    /* Enable SPI DMA requests: SPI fetches TX from DMA and writes RX to DMA */
    spi->CR2 |= SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
    __DSB();

    if (wait_ctx != nullptr && wait_ctx->isr_reg != nullptr) {
        rt_err_t werr = rt_completion_wait(&wait_ctx->cpt, rt_tick_from_millisecond(5));
        wait_ctx->waiting = 0;
        if (werr == RT_EOK &&
            !(dma->rx_stream->CR & DMA_SxCR_EN) &&
            !(dma->tx_stream->CR & DMA_SxCR_EN)) {
            return _spi_dma_xfer_finish(dma, spi);
        }
        rtt_dbg_spi_dma_irq_fallback_count++;
        uint32_t poll_timeout = 20000U + len * 32U;
        return _spi_dma_poll_en_complete(dma, spi, poll_timeout);
    }
    /* bus not hooked — fallback poll below */
    {
        uint32_t poll_timeout = 20000U + len * 32U;
        return _spi_dma_poll_en_complete(dma, spi, poll_timeout);
    }
#else
    /* Enable RX stream first (then TX) — ChibiOS convention */
    dma->rx_stream->CR |= DMA_SxCR_EN;
    dma->tx_stream->CR |= DMA_SxCR_EN;
    __DSB();

    /* Enable SPI DMA requests: SPI fetches TX from DMA and writes RX to DMA */
    spi->CR2 |= SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
    __DSB();
#endif

#if !RTT_SPI_DMA_IRQ_WAIT
    /* Poll for completion — hardware clears EN when the stream finishes.
     * Timeout: 20ms + 32us/byte (same as ChibiOS SPIDevice.cpp). */
    uint32_t timeout = 20000U + len * 32U;
    while (timeout--) {
        if (!(dma->rx_stream->CR & DMA_SxCR_EN) &&
            !(dma->tx_stream->CR & DMA_SxCR_EN)) {
            spi->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN);
            bool err = false;
            {
                static const uint32_t lisr_teif[] = {
                    DMA_LISR_TEIF0, DMA_LISR_TEIF1,
                    DMA_LISR_TEIF2, DMA_LISR_TEIF3,
                };
                static const uint32_t hisr_teif[] = {
                    DMA_HISR_TEIF4, DMA_HISR_TEIF5,
                    DMA_HISR_TEIF6, DMA_HISR_TEIF7,
                };
                uint32_t rx_idx = ((uint32_t)dma->rx_stream - (uint32_t)DMA2) / 0x18U;
                uint32_t tx_idx = ((uint32_t)dma->tx_stream - (uint32_t)DMA2) / 0x18U;
                if (rx_idx < 4U) {
                    if (DMA2->LISR & lisr_teif[rx_idx]) { err = true; }
                } else {
                    if (DMA2->HISR & hisr_teif[rx_idx - 4U]) { err = true; }
                }
                if (tx_idx < 4U) {
                    if (DMA2->LISR & lisr_teif[tx_idx]) { err = true; }
                } else {
                    if (DMA2->HISR & hisr_teif[tx_idx - 4U]) { err = true; }
                }
                if (err) {
                    if (rx_idx < 4U) DMA2->LIFCR = lisr_teif[rx_idx];
                    else             DMA2->HIFCR = hisr_teif[rx_idx - 4U];
                    if (tx_idx < 4U) DMA2->LIFCR = lisr_teif[tx_idx];
                    else             DMA2->HIFCR = hisr_teif[tx_idx - 4U];
                }
            }
            if (err) {
                _spi_dma_abort(dma, spi);
                return false;
            }
            uint32_t bsy = 10000;
            while ((spi->SR & SPI_SR_BSY) && --bsy) { __NOP(); }
            if (bsy == 0) {
                spi->CR1 &= ~SPI_CR1_SPE;
                (void)spi->SR;
                (void)spi->DR;
                spi->CR1 |= SPI_CR1_SPE;
                bsy = 10000;
                while ((spi->SR & SPI_SR_BSY) && --bsy) { __NOP(); }
            }
            return true;
        }
        __NOP();
    }
    _spi_dma_abort(dma, spi);
    return false;
#endif
}

#endif /* SOC_SERIES_STM32F7 */

using namespace RTT;

/* SPI transfer debug counter */
static volatile uint32_t rtt_dbg_spi_xfer_count = 0;

/* SPI1 runtime diagnostic — read via GDB */
volatile struct {
    uint32_t spi1_xfer_calls;
    uint32_t spi1_tx_bytes;
    uint32_t spi1_rx_bytes;
    uint32_t last_recv_0;
    uint32_t last_recv_1;
    uint32_t cr2_after_cfg;
    uint32_t cr2_after_xfer;
} rtt_spi1_rt = {};

/*
 * CS pin lookup table — matches rtt_devname to GPIO pin number.
 * Values correspond to HAL_RTT_SPI_ATTACH_LIST CS pins.
 */
struct spi_cs_entry {
    const char *rtt_devname;
    rt_base_t cs_pin;
};
static const struct spi_cs_entry _spi_cs_table[] = {
    {"spi12", 91},   /* ICM42688 CS = PF11 = 5*16+11 = 91 — must be first! */
    {"spi11", 82},
    {"spi12", 83},
    {"spi13", 84},
    {"spi14", 106},
    {"spi21", 85},
    {"spi41", 90},
};

static rt_base_t _lookup_cs_pin(const char *rtt_devname)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(_spi_cs_table); i++) {
        if (strcmp(_spi_cs_table[i].rtt_devname, rtt_devname) == 0) {
            return _spi_cs_table[i].cs_pin;
        }
    }
    return 0;
}

/*
 * SPI1 register-level polling transfer on STM32F7.
 * The RTT HAL polling path (HAL_SPI_TransmitReceive) returns incorrect data
 * for multi-byte reads on SPI1.  Direct register-level polling bypasses this.
 * CS pins must be initialised HIGH (inactive) before calling this.
 */
/*
 * Detect full-duplex: transfer_fullduplex(buf, len) calls
 * transfer(buf, len, buf, len), so send==recv and send_len==recv_len.
 * In full-duplex mode we exchange max(send_len, recv_len) bytes
 * simultaneously.  In half-duplex (write-then-read) mode we exchange
 * send_len + recv_len bytes sequentially.
 */
/*
 * Map AP bus number to STM32F7 SPI peripheral.
 */
static SPI_TypeDef *bus_to_spi(uint8_t bus)
{
    switch (bus) {
    case 1: return SPI1;
    case 2: return SPI2;
#ifdef SPI3
    case 3: return SPI3;
#endif
#ifdef SPI4
    case 4: return SPI4;
#endif
#ifdef SPI5
    case 5: return SPI5;
#endif
#ifdef SPI6
    case 6: return SPI6;
#endif
    default: return SPI1;
    }
}

static bool spi1_poll_transfer(struct rt_spi_device *dev,
                                const uint8_t *send, uint32_t send_len,
                                uint8_t *recv, uint32_t recv_len,
                                bool cs_take, bool cs_release,
                                SPI_TypeDef *spi,
                                rt_base_t cs_pin,
                                uint32_t br)
{
    const bool fullduplex = (send_len > 0 && recv_len > 0 &&
                             send == recv && send_len == recv_len);
    const uint32_t total_len = fullduplex ? send_len : (send_len + recv_len);
    uint8_t _bounce[64];
    uint8_t *buf;
    bool heap = false;

    if (total_len == 0) return true;

    if (total_len <= sizeof(_bounce)) {
        buf = _bounce;
    } else {
        buf = (uint8_t *)rt_malloc_align(total_len, 32);
        if (buf == nullptr) return false;
        heap = true;
    }

    if (send_len > 0) memcpy(buf, send, send_len);
    if (!fullduplex && recv_len > 0) memset(buf + send_len, 0, recv_len);

    /*
     * Configure SPI before asserting CS — matches ChibiOS spiStart() convention.
     *
     * ChibiOS (SPIDevice.cpp:397-420): acquire_bus() calls spiStart() to configure
     * CR1 + enable SPE BEFORE CS is asserted.  The original RTT code did the
     * opposite — CS assertion before SPE toggle — creating a glitch on SCK
     * (MODE3 idle-HIGH → GPIO input → AF output) that confuses the IMU slave
     * during its first byte of the transaction.
     *
     * Only re-initialize SPI on standalone transactions (cs_take == true).
     * When CS is held across calls (ICM20689 multi-part burst read), cs_take=false
     * and we reuse the existing CR1/CR2/SPE configuration — no glitch.
     *
     * BR is fixed at BR_0|BR_1 (=BR=3, /16 = 6.75MHz) for now, matching
     * ChibiOS SPEED_HIGH.  SPEED_LOW (2MHz → BR=5, /64) requires dynamic BR
     * selection (ChibiOS: derive_freq_flag, SPIDevice.cpp:259-281).
     */
    if (cs_take) {
        CLEAR_BIT(spi->CR1, SPI_CR1_SPE);

        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                   SPI_CR1_CPOL | SPI_CR1_CPHA |
                   br;
        spi->CR2 = SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2 | SPI_CR2_FRXTH;
        rtt_spi1_rt.cr2_after_cfg = spi->CR2;
        SET_BIT(spi->CR1, SPI_CR1_SPE);

        /* Flush stale FIFO — SPI is now running, CS still HIGH */
        while (spi->SR & SPI_SR_RXNE) { (void)*((__IO uint8_t *)&spi->DR); }
        (void)spi->SR;
    }

    /* Assert CS via GPIO BSRR — SPI is fully configured and running */
    if (cs_take) {
        rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
        uint32_t port_idx = cs >> 4;
        uint32_t pin = cs & 0xF;
        volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
        *bsrr = 1U << (pin + 16);
    }

    for (uint32_t i = 0; i < total_len; i++) {
        uint32_t timeout = 100000;
        while (!(spi->SR & SPI_SR_TXE) && --timeout) { __NOP(); }
        if (timeout == 0) {
            if (cs_release) {
                rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                *(volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U) = 1U << pin;
            }
            CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
            while (spi->SR & SPI_SR_RXNE) { (void)(*(__IO uint8_t *)&spi->DR); }
            if (heap) { rt_free_align(buf); }
            return false;
        }
        *((__IO uint8_t *)&spi->DR) = buf[i];
        timeout = 100000;
        while (!(spi->SR & SPI_SR_RXNE) && --timeout) { __NOP(); }
        if (timeout == 0) {
            (void)*((__IO uint8_t *)&spi->DR); // drain DR
            if (cs_release) {
                rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                *(volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U) = 1U << pin;
            }
            CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
            while (spi->SR & SPI_SR_RXNE) { (void)(*(__IO uint8_t *)&spi->DR); }
            if (heap) { rt_free_align(buf); }
            return false;
        }
        buf[i] = *((__IO uint8_t *)&spi->DR);
    }

    uint32_t timeout = 10000;
    while ((spi->SR & SPI_SR_BSY) && --timeout) { __NOP(); }

    /* Release CS via GPIO BSRR */
    if (cs_release) {
        rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
        uint32_t port_idx = cs >> 4;
        uint32_t pin = cs & 0xF;
        volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
        *bsrr = 1U << pin;
    }

    if (fullduplex) {
        memcpy(recv, buf, recv_len);
    } else {
        memcpy(recv, buf + send_len, recv_len);
    }

    /* Runtime diagnostic */
    rtt_spi1_rt.cr2_after_xfer = spi->CR2;
    rtt_spi1_rt.spi1_xfer_calls++;
    rtt_spi1_rt.spi1_tx_bytes += send_len;
    rtt_spi1_rt.spi1_rx_bytes += recv_len;
    if (recv_len > 0) rtt_spi1_rt.last_recv_0 = fullduplex ? buf[0] : buf[send_len];
    if (recv_len > 1) rtt_spi1_rt.last_recv_1 = fullduplex ? buf[1] : buf[send_len + 1];

    if (heap) { rt_free_align(buf); }
    return true;
}

static uint32_t _spi_mode_to_rtt(uint8_t mode)
{
    switch (mode) {
    case 0: return RT_SPI_MODE_0;
    case 1: return RT_SPI_MODE_1;
    case 2: return RT_SPI_MODE_2;
    default: return RT_SPI_MODE_3;
    }
}

SPIDevice::SPIDevice(RTT_SPIDesc &desc)
    : AP_HAL::SPIDevice()
    , _desc(desc)
    , _dev(nullptr)
    , _bus(DeviceBus::get_bus(desc.bus, APM_RTT_SPI_PRIORITY))
    , _cs_pin(0)
    , _br(SPI_CR1_BR_0 | SPI_CR1_BR_1)   /* BR=3 = default HIGH (6.75MHz @ 108MHz) */
{
    set_device_bus(desc.bus);
    _cs_pin = _lookup_cs_pin(desc.rtt_devname);
    /* For STM32F7 SPI1 devices (IMUs), use register-level polling directly,
     * bypassing RT-Thread's SPI framework which has DMA and GPIO config issues.
     * See _spi1_gpio_init() and spi1_poll_transfer() for the polling path. */
#ifndef FORCE_RTT_SPI_FRAMEWORK
    if (_desc.bus == 1 || _desc.bus == 2 || _desc.bus == 4) {
        _dev = nullptr;
        return;
    }
#endif
    _dev = (struct rt_spi_device *)rt_device_find(desc.rtt_devname);
    if (_dev != nullptr) {
        set_speed(AP_HAL::Device::SPEED_LOW);
    }
}

SPIDevice::~SPIDevice()
{
}

bool SPIDevice::_lock_bus()
{
    if (_dev == nullptr) {
#ifdef SOC_SERIES_STM32F7
        _cmsis_spi_bus_mtx_ensure(_desc.bus);
        if (_desc.bus >= RTT_CMSIS_SPI_BUS_MAX) {
            return false;
        }
        return rt_mutex_take(&_cmsis_spi_bus_mtx[_desc.bus], RT_WAITING_FOREVER) == RT_EOK;
#else
        return true;
#endif
    }
    if (_dev->bus == nullptr || _dev->bus->ops == nullptr) {
        return false;
    }
    if (_bus_locked) {
        return true;
    }
    if (rt_mutex_take(&(_dev->bus->lock), RT_WAITING_FOREVER) != RT_EOK) {
        return false;
    }
    if (_config_dirty || _dev->bus->owner != _dev) {
        if (_dev->bus->ops->configure(_dev, &_dev->config) != RT_EOK) {
            rt_mutex_release(&(_dev->bus->lock));
            return false;
        }
        _dev->bus->owner = _dev;
        _config_dirty = false;
    }
    return true;
}

void SPIDevice::_unlock_bus()
{
    if (_dev == nullptr) {
#ifdef SOC_SERIES_STM32F7
        if (_desc.bus < RTT_CMSIS_SPI_BUS_MAX) {
            rt_mutex_release(&_cmsis_spi_bus_mtx[_desc.bus]);
        }
#endif
        return;
    }
    if (_dev->bus != nullptr && !_bus_locked) {
        rt_mutex_release(&(_dev->bus->lock));
    }
}

bool SPIDevice::set_speed(AP_HAL::Device::Speed speed)
{
#ifdef SOC_SERIES_STM32F7
    if (_dev == nullptr) {
        /* Register-level path: update per-device BR for the polling transfer.
         * Matches ChibiOS derive_freq_flag() semantics — find the lowest
         * divider that brings clock below target frequency. */
        uint32_t target_hz = (speed == AP_HAL::Device::SPEED_HIGH)
                             ? _desc.highspeed : _desc.lowspeed;
        if (target_hz == 0) target_hz = 8000000U;
        /* ChibiOS: derive_freq_flag_bus() starts from bus_clocks/2 and halves.
         * SPI1/SPI4 share STM32_PCLK2 = 108MHz. bus_clocks[0]/2 = 54MHz.
         * For target=2MHz: 54M→27M→13.5M→6.75M→3.375M→1.6875M → i=5 → BR=5
         * For target=8MHz: 54M→27M→13.5M→6.75M            → i=3 → BR=3
         * For target=20MHz: 54M→27M→13.5M                  → i=2 → BR=2 */
        uint32_t clk_hz = SPI1_PCLK2_HZ;
        if (_desc.bus == 4) {
            clk_hz = SPI4_PCLK2_HZ;
        } else if (_desc.bus == 2) {
            clk_hz = SPI2_PCLK1_HZ;
        }
        uint32_t clk = clk_hz / 2U;
        uint32_t i = 0;
        while (clk > target_hz && i < 7) { clk >>= 1U; i++; }
        _br = i * SPI_CR1_BR_0;
        return true;
    }
#endif
    if (_dev == nullptr) return false;
    const uint32_t target_hz =
        (speed == AP_HAL::Device::SPEED_HIGH) ? _desc.highspeed : _desc.lowspeed;

    if (_dev->config.mode == _spi_mode_to_rtt(_desc.mode) &&
        _dev->config.data_width == 8 &&
        _dev->config.max_hz == target_hz) {
        return true;
    }

    _dev->config.mode = _spi_mode_to_rtt(_desc.mode) | RT_SPI_MSB;
    _dev->config.data_width = 8;
    _dev->config.max_hz = target_hz;
    _config_dirty = true;
    return true;
}

/*
 * SPI1 register-level polling transfer on STM32F7.
 * Direct register polling bypasses broken HAL_SPI_TransmitReceive path.
 */

bool SPIDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
#ifdef SOC_SERIES_STM32F7
    if (_dev == nullptr) {
        /* Skip GPIO re-init during CS-held burst reads.
         * _spi1_gpio_init() sets ALL CS pins HIGH, which would
         * inadvertently release CS mid-transaction, causing the
         * IMU slave to abort the burst and return all-zero data. */
        if (!_cs_held) {
            if (_desc.bus == 4) {
                _spi4_gpio_init();
            } else if (_desc.bus == 2) {
                _spi2_gpio_init();
            } else {
                _spi1_gpio_init();
            }
        }
        if (send_len > 0 || recv_len > 0) {
            if (!_cs_held && !_lock_bus()) {
                return false;
            }
            bool ok = false;

            /* ── Full-duplex case (send == recv, same len): try DMA ── */
            const bool fullduplex = (send_len > 0 && recv_len > 0 &&
                                     send == recv && send_len == recv_len);

            if (fullduplex) {
                /* CS低 — 用BSRR复位CS引脚 */
                {
                    rt_base_t cs = _cs_pin;
                    uint32_t port_idx = cs >> 4;
                    uint32_t pin = cs & 0xF;
                    *(volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U) = 1U << (pin + 16);
                }
                ok = _spi_dma_xfer(_bus, bus_to_spi(_desc.bus), _desc.bus,
                                   send, recv, send_len, _br);
                /* CS高 — 用BSRR置位CS引脚 */
                {
                    rt_base_t cs = _cs_pin;
                    uint32_t port_idx = cs >> 4;
                    uint32_t pin = cs & 0xF;
                    *(volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U) = 1U << pin;
                }
            } else {
                /* ── Half-duplex (write then read): use bounce buffer, poll ── */
                ok = spi1_poll_transfer(nullptr, send, send_len, recv, recv_len,
                                        !_cs_held, !_cs_held,
                                        bus_to_spi(_desc.bus), _cs_pin, _br);
            }

            if (!_cs_held) {
                _unlock_bus();
            }
            return ok;
        }
        return true;
    }
#endif
    if (_dev == nullptr) return false;

    if (!_cs_held && !_lock_bus()) {
        return false;
    }

    bool ok = false;
    const bool cs_take = !_cs_held;
    const bool cs_release = !_cs_held;

    if (send_len > 0 && recv_len > 0) {
        uint8_t _bounce[64];
        const uint32_t total_len = send_len + recv_len;
        uint8_t *buf;
        bool heap = false;

        if (total_len <= sizeof(_bounce)) {
            buf = _bounce;
        } else {
            buf = (uint8_t *)rt_malloc_align(total_len, 32);
            if (buf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                return false;
            }
            heap = true;
        }

        memcpy(buf, send, send_len);
        memset(buf + send_len, 0, recv_len);

        struct rt_spi_message msg = {};
        msg.send_buf = buf;
        msg.recv_buf = buf;
        msg.length = total_len;
        msg.cs_take = cs_take ? 1U : 0U;
        msg.cs_release = cs_release ? 1U : 0U;
        msg.next = RT_NULL;

        rtt_dbg_spi_xfer_count++;
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        if (ret == RT_NULL) {
            memcpy(recv, buf + send_len, recv_len);
            ok = true;
        }
        rtt_dbg_spi_xfer_count++;

        if (heap) { rt_free_align(buf); }
    } else if (send_len > 0) {
        uint8_t _bounce_rx[64];
        uint8_t *rxbuf = _bounce_rx;
        bool heap = false;

        if (send_len > sizeof(_bounce_rx)) {
            rxbuf = (uint8_t *)rt_malloc_align(send_len, 32);
            if (rxbuf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                return false;
            }
            heap = true;
        }

        memset(rxbuf, 0, send_len);

        struct rt_spi_message msg = {};
        msg.send_buf = send;
        msg.recv_buf = rxbuf;
        msg.length = send_len;
        msg.cs_take = cs_take ? 1U : 0U;
        msg.cs_release = cs_release ? 1U : 0U;
        msg.next = RT_NULL;

        rtt_dbg_spi_xfer_count++;
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        ok = (ret == RT_NULL);
        rtt_dbg_spi_xfer_count++;

        if (heap) { rt_free_align(rxbuf); }
    } else if (recv_len > 0) {
        struct rt_spi_message msg = {};
        msg.send_buf = RT_NULL;
        msg.recv_buf = recv;
        msg.length = recv_len;
        msg.cs_take = cs_take ? 1U : 0U;
        msg.cs_release = cs_release ? 1U : 0U;
        msg.next = RT_NULL;

        rtt_dbg_spi_xfer_count++;
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        ok = (ret == RT_NULL);
        rtt_dbg_spi_xfer_count++;
    }

    if (!_cs_held) { _unlock_bus(); }
    return ok;
}

bool SPIDevice::set_chip_select(bool set)
{
#ifdef SOC_SERIES_STM32F7
    if (_dev == nullptr) {
        if (set && !_cs_held) {
            /* Align with ChibiOS: take bus semaphore before asserting CS.
             * This prevents the DeviceBus thread from dispatching periodic
             * callbacks (e.g. _poll_data()) while CS is held during a
             * burst read, which would cause bus contention and data
             * corruption.  ChibiOS ref: SPIDevice.h:set_chip_select().
             *
             * The semaphore is released in set_chip_select(false). */
            if (!_lock_bus()) {
                return false;
            }
            /* Configure SPI registers (CR1/CR2/SPE) BEFORE asserting CS.
             * When cs_take=false, spi1_poll_transfer() skips SPI config
             * entirely, relying on the caller to have configured it via
             * set_chip_select(true) — matching the ChibiOS contract where
             * acquire_bus() calls spiStart() to init CR1+SPE before CS. */
            {
                SPI_TypeDef *spi = bus_to_spi(_desc.bus);
                CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
                spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                           SPI_CR1_CPOL | SPI_CR1_CPHA |
                           _br;
                spi->CR2 = SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2 |
                           SPI_CR2_FRXTH;
                __DSB();
                SET_BIT(spi->CR1, SPI_CR1_SPE);
                while (spi->SR & SPI_SR_RXNE) {
                    (void)*(volatile uint8_t *)&spi->DR;
                }
                (void)spi->SR;
            }
            /* Actually assert CS via GPIO BSRR — callers (Invensense IMU
             * driver) expect the pin to be driven LOW (active) after
             * set_chip_select(true) so that subsequent transfer() /
             * transfer_fullduplex() calls with cs_take=false happen while
             * CS is asserted, enabling multi-byte burst reads (e.g.
             * ICM20689 112-byte FIFO read). */
            if (_desc.bus == 4) {
                _spi4_gpio_init();
            } else if (_desc.bus == 2) {
                _spi2_gpio_init();
            } else {
                _spi1_gpio_init();
            }
            rt_base_t cs = (_cs_pin != 0) ? _cs_pin : 0;
            if (cs != 0) {
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
                *bsrr = 1U << (pin + 16);  /* BR = drive LOW */
            }
        } else if (!set && _cs_held) {
            /* De-assert CS then release bus lock (FRAM WREN needs CS rising edge). */
            rt_base_t cs = (_cs_pin != 0) ? _cs_pin : 0;
            if (cs != 0) {
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                volatile uint32_t *bsrr =
                    (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
                *bsrr = 1U << pin;  /* BS = drive HIGH */
            }
            _cs_held = false;
            _unlock_bus();
            return true;
        }
        _cs_held = set;
        return true;
    }
#endif
    if (_dev == nullptr) {
        return false;
    }

    if (set) {
        if (_cs_held) {
            return true;
        }
        if (!_lock_bus()) {
            return false;
        }
        _cs_held = true;
        return true;
    }

    if (_cs_held) {
        _cs_held = false;
        _unlock_bus();
    }
    return true;
}

/*
 * transfer_fullduplex — send and receive simultaneously.
 */
bool SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv, uint32_t len)
{
    if (_dev == nullptr) {
#ifdef SOC_SERIES_STM32F7
        /* Skip GPIO re-init during CS-held burst reads.
         * See transfer() for detailed rationale. */
        if (!_cs_held) {
            if (_desc.bus == 4) {
                _spi4_gpio_init();
            } else if (_desc.bus == 2) {
                _spi2_gpio_init();
            } else {
                _spi1_gpio_init();
            }
        }
        if (len > 0) {
            if (!_cs_held && !_lock_bus()) {
                return false;
            }
            const bool ok = _spi_dma_xfer(_bus, bus_to_spi(_desc.bus), _desc.bus,
                                          send, recv, len, _br);
            if (!_cs_held) {
                _unlock_bus();
            }
            return ok;
        }
#endif
        return false;
    }

    if (!_cs_held && !_lock_bus()) {
        return false;
    }

    const bool cs_take = !_cs_held;
    const bool cs_release = !_cs_held;

    uint8_t _bounce[64];
    uint8_t *txbuf = (uint8_t *)send;
    uint8_t *rxbuf = recv;
    bool ok = false;
    bool heap = false;

    if (send == recv) {
        if (len <= sizeof(_bounce)) {
            txbuf = _bounce;
            rxbuf = _bounce;
        } else {
            txbuf = (uint8_t *)rt_malloc_align(len, 32);
            if (txbuf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                return false;
            }
            rxbuf = txbuf;
            heap = true;
        }
        memcpy(txbuf, send, len);
    }

    struct rt_spi_message msg = {};
    msg.send_buf = txbuf;
    msg.recv_buf = rxbuf;
    msg.length = len;
    msg.cs_take = cs_take ? 1U : 0U;
    msg.cs_release = cs_release ? 1U : 0U;
    msg.next = RT_NULL;

    rtt_dbg_spi_xfer_count++;
    struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
    ok = (ret == RT_NULL);
    if (ok && send == recv) {
        memcpy(recv, rxbuf, len);
    }
    rtt_dbg_spi_xfer_count++;

    if (heap) { rt_free_align(txbuf); }
    if (!_cs_held) { _unlock_bus(); }
    return ok;
}

AP_HAL::Semaphore *SPIDevice::get_semaphore()
{
    // Return the bus-level semaphore, matching ChibiOS semantics.
    // This ensures WITH_SEMAPHORE(_dev->get_semaphore()) holds the bus lock,
    // preventing the DeviceBus thread from dispatching periodic callbacks
    // (e.g. ICM20689 _poll_data) concurrently with transfers on the same bus.
    // Ref: AP_HAL_ChibiOS/SPIDevice.cpp:336-339
    return &_bus->semaphore;
}

AP_HAL::Device::PeriodicHandle SPIDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return _bus->register_periodic_callback(period_usec, cb, this);
}

bool SPIDevice::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    return _bus->adjust_timer(h, period_usec);
}
