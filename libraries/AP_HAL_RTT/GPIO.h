/*
 * AP_HAL_RTT — GPIO driver
 * Pure CMSIS register access for all GPIO operations.
 * Pin numbers follow GET_PIN(port,bit) = port*16 + bit.
 * Supports up to 8 concurrent pin interrupts (EXTI via RT-Thread PIN framework).
 *
 * Reference: ChibiOS GPIOv3/hal_pal_lld.c and stm32_gpio.h
 *   _pal_lld_setgroupmode()  — hal_pal_lld.c:89-143
 *   _pal_lld_enablepadevent() — hal_pal_lld.c:156-196
 */

#pragma once

#include <AP_HAL/GPIO.h>
#include "HAL_RTT_Namespace.h"

/* Max concurrent EXTI interrupt handlers */
#define RTT_GPIO_MAX_IRQ 8

namespace RTT
{

class DigitalSource : public AP_HAL::DigitalSource
{
public:
    DigitalSource(uint16_t pin);
    void mode(uint8_t output) override;
    uint8_t read() override;
    void write(uint8_t value) override;
    void toggle() override;
private:
    uint16_t _pin;
};

class GPIO : public AP_HAL::GPIO
{
public:
    void init() override;
    void    pinMode(uint8_t pin, uint8_t output) override;
    void    pinMode(uint8_t pin, uint8_t output, uint8_t alt) override;
    uint8_t read(uint8_t pin) override;
    void    write(uint8_t pin, uint8_t value) override;
    void    toggle(uint8_t pin) override;
    AP_HAL::DigitalSource* channel(uint16_t n) override;
    bool    usb_connected() override;
    bool    attach_interrupt(uint8_t pin,
                             irq_handler_fn_t fn,
                             INTERRUPT_TRIGGER_TYPE mode) override;
    bool    attach_interrupt(uint8_t pin,
                             AP_HAL::Proc fn,
                             INTERRUPT_TRIGGER_TYPE mode) override;

    bool    valid_pin(uint8_t pin) const override;
    bool    pin_to_servo_channel(uint8_t pin, uint8_t &servo_ch) const override;
    bool    wait_pin(uint8_t pin, INTERRUPT_TRIGGER_TYPE mode, uint32_t timeout_us) override;
    void    timer_tick(void) override;
    bool    arming_checks(size_t buflen, char *buffer) const override;

    bool    get_mode(uint8_t pin, uint32_t &mode) override;
    void    set_mode(uint8_t pin, uint32_t mode) override;

    /*
     * Set GPIO alternate function number (AFRL/AFRH).
     * Reference: ChibiOS _pal_lld_setgroupmode(), hal_pal_lld.c:105-129.
     * af_num: 0-15 (AF0-AF15).  Skips PA13/PA14 (SWD).
     */
    void    set_af(uint8_t pin, uint8_t af_num);

private:
    struct IRQState {
        uint8_t pin;
        irq_handler_fn_t isr_fn;
        AP_HAL::Proc     simple_fn;
        bool in_use;
        uint32_t isr_count;
        uint32_t last_isr_count;
    };
    static IRQState _irq_state[RTT_GPIO_MAX_IRQ];
    static void _irq_trampoline(void *args);
    IRQState* _find_or_alloc_irq(uint8_t pin);
    bool _isr_flood_detected = false;
    /* Public dispatch for EXTI IRQ handlers (called from extern "C") */
    friend void _rtt_gpio_exti_dispatch(uint8_t line);
};

} // namespace RTT
