/*
 * AP_HAL_RTT — GPIO driver implementation
 * Uses RT-Thread rt_pin_* API. Pin numbers follow GET_PIN() convention.
 * Interrupt support via rt_pin_attach_irq / rt_pin_irq_enable.
 */

#include "GPIO.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <drivers/dev_pin.h>

using namespace RTT;

/* DigitalSource */

DigitalSource::DigitalSource(uint16_t pin) : _pin(pin) {}

void DigitalSource::mode(uint8_t output)
{
    rt_pin_mode(_pin, output ? PIN_MODE_OUTPUT : PIN_MODE_INPUT);
}

uint8_t DigitalSource::read()
{
    return rt_pin_read(_pin) == PIN_HIGH ? 1 : 0;
}

void DigitalSource::write(uint8_t value)
{
    rt_pin_write(_pin, value ? PIN_HIGH : PIN_LOW);
}

void DigitalSource::toggle()
{
    write(read() ^ 1);
}

/* GPIO */

GPIO::IRQState GPIO::_irq_state[RTT_GPIO_MAX_IRQ] = {};

void GPIO::init()
{
}

void GPIO::pinMode(uint8_t pin, uint8_t output)
{
    if (output == HAL_GPIO_INPUT) {
        rt_pin_mode(pin, PIN_MODE_INPUT);
    } else {
        rt_pin_mode(pin, PIN_MODE_OUTPUT);
    }
}

void GPIO::pinMode(uint8_t pin, uint8_t output, uint8_t alt)
{
    (void)alt;
    pinMode(pin, output);
}

uint8_t GPIO::read(uint8_t pin)
{
    return rt_pin_read(pin) == PIN_HIGH ? 1 : 0;
}

void GPIO::write(uint8_t pin, uint8_t value)
{
    rt_pin_write(pin, value ? PIN_HIGH : PIN_LOW);
}

void GPIO::toggle(uint8_t pin)
{
    write(pin, read(pin) ^ 1);
}

AP_HAL::DigitalSource* GPIO::channel(uint16_t n)
{
    return NEW_NOTHROW DigitalSource(n);
}

extern "C" bool usb_device_is_configured(uint8_t busid);

bool GPIO::usb_connected()
{
    return usb_device_is_configured(0);
}

/* --- Interrupt support ------------------------------------------ */

GPIO::IRQState* GPIO::_find_or_alloc_irq(uint8_t pin)
{
    for (uint8_t i = 0; i < RTT_GPIO_MAX_IRQ; i++) {
        if (_irq_state[i].in_use && _irq_state[i].pin == pin) {
            return &_irq_state[i];
        }
    }
    for (uint8_t i = 0; i < RTT_GPIO_MAX_IRQ; i++) {
        if (!_irq_state[i].in_use) {
            _irq_state[i].pin = pin;
            _irq_state[i].in_use = true;
            return &_irq_state[i];
        }
    }
    return nullptr;
}

void GPIO::_irq_trampoline(void *args)
{
    IRQState *st = (IRQState *)args;
    if (!st) return;

    if (st->isr_fn) {
        bool state = rt_pin_read(st->pin) == PIN_HIGH;
        uint32_t ts = AP_HAL::micros();
        st->isr_fn(st->pin, state, ts);
    } else if (st->simple_fn) {
        st->simple_fn();
    }
}

static rt_uint32_t _to_rtt_irq_mode(AP_HAL::GPIO::INTERRUPT_TRIGGER_TYPE mode)
{
    switch (mode) {
    case AP_HAL::GPIO::INTERRUPT_RISING:
        return PIN_IRQ_MODE_RISING;
    case AP_HAL::GPIO::INTERRUPT_FALLING:
        return PIN_IRQ_MODE_FALLING;
    case AP_HAL::GPIO::INTERRUPT_BOTH:
        return PIN_IRQ_MODE_RISING_FALLING;
    default:
        return PIN_IRQ_MODE_RISING;
    }
}

bool GPIO::attach_interrupt(uint8_t pin,
                            irq_handler_fn_t fn,
                            INTERRUPT_TRIGGER_TYPE mode)
{
    if (mode == INTERRUPT_NONE || fn == nullptr) {
        for (uint8_t i = 0; i < RTT_GPIO_MAX_IRQ; i++) {
            if (_irq_state[i].in_use && _irq_state[i].pin == pin) {
                rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
                rt_pin_detach_irq(pin);
                _irq_state[i].in_use = false;
                _irq_state[i].isr_fn = nullptr;
                _irq_state[i].simple_fn = nullptr;
                return true;
            }
        }
        return fn == nullptr;
    }

    IRQState *st = _find_or_alloc_irq(pin);
    if (!st) return false;

    st->isr_fn = fn;
    st->simple_fn = nullptr;

    rt_pin_mode(pin, PIN_MODE_INPUT);
    rt_pin_attach_irq(pin, _to_rtt_irq_mode(mode), _irq_trampoline, st);
    rt_pin_irq_enable(pin, PIN_IRQ_ENABLE);
    return true;
}

bool GPIO::attach_interrupt(uint8_t pin, AP_HAL::Proc fn,
                            INTERRUPT_TRIGGER_TYPE mode)
{
    if (mode == INTERRUPT_NONE || fn == nullptr) {
        for (uint8_t i = 0; i < RTT_GPIO_MAX_IRQ; i++) {
            if (_irq_state[i].in_use && _irq_state[i].pin == pin) {
                rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
                rt_pin_detach_irq(pin);
                _irq_state[i].in_use = false;
                _irq_state[i].isr_fn = nullptr;
                _irq_state[i].simple_fn = nullptr;
                return true;
            }
        }
        return fn == nullptr;
    }

    IRQState *st = _find_or_alloc_irq(pin);
    if (!st) return false;

    st->isr_fn = nullptr;
    st->simple_fn = fn;

    rt_pin_mode(pin, PIN_MODE_INPUT);
    rt_pin_attach_irq(pin, _to_rtt_irq_mode(mode), _irq_trampoline, st);
    rt_pin_irq_enable(pin, PIN_IRQ_ENABLE);
    return true;
}
