/*
 * LL USART driver for STM32F767.
 * GPIO AF, USART init, polled TX — all via direct register access.
 * No dependency on stm32f7xx_ll_usart.c / stm32f7xx_ll_gpio.c object files.
 */

#include "drv_usart_ll.h"
#include "drv_gpio_ll.h"

/* ---- Predefined configs for CUAV V5 ---- */

const usart_ll_config_t usart3_ll_cfg = {
    .Instance    = USART3,
    .baudrate    = 115200,
    .tx_port_idx = 3,   /* PD */
    .tx_pin_no   = 8,
    .tx_af       = 7,
    .rx_port_idx = 3,   /* PD */
    .rx_pin_no   = 9,
    .rx_af       = 7,
    .irqn        = USART3_IRQn,
};

const usart_ll_config_t uart7_ll_cfg = {
    .Instance    = UART7,
    .baudrate    = 115200,
    .tx_port_idx = 4,   /* PE */
    .tx_pin_no   = 8,
    .tx_af       = 8,
    .rx_port_idx = 5,   /* PF */
    .rx_pin_no   = 6,
    .rx_af       = 8,
    .irqn        = UART7_IRQn,
};

/* ---- Internal: GPIO alternate function ---- */

static void _gpio_af_config(uint8_t port_idx, uint8_t pin_no, uint8_t af,
                             uint32_t pupd)
{
    gpio_ll_clk_enable(port_idx);
    GPIO_TypeDef *port = gpio_ll_port(port_idx);
    uint32_t pos2 = pin_no * 2U;

    /* MODER = 0x02 (Alternate function) */
    uint32_t tmp = port->MODER;
    tmp &= ~(0x3U << pos2);
    tmp |= (0x2U << pos2);
    port->MODER = tmp;

    /* OTYPER = push-pull (0) */
    port->OTYPER &= ~(1U << pin_no);

    /* OSPEEDR = Very High (0x03) for USART */
    tmp = port->OSPEEDR;
    tmp &= ~(0x3U << pos2);
    tmp |= (0x3U << pos2);
    port->OSPEEDR = tmp;

    /* PUPDR */
    tmp = port->PUPDR;
    tmp &= ~(0x3U << pos2);
    tmp |= (pupd << pos2);
    port->PUPDR = tmp;

    /* AFR[0] for pins 0-7, AFR[1] for pins 8-15 */
    uint8_t afr_idx = pin_no >> 3;
    uint8_t afr_pos = (pin_no & 7U) * 4U;
    tmp = port->AFR[afr_idx];
    tmp &= ~(0xFU << afr_pos);
    tmp |= ((uint32_t)af << afr_pos);
    port->AFR[afr_idx] = tmp;
}

/* ---- USART peripheral clock ---- */

static void _usart_clk_enable(USART_TypeDef *uart)
{
    if (uart == USART1)      RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    else if (uart == USART2) RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    else if (uart == USART3) RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    else if (uart == UART4)  RCC->APB1ENR |= RCC_APB1ENR_UART4EN;
    else if (uart == UART5)  RCC->APB1ENR |= RCC_APB1ENR_UART5EN;
    else if (uart == USART6) RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    else if (uart == UART7)  RCC->APB1ENR |= RCC_APB1ENR_UART7EN;
    else if (uart == UART8)  RCC->APB1ENR |= RCC_APB1ENR_UART8EN;
    __DSB();
}

uint32_t usart_ll_get_pclk(USART_TypeDef *uart)
{
    extern uint32_t SystemCoreClock;
    /* APB2 (108 MHz): USART1, USART6 */
    if (uart == USART1 || uart == USART6)
        return SystemCoreClock / 2U;
    /* APB1 (54 MHz): everything else */
    return SystemCoreClock / 4U;
}

/* ---- Public API ---- */

void usart_ll_init(const usart_ll_config_t *cfg)
{
    USART_TypeDef *uart = cfg->Instance;

    /* Enable peripheral clock */
    _usart_clk_enable(uart);

    /* Configure GPIO pins: TX = AF push-pull pull-up, RX = AF pull-up */
    _gpio_af_config(cfg->tx_port_idx, cfg->tx_pin_no, cfg->tx_af, 0x01);
    _gpio_af_config(cfg->rx_port_idx, cfg->rx_pin_no, cfg->rx_af, 0x01);

    /* Disable USART before configuration */
    uart->CR1 &= ~USART_CR1_UE;

    /* CR1: 8-bit word, no parity, oversampling x16 */
    uart->CR1 = 0;
    /* CR2: 1 stop bit */
    uart->CR2 = 0;
    /* CR3: no flow control, no DMA */
    uart->CR3 = 0;

    /* BRR: for OVER8=0, BRR = fck / baud (rounded) */
    uint32_t pclk = usart_ll_get_pclk(uart);
    uart->BRR = (pclk + cfg->baudrate / 2U) / cfg->baudrate;

    /* Enable USART: TX + RX + UE */
    uart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void usart_ll_putc(USART_TypeDef *uart, uint8_t ch)
{
    while (!(uart->ISR & USART_ISR_TXE)) { }
    uart->TDR = ch;
}

int usart_ll_getc_nb(USART_TypeDef *uart)
{
    if (uart->ISR & USART_ISR_RXNE)
        return (int)(uart->RDR & 0xFFU);
    return -1;
}

int usart_ll_tx_poll(USART_TypeDef *uart, const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        while (!(uart->ISR & USART_ISR_TXE)) { }
        uart->TDR = buf[i];
    }
    /* Wait for transmission complete */
    while (!(uart->ISR & USART_ISR_TC)) { }
    return len;
}
