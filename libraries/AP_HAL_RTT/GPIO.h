/*
 * AP_HAL_RTT — GPIO driver
 * Uses RT-Thread rt_pin_* API for digital I/O.
 * Supports up to 8 concurrent pin interrupts.
 */

#pragma once

#include <AP_HAL/GPIO.h>
#include "HAL_RTT_Namespace.h"

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

private:
    struct IRQState {
        uint8_t pin;
        irq_handler_fn_t isr_fn;
        AP_HAL::Proc     simple_fn;
        bool in_use;
    };
    static IRQState _irq_state[RTT_GPIO_MAX_IRQ];
    static void _irq_trampoline(void *args);
    IRQState* _find_or_alloc_irq(uint8_t pin);
};

} // namespace RTT
