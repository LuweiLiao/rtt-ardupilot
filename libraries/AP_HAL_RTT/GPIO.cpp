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

#if AP_NOTIFY_GPIO_LED_RGB_ENABLED && defined(AP_NOTIFY_GPIO_LED_RGB_RED_PIN)
#include <stm32f7xx_hal.h>
#endif

/*
 * GPIO register-level helpers.
 * _GPIO_PORT_BASE computes the peripheral base address for a port index:
 *   port 0 = GPIOA (0x40020000), port 1 = GPIOB (0x40020400), etc.
 * Must be defined before any function using it (pinMode OTYPER check).
 */
#ifndef GPIOA_BASE
#define GPIOA_BASE 0x40020000UL
#endif
#define _GPIO_PORT_BASE(port) (GPIOA_BASE + (port) * 0x0400UL)

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
    /*
     * Power-on sensor rails and other output GPIOs with INIT=1 from hwdef.h.
     * This MUST run before any SPI/I2C sensor communication.
     *
     * Uses the _VALUE macros (precomputed integer pin numbers) available
     * from hwdef.h.  _PIN macros use GET_PIN() which requires BSP headers
     * not always in the include chain from AP_HAL code.
     */
    struct gpio_init_entry {
        rt_base_t pin;
        uint8_t   init_value;  /* 0=LOW, 1=HIGH */
    };
    static const gpio_init_entry init_list[] = {
#ifdef HAL_GPIO_VDD_3V3_SENSORS_EN_VALUE
        { HAL_GPIO_VDD_3V3_SENSORS_EN_VALUE,
#ifdef HAL_GPIO_VDD_3V3_SENSORS_EN_INIT
          HAL_GPIO_VDD_3V3_SENSORS_EN_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_VDD_5V_RC_EN_VALUE
        { HAL_GPIO_VDD_5V_RC_EN_VALUE,
#ifdef HAL_GPIO_VDD_5V_RC_EN_INIT
          HAL_GPIO_VDD_5V_RC_EN_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_VDD_3V3_SD_CARD_EN_VALUE
        { HAL_GPIO_VDD_3V3_SD_CARD_EN_VALUE,
#ifdef HAL_GPIO_VDD_3V3_SD_CARD_EN_INIT
          HAL_GPIO_VDD_3V3_SD_CARD_EN_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_VDD_5V_WIFI_EN_VALUE
        { HAL_GPIO_VDD_5V_WIFI_EN_VALUE,
#ifdef HAL_GPIO_VDD_5V_WIFI_EN_INIT
          HAL_GPIO_VDD_5V_WIFI_EN_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_SPEKTRUM_PWR_VALUE
        { HAL_GPIO_SPEKTRUM_PWR_VALUE,
#ifdef HAL_GPIO_SPEKTRUM_PWR_INIT
          HAL_GPIO_SPEKTRUM_PWR_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_nVDD_5V_HIPOWER_EN_VALUE
        { HAL_GPIO_nVDD_5V_HIPOWER_EN_VALUE,
#ifdef HAL_GPIO_nVDD_5V_HIPOWER_EN_INIT
          HAL_GPIO_nVDD_5V_HIPOWER_EN_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_nVDD_5V_PERIPH_EN_VALUE
        { HAL_GPIO_nVDD_5V_PERIPH_EN_VALUE,
#ifdef HAL_GPIO_nVDD_5V_PERIPH_EN_INIT
          HAL_GPIO_nVDD_5V_PERIPH_EN_INIT
#else
          1
#endif
        },
#endif
#ifdef HAL_GPIO_nSPI5_RESET_EXTERNAL1_VALUE
        { HAL_GPIO_nSPI5_RESET_EXTERNAL1_VALUE,
#ifdef HAL_GPIO_nSPI5_RESET_EXTERNAL1_INIT
          HAL_GPIO_nSPI5_RESET_EXTERNAL1_INIT
#else
          1
#endif
        },
#endif
    };

    for (const auto &e : init_list) {
        rt_pin_mode(e.pin, PIN_MODE_OUTPUT);
        rt_pin_write(e.pin, e.init_value ? PIN_HIGH : PIN_LOW);
    }

    /* RGB LED GPIO (PH10/11/12) MODER is set lazily in write() on first use.
     * Cannot set it here because TIM12 HAL_GPIO_Init (stm32f7xx_hal_msp.c)
     * does read-modify-write on GPIOH->MODER AFTER this point, clobbering
     * PH10-12 OUTPUT configuration.  The lazy init in write() runs from the
     * AP_Notify thread, which is well past all board-level HAL inits.
     */
}

void GPIO::pinMode(uint8_t pin, uint8_t output)
{
    if (output == HAL_GPIO_INPUT) {
        rt_pin_mode(pin, PIN_MODE_INPUT);
    } else {
        /*
         * Retain OPENDRAIN if already set (mirrors ChibiOS behavior on
         * STM32F7/H7/F4/G4/L4).  Read OTYPER directly to check.
         */
        if (pin < 176) {
            uint8_t port = pin / 16;
            uint8_t bit  = pin % 16;
            volatile uint32_t *otyper =
                (volatile uint32_t *)(_GPIO_PORT_BASE(port) + 0x04U);
            if ((*otyper >> bit) & 0x01U) {
                rt_pin_mode(pin, PIN_MODE_OUTPUT_OD);
                return;
            }
        }
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
#if AP_NOTIFY_GPIO_LED_RGB_ENABLED && defined(AP_NOTIFY_GPIO_LED_RGB_RED_PIN)
    /* RGB LED pins on CUAV V5: PH10(R) / PH11(G) / PH12(B), active-low.
     * ChibiOS configures these as OPENDRAIN. The LED turns ON when pin is
     * LOW (open-drain sinks current). Bypass RTT pin driver, write BSRR
     * directly to avoid rt_pin_write() reliability issues on GPIOH.
     * BSRR: bits[15:0] set ODR (pin HIGH), bits[31:16] reset ODR (pin LOW).
     */
    if (pin == AP_NOTIFY_GPIO_LED_RGB_RED_PIN ||
        pin == AP_NOTIFY_GPIO_LED_RGB_GREEN_PIN ||
        pin == AP_NOTIFY_GPIO_LED_RGB_BLUE_PIN) {
        static bool led_moder_done = false;
        if (!led_moder_done) {
            __HAL_RCC_GPIOH_CLK_ENABLE();
            /* Set PH10/11/12 to OUTPUT (MODER bits 21:20, 23:22, 25:24 = 01) */
            GPIOH->MODER = (GPIOH->MODER & ~(0x3FUL << 20)) | (0x15UL << 20);
            /* Set PH10/11/12 to open-drain (OTYPER bits 10,11,12 = 1) */
            GPIOH->OTYPER |= (0x7UL << 10);
            /* Set ODR HIGH (LED off — open-drain HIGH = floating) */
            GPIOH->BSRR = (0x7UL << 10);
            led_moder_done = true;
        }
        /* Pin bit: PH10=10, PH11=11, PH12=12 — same as pin % 16 */
        uint8_t bit = pin % 16;
        if (value) {
            GPIOH->BSRR = 1UL << bit;       /* set ODR → pin HIGH → LED off */
        } else {
            GPIOH->BSRR = 1UL << (bit + 16); /* reset ODR → pin LOW → LED on */
        }
        return;
    }
#endif
    /*
     * ChibiOS semantics: writing to an input-configured pin controls
     * pull-up/pull-down resistors.  Read MODER to check current mode.
     */
    if (pin < 176) {
        uint8_t port = pin / 16;
        uint8_t bit  = pin % 16;
        volatile uint32_t *moder =
            (volatile uint32_t *)_GPIO_PORT_BASE(port);
        uint32_t mode = (*moder >> (bit * 2)) & 0x03;
        if (mode == 0) {
            /* Pin is in INPUT mode — set pull-up/pull-down via PUPDR
             * through rt_pin_mode instead of writing ODR (which is a
             * no-op for input pins on STM32). */
            rt_pin_mode(pin, value ? PIN_MODE_INPUT_PULLUP
                                   : PIN_MODE_INPUT_PULLDOWN);
            return;
        }
    }
    rt_pin_write(pin, value ? PIN_HIGH : PIN_LOW);
}

void GPIO::toggle(uint8_t pin)
{
    /* Atomically toggle via direct ODR XOR (same as STM32 HAL_GPIO_TogglePin).
     * Avoids non-atomic read-write cycle of write(pin, read(pin) ^ 1). */
    if (pin >= 176) {
        return;
    }
    uint8_t port = pin / 16;
    uint8_t bit  = pin % 16;
    volatile uint32_t *odr =
        (volatile uint32_t *)(_GPIO_PORT_BASE(port) + 0x14U);
    *odr ^= (1UL << bit);
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

    /* Reject double-attach (ChibiOS semantics: pin already has a handler) */
    if (st->isr_fn != nullptr || st->simple_fn != nullptr) {
        return false;
    }

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

    /* Reject double-attach (ChibiOS semantics: pin already has a handler) */
    if (st->isr_fn != nullptr || st->simple_fn != nullptr) {
        return false;
    }

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

    /* Clamp timeout: max 30ms to match ChibiOS constraint */
    if (timeout_us == 0 || timeout_us > 30000U) {
        timeout_us = 30000U;
    }

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
    __DSB(); /* Ensure MODER write is visible before subsequent operations */
}
