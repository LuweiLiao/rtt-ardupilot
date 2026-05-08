/*
 * LL SPI driver for STM32F767 — polled master mode, direct register access.
 * Byte-by-byte TX/RX via TXE/RXNE flags with 8-bit FIFO threshold.
 */

#include "drv_spi_ll.h"
#include "drv_gpio_ll.h"

/* ---- Predefined configs for CUAV V5 ---- */

const spi_ll_config_t spi1_ll_cfg = {
    .Instance       = SPI1,
    .sck_port_idx   = 6,  /* PG11 */
    .sck_pin_no     = 11,
    .miso_port_idx  = 6,  /* PG9 */
    .miso_pin_no    = 9,
    .mosi_port_idx  = 1,  /* PB5 */
    .mosi_pin_no    = 5,
    .af             = 5,
    .mode           = 3,  /* CPOL=1, CPHA=1 */
    .prescaler      = 3,  /* /16 → APB2 108MHz/16 = 6.75MHz */
};

const spi_ll_config_t spi4_ll_cfg = {
    .Instance       = SPI4,
    .sck_port_idx   = 4,  /* PE2 */
    .sck_pin_no     = 2,
    .miso_port_idx  = 4,  /* PE13 */
    .miso_pin_no    = 13,
    .mosi_port_idx  = 4,  /* PE6 */
    .mosi_pin_no    = 6,
    .af             = 5,
    .mode           = 3,  /* CPOL=1, CPHA=1 */
    .prescaler      = 3,  /* /16 → APB2 108MHz/16 = 6.75MHz */
};

/* ---- Internal: GPIO AF config (same as USART LL) ---- */

static void _gpio_af_set(uint8_t port_idx, uint8_t pin_no, uint8_t af,
                          uint32_t pupd)
{
    gpio_ll_clk_enable(port_idx);
    GPIO_TypeDef *port = gpio_ll_port(port_idx);
    uint32_t pos2 = pin_no * 2U;
    uint32_t tmp;

    /* MODER = AF (0x02) */
    tmp = port->MODER;
    tmp &= ~(0x3U << pos2);
    tmp |= (0x2U << pos2);
    port->MODER = tmp;

    /* Push-pull */
    port->OTYPER &= ~(1U << pin_no);

    /* Very-high speed */
    tmp = port->OSPEEDR;
    tmp &= ~(0x3U << pos2);
    tmp |= (0x3U << pos2);
    port->OSPEEDR = tmp;

    /* Pull-up/down */
    tmp = port->PUPDR;
    tmp &= ~(0x3U << pos2);
    tmp |= (pupd << pos2);
    port->PUPDR = tmp;

    /* AF register */
    uint8_t afr_idx = pin_no >> 3;
    uint8_t afr_pos = (pin_no & 7U) * 4U;
    tmp = port->AFR[afr_idx];
    tmp &= ~(0xFU << afr_pos);
    tmp |= ((uint32_t)af << afr_pos);
    port->AFR[afr_idx] = tmp;
}

/* ---- SPI clock enable ---- */

static void _spi_clk_enable(SPI_TypeDef *spi)
{
    if (spi == SPI1)      RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    else if (spi == SPI2) RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    else if (spi == SPI3) RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    else if (spi == SPI4) RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
    __DSB();
}

uint32_t spi_ll_get_pclk(SPI_TypeDef *spi)
{
    extern uint32_t SystemCoreClock;
    if (spi == SPI1 || spi == SPI4)
        return SystemCoreClock / 2U;   /* APB2 = 108 MHz */
    return SystemCoreClock / 4U;       /* APB1 = 54 MHz */
}

/* ---- Public API ---- */

void spi_ll_init(const spi_ll_config_t *cfg)
{
    SPI_TypeDef *spi = cfg->Instance;

    _spi_clk_enable(spi);

    /* GPIO: SCK/MOSI = AF push-pull no-pull, MISO = AF input no-pull */
    _gpio_af_set(cfg->sck_port_idx, cfg->sck_pin_no, cfg->af, 0x00);
    _gpio_af_set(cfg->mosi_port_idx, cfg->mosi_pin_no, cfg->af, 0x00);
    _gpio_af_set(cfg->miso_port_idx, cfg->miso_pin_no, cfg->af, 0x00);

    /* Disable SPI */
    spi->CR1 &= ~SPI_CR1_SPE;

    /* CR1: Master, software CS (SSM+SSI), baud rate, CPOL/CPHA */
    uint32_t cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    cr1 |= ((uint32_t)(cfg->prescaler & 7U)) << SPI_CR1_BR_Pos;
    if (cfg->mode & 0x02) cr1 |= SPI_CR1_CPOL;
    if (cfg->mode & 0x01) cr1 |= SPI_CR1_CPHA;
    spi->CR1 = cr1;

    /* CR2: 8-bit data, FRXTH=1 (RXNE event on 8 bits) */
    spi->CR2 = SPI_CR2_FRXTH | (0x7U << SPI_CR2_DS_Pos);  /* DS=0111 = 8-bit */

    /* Enable SPI */
    spi->CR1 |= SPI_CR1_SPE;
}

void spi_ll_set_speed(SPI_TypeDef *spi, uint8_t prescaler)
{
    spi->CR1 &= ~SPI_CR1_SPE;
    uint32_t cr1 = spi->CR1;
    cr1 &= ~SPI_CR1_BR_Msk;
    cr1 |= ((uint32_t)(prescaler & 7U)) << SPI_CR1_BR_Pos;
    spi->CR1 = cr1;
    spi->CR1 |= SPI_CR1_SPE;
}

int spi_ll_xfer_poll(SPI_TypeDef *spi, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    /* Drain any stale RX data */
    while (spi->SR & SPI_SR_RXNE)
        (void)(*(volatile uint8_t *)&spi->DR);

    for (uint16_t i = 0; i < len; i++) {
        /* Wait TXE */
        while (!(spi->SR & SPI_SR_TXE)) { }
        /* Write 8-bit (must use byte access for 8-bit frame) */
        *(volatile uint8_t *)&spi->DR = tx ? tx[i] : 0xFFU;
        /* Wait RXNE */
        while (!(spi->SR & SPI_SR_RXNE)) { }
        uint8_t rd = *(volatile uint8_t *)&spi->DR;
        if (rx) rx[i] = rd;
    }

    /* Wait until not busy */
    while (spi->SR & SPI_SR_BSY) { }

    return (int)len;
}
