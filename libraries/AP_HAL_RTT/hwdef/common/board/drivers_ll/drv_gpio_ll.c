/*
 * LL GPIO driver for STM32F767.
 * Pin mode configuration uses direct register writes (MODER/OTYPER/OSPEEDR/PUPDR).
 * Read/write use direct BSRR/IDR (inline in header).
 */

#include "drv_gpio_ll.h"
#include "stm32f7xx_ll_bus.h"

static const uint32_t _gpio_clk_bits[] = {
    LL_AHB1_GRP1_PERIPH_GPIOA,
    LL_AHB1_GRP1_PERIPH_GPIOB,
    LL_AHB1_GRP1_PERIPH_GPIOC,
    LL_AHB1_GRP1_PERIPH_GPIOD,
    LL_AHB1_GRP1_PERIPH_GPIOE,
    LL_AHB1_GRP1_PERIPH_GPIOF,
    LL_AHB1_GRP1_PERIPH_GPIOG,
    LL_AHB1_GRP1_PERIPH_GPIOH,
    LL_AHB1_GRP1_PERIPH_GPIOI,
};

#define GPIO_PORT_COUNT  (sizeof(_gpio_clk_bits) / sizeof(_gpio_clk_bits[0]))

void gpio_ll_clk_enable(uint8_t port_idx)
{
    if (port_idx < GPIO_PORT_COUNT) {
        LL_AHB1_GRP1_EnableClock(_gpio_clk_bits[port_idx]);
    }
}

void gpio_ll_init_all_clocks(void)
{
    for (uint8_t i = 0; i < GPIO_PORT_COUNT; i++) {
        LL_AHB1_GRP1_EnableClock(_gpio_clk_bits[i]);
    }
}

void gpio_ll_pin_mode(rt_base_t pin, rt_uint8_t mode)
{
    uint8_t port_idx = GPIO_LL_PORT_IDX(pin);
    uint8_t pin_no   = GPIO_LL_PIN_NO(pin);
    GPIO_TypeDef *port = gpio_ll_port(port_idx);

    gpio_ll_clk_enable(port_idx);

    uint32_t moder_val;
    uint32_t otyper_val;
    uint32_t ospeedr_val;
    uint32_t pupdr_val;

    switch (mode) {
    case PIN_MODE_OUTPUT:
        moder_val   = 0x01;  /* General purpose output */
        otyper_val  = 0;     /* Push-pull */
        ospeedr_val = 0x02;  /* High speed */
        pupdr_val   = 0x00;  /* No pull */
        break;

    case PIN_MODE_OUTPUT_OD:
        moder_val   = 0x01;
        otyper_val  = 1;     /* Open-drain */
        ospeedr_val = 0x02;
        pupdr_val   = 0x00;
        break;

    case PIN_MODE_INPUT:
        moder_val   = 0x00;  /* Input */
        otyper_val  = 0;
        ospeedr_val = 0x00;
        pupdr_val   = 0x00;  /* No pull */
        break;

    case PIN_MODE_INPUT_PULLUP:
        moder_val   = 0x00;
        otyper_val  = 0;
        ospeedr_val = 0x00;
        pupdr_val   = 0x01;  /* Pull-up */
        break;

    case PIN_MODE_INPUT_PULLDOWN:
        moder_val   = 0x00;
        otyper_val  = 0;
        ospeedr_val = 0x00;
        pupdr_val   = 0x02;  /* Pull-down */
        break;

    default:
        return;
    }

    uint32_t pos2 = pin_no * 2;

    /* MODER: 2 bits per pin */
    uint32_t tmp = port->MODER;
    tmp &= ~(0x03U << pos2);
    tmp |= (moder_val << pos2);
    port->MODER = tmp;

    /* OTYPER: 1 bit per pin */
    tmp = port->OTYPER;
    tmp &= ~(0x01U << pin_no);
    tmp |= (otyper_val << pin_no);
    port->OTYPER = tmp;

    /* OSPEEDR: 2 bits per pin */
    tmp = port->OSPEEDR;
    tmp &= ~(0x03U << pos2);
    tmp |= (ospeedr_val << pos2);
    port->OSPEEDR = tmp;

    /* PUPDR: 2 bits per pin */
    tmp = port->PUPDR;
    tmp &= ~(0x03U << pos2);
    tmp |= (pupdr_val << pos2);
    port->PUPDR = tmp;
}
