/*
 * LL GPIO driver for STM32F767 — standalone functions.
 * Uses LL_GPIO inline API and direct register access (BSRR/IDR).
 * Designed to be a drop-in replacement for drv_gpio.c's pin ops in Phase 4.
 */
#ifndef __DRV_GPIO_LL_H__
#define __DRV_GPIO_LL_H__

#include <rtthread.h>
#include <rtdevice.h>
#include "stm32f7xx.h"
#include "stm32f7xx_ll_gpio.h"
#include "stm32f7xx_ll_bus.h"

/* Pin encoding: same as RT-Thread drv_gpio.c for compatibility */
#define GPIO_LL_PIN(port_idx, pin_no)  (((port_idx) << 4) | ((pin_no) & 0x0F))
#define GPIO_LL_PORT_IDX(pin)          (((pin) >> 4) & 0x0F)
#define GPIO_LL_PIN_NO(pin)            ((pin) & 0x0F)

/* Get GPIO peripheral base from port index (0=A, 1=B, ...) */
static inline GPIO_TypeDef *gpio_ll_port(uint8_t port_idx)
{
    return (GPIO_TypeDef *)(GPIOA_BASE + 0x400U * port_idx);
}

/* Get GPIO pin bit mask (same as LL_GPIO_PIN_x) */
static inline uint32_t gpio_ll_pin_bit(uint8_t pin_no)
{
    return 1U << pin_no;
}

/* Enable RCC clock for a GPIO port (AHB1 on F7) */
void gpio_ll_clk_enable(uint8_t port_idx);

/* Configure pin mode (INPUT, OUTPUT_PP, OUTPUT_OD, IT_RISING, etc.) */
void gpio_ll_pin_mode(rt_base_t pin, rt_uint8_t mode);

/* Write pin: direct BSRR (identical to existing register-level op) */
static inline void gpio_ll_pin_write(rt_base_t pin, rt_uint8_t value)
{
    GPIO_TypeDef *port = gpio_ll_port(GPIO_LL_PORT_IDX(pin));
    uint32_t bit = gpio_ll_pin_bit(GPIO_LL_PIN_NO(pin));
    if (value) {
        port->BSRR = bit;
    } else {
        port->BSRR = bit << 16U;
    }
}

/* Read pin: direct IDR */
static inline rt_ssize_t gpio_ll_pin_read(rt_base_t pin)
{
    GPIO_TypeDef *port = gpio_ll_port(GPIO_LL_PORT_IDX(pin));
    uint32_t bit = gpio_ll_pin_bit(GPIO_LL_PIN_NO(pin));
    return (port->IDR & bit) ? PIN_HIGH : PIN_LOW;
}

/* Enable all GPIO port clocks used by CUAV V5 */
void gpio_ll_init_all_clocks(void);

#endif /* __DRV_GPIO_LL_H__ */
