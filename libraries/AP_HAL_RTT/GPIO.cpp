/*
 * AP_HAL_RTT — GPIO driver implementation
 * Uses RT-Thread rt_pin_* API. Pin numbers follow GET_PIN() convention.
 * Interrupt support via rt_pin_attach_irq / rt_pin_irq_enable.
 */

#include "GPIO.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <drivers/dev_pin.h>
#include <cstdio>

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

    st->isr_count++;

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

/*
 * valid_pin — STM32F7: pin numbers 0–175 (11 ports × 16 pins)
 * RT-Thread uses GET_PIN(port, num) = port*16 + num
 */
bool GPIO::valid_pin(uint8_t pin) const
{
    return pin < 176;
}

/*
 * pin_to_servo_channel — CUAV V5 FMU outputs:
 *   PE14=CH1, PA10=CH2, PE11=CH3, PE9=CH4,
 *   PD13=CH5, PD14=CH6
 * Pin numbers via GET_PIN(port, num)
 */
bool GPIO::pin_to_servo_channel(uint8_t pin, uint8_t &servo_ch) const
{
    /* Compact lookup: only the 6 main FMU outputs */
    static const struct { uint8_t gpio_pin; uint8_t ch; } map[] = {
        { 4*16+14, 0 },  // PE14 → CH1
        { 0*16+10, 1 },  // PA10 → CH2
        { 4*16+11, 2 },  // PE11 → CH3
        { 4*16+9,  3 },  // PE9  → CH4
        { 3*16+13, 4 },  // PD13 → CH5
        { 3*16+14, 5 },  // PD14 → CH6
    };
    for (const auto &m : map) {
        if (m.gpio_pin == pin) {
            servo_ch = m.ch;
            return true;
        }
    }
    return false;
}

/*
 * wait_pin — block until pin changes (or timeout).
 * Uses polling with OS sleep to avoid burning CPU.
 */
bool GPIO::wait_pin(uint8_t pin, INTERRUPT_TRIGGER_TYPE mode, uint32_t timeout_us)
{
    if (!valid_pin(pin)) return false;

    rt_pin_mode(pin, PIN_MODE_INPUT);
    uint8_t initial = read(pin);
    uint64_t start = AP_HAL::micros64();

    while (true) {
        uint8_t current = read(pin);
        bool triggered = false;
        switch (mode) {
        case INTERRUPT_RISING:   triggered = (current && !initial); break;
        case INTERRUPT_FALLING:  triggered = (!current && initial); break;
        case INTERRUPT_BOTH:     triggered = (current != initial); break;
        default: return false;
        }
        if (triggered) return true;
        initial = current;

        if (timeout_us != 0 && (AP_HAL::micros64() - start) >= timeout_us) {
            return false;
        }
        rt_thread_mdelay(1);
    }
}

/*
 * timer_tick — called from monitor thread at 10 Hz.
 * Tracks ISR counts for flood detection (mirrors ChibiOS).
 */
void GPIO::timer_tick(void)
{
    const uint32_t ISR_FLOOD_THRESHOLD = 1000;
    _isr_flood_detected = false;

    for (uint8_t i = 0; i < RTT_GPIO_MAX_IRQ; i++) {
        if (!_irq_state[i].in_use) continue;
        uint32_t delta = _irq_state[i].isr_count - _irq_state[i].last_isr_count;
        _irq_state[i].last_isr_count = _irq_state[i].isr_count;
        if (delta > ISR_FLOOD_THRESHOLD) {
            _isr_flood_detected = true;
        }
    }
}

bool GPIO::arming_checks(size_t buflen, char *buffer) const
{
    if (_isr_flood_detected) {
        if (buflen > 0 && buffer) {
            snprintf(buffer, buflen, "GPIO ISR flood detected");
        }
        return false;
    }
    return true;
}

/*
 * get_mode / set_mode — read/write STM32 MODER register directly.
 * Pin number is RT-Thread convention: port*16 + bit.
 * Returns raw MODER 2-bit field: 0=input, 1=output, 2=AF, 3=analog.
 */
#ifndef GPIOA_BASE
#define GPIOA_BASE 0x40020000UL
#endif
#define _GPIO_PORT_BASE(port) (GPIOA_BASE + (port) * 0x0400UL)

bool GPIO::get_mode(uint8_t pin, uint32_t &mode)
{
    if (pin >= 176) return false;
    uint8_t port = pin / 16;
    uint8_t bit  = pin % 16;
    volatile uint32_t *moder = (volatile uint32_t *)_GPIO_PORT_BASE(port);
    mode = (*moder >> (bit * 2)) & 0x03;
    return true;
}

void GPIO::set_mode(uint8_t pin, uint32_t mode)
{
    if (pin >= 176) return;
    uint8_t port = pin / 16;
    uint8_t bit  = pin % 16;
    volatile uint32_t *moder = (volatile uint32_t *)_GPIO_PORT_BASE(port);
    uint32_t val = *moder;
    val &= ~(0x03U << (bit * 2));
    val |= (mode & 0x03U) << (bit * 2);
    *moder = val;
}
