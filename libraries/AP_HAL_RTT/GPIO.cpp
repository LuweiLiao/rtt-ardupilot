/*
 * AP_HAL_RTT — GPIO driver implementation
 * Pure CMSIS register access for all GPIO operations.
 * Pin numbers follow GET_PIN(port,bit) = port*16 + bit.
 * EXTI interrupt support retained through RT-Thread pin device framework
 * (OS-level NVIC service, not GPIO data path).
 *
 * Reference: ChibiOS GPIOv3/hal_pal_lld.c and stm32_gpio.h
 *   _pal_lld_setgroupmode()  — hal_pal_lld.c:89-143 (MODER/OTYPER/OSPEEDR/PUPDR/AF)
 *   _pal_lld_enablepadevent() — hal_pal_lld.c:156-196 (SYSCFG_EXTICR/EXTI)
 */

#include "GPIO.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <drivers/dev_pin.h>
#include <cstdio>

/* CMSIS header provides GPIO_TypeDef, GPIOA_BASE, __DSB() */
#include <stm32f7xx.h>

/*
 * Internal helpers — convert RT-Thread pin numbering
 * (port*16 + bit) to port index and bit position.
 */
static inline uint8_t _rtt_pin_port(uint8_t pin)
{
    return pin / 16;
}
static inline uint8_t _rtt_pin_bit(uint8_t pin)
{
    return pin % 16;
}
static inline GPIO_TypeDef *_rtt_pin_gpio(uint8_t pin)
{
    return (GPIO_TypeDef *)(GPIOA_BASE + _rtt_pin_port(pin) * 0x400UL);
}

/* Maximum valid pin for STM32F7 (11 ports A-K × 16 pins) */
#define RTT_GPIO_PIN_MAX (11U * 16U)

/* SWD pins: PA13 = SWDIO, PA14 = SWCLK (must never be changed) */
#define RTT_GPIO_PIN_SWDIO 13
#define RTT_GPIO_PIN_SWCLK 14

/* Default speed for all GPIO outputs: high speed (10 = 50MHz) */
#define RTT_GPIO_OSPEED_DEFAULT (2U)

using namespace RTT;

/* DigitalSource */

DigitalSource::DigitalSource(uint16_t pin) : _pin(pin) {}

void DigitalSource::mode(uint8_t output)
{
    if (_pin >= RTT_GPIO_PIN_MAX) {
        return;
    }
    GPIO_TypeDef *gpio = _rtt_pin_gpio((uint8_t)_pin);
    uint8_t bit = _rtt_pin_bit((uint8_t)_pin);
    uint32_t mask2 = 3UL << (bit * 2);

    if (output) {
        /*
         * ChibiOS _pal_lld_setgroupmode() order (hal_pal_lld.c:109-125):
         * OTYPER → OSPEEDR → PUPDR → MODER (non-alternate path).
         * Retain OPENDRAIN if already set (mirrors ChibiOS L222-228).
         */
        if (gpio->OTYPER & (1UL << bit)) {
            /* open-drain: keep OTYPER=1 */
        } else {
            gpio->OTYPER &= ~(1UL << bit);
        }
        gpio->OSPEEDR = (gpio->OSPEEDR & ~mask2) | (RTT_GPIO_OSPEED_DEFAULT << (bit * 2));
        gpio->PUPDR   = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));   /* no pull */
        gpio->MODER   = (gpio->MODER & ~mask2) | (1U << (bit * 2));   /* output */
    } else {
        gpio->PUPDR = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));   /* no pull */
        gpio->MODER = (gpio->MODER & ~mask2) | (0U << (bit * 2));   /* input */
    }
    __DSB();
}

uint8_t DigitalSource::read()
{
    if (_pin >= RTT_GPIO_PIN_MAX) {
        return 0;
    }
    GPIO_TypeDef *gpio = _rtt_pin_gpio((uint8_t)_pin);
    uint8_t bit = _rtt_pin_bit((uint8_t)_pin);
    return (gpio->IDR >> bit) & 1U;
}

void DigitalSource::write(uint8_t value)
{
    if (_pin >= RTT_GPIO_PIN_MAX) {
        return;
    }
    GPIO_TypeDef *gpio = _rtt_pin_gpio((uint8_t)_pin);
    uint8_t bit = _rtt_pin_bit((uint8_t)_pin);
    if (value) {
        gpio->BSRR = 1UL << bit;          /* set ODR HIGH */
    } else {
        gpio->BSRR = 1UL << (bit + 16);   /* reset ODR LOW */
    }
}

void DigitalSource::toggle()
{
    if (_pin >= RTT_GPIO_PIN_MAX) {
        return;
    }
    GPIO_TypeDef *gpio = _rtt_pin_gpio((uint8_t)_pin);
    uint8_t bit = _rtt_pin_bit((uint8_t)_pin);
    /*
     * Atomic toggle via ODR XOR (same as STM32 HAL_GPIO_TogglePin).
     * Volatile access ensures compiler does not optimize the RMW.
     */
    gpio->ODR ^= (1UL << bit);
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
        uint8_t pin = (uint8_t)e.pin;
        if (pin >= RTT_GPIO_PIN_MAX) {
            continue;
        }
        /* Skip SWD pins — never touch PA13/PA14 */
        if (pin == RTT_GPIO_PIN_SWDIO || pin == RTT_GPIO_PIN_SWCLK) {
            continue;
        }
        GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
        uint8_t bit = _rtt_pin_bit(pin);
        uint32_t mask2 = 3UL << (bit * 2);
        /* ChibiOS order: OTYPER → OSPEEDR → PUPDR → MODER */
        gpio->OTYPER &= ~(1UL << bit);              /* push-pull */
        gpio->OSPEEDR = (gpio->OSPEEDR & ~mask2) | (RTT_GPIO_OSPEED_DEFAULT << (bit * 2));
        gpio->PUPDR   = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));   /* no pull */
        gpio->MODER   = (gpio->MODER & ~mask2) | (1U << (bit * 2));   /* output */
        __DSB();
        /* Set initial output value via BSRR */
        if (e.init_value) {
            gpio->BSRR = 1UL << bit;
        } else {
            gpio->BSRR = 1UL << (bit + 16);
        }
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
    if (pin >= RTT_GPIO_PIN_MAX) {
        return;
    }
    /* SWD pin protection: PA13(SWDIO) and PA14(SWCLK) must keep default mode */
    if (pin == RTT_GPIO_PIN_SWDIO || pin == RTT_GPIO_PIN_SWCLK) {
        return;
    }

    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    uint32_t mask2 = 3UL << (bit * 2);

    if (output == HAL_GPIO_INPUT) {
        gpio->PUPDR = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));   /* no pull */
        gpio->MODER = (gpio->MODER & ~mask2) | (0U << (bit * 2));   /* input */
    } else {
        /*
         * Retain OPENDRAIN if already set (mirrors ChibiOS behavior on
         * STM32F7/H7/F4/G4/L4).  Read OTYPER directly to check.
         * ChibiOS reference: GPIO.cpp:221-228
         */
        if ((gpio->OTYPER >> bit) & 0x01U) {
            /* Keep open-drain OTYPER=1, set MODER=output */
            gpio->OSPEEDR = (gpio->OSPEEDR & ~mask2) | (RTT_GPIO_OSPEED_DEFAULT << (bit * 2));
            gpio->PUPDR   = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));
            gpio->MODER   = (gpio->MODER & ~mask2) | (1U << (bit * 2));
        } else {
            /* Push-pull: OTYPER=0 */
            gpio->OTYPER &= ~(1UL << bit);
            gpio->OSPEEDR = (gpio->OSPEEDR & ~mask2) | (RTT_GPIO_OSPEED_DEFAULT << (bit * 2));
            gpio->PUPDR   = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));
            gpio->MODER   = (gpio->MODER & ~mask2) | (1U << (bit * 2));
        }
    }
    __DSB();
}

void GPIO::pinMode(uint8_t pin, uint8_t output, uint8_t alt)
{
    if (pin >= RTT_GPIO_PIN_MAX) {
        return;
    }
    /* SWD pin protection */
    if (pin == RTT_GPIO_PIN_SWDIO || pin == RTT_GPIO_PIN_SWCLK) {
        return;
    }

    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    uint32_t mask2 = 3UL << (bit * 2);

    if (output == HAL_GPIO_INPUT) {
        gpio->PUPDR = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));
        gpio->MODER = (gpio->MODER & ~mask2) | (0U << (bit * 2));
    } else {
        /*
         * Alternate function configuration.
         * ChibiOS _pal_lld_setgroupmode() AF path (L113-120):
         *   AFRL/AFRH → MODER (alternate mode set AFTER AFR to avoid glitches).
         */
        if (bit < 8) {
            gpio->AFR[0] = (gpio->AFR[0] & ~(0xFUL << (bit * 4))) | ((uint32_t)alt << (bit * 4));
        } else {
            gpio->AFR[1] = (gpio->AFR[1] & ~(0xFUL << ((bit - 8) * 4))) | ((uint32_t)alt << ((bit - 8) * 4));
        }
        gpio->OTYPER &= ~(1UL << bit);           /* push-pull */
        gpio->OSPEEDR = (gpio->OSPEEDR & ~mask2) | (RTT_GPIO_OSPEED_DEFAULT << (bit * 2));
        gpio->PUPDR   = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));
        gpio->MODER   = (gpio->MODER & ~mask2) | (2U << (bit * 2));   /* alternate function */
    }
    __DSB();
}

uint8_t GPIO::read(uint8_t pin)
{
    if (pin >= RTT_GPIO_PIN_MAX) {
        return 0;
    }
    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    return (gpio->IDR >> bit) & 1U;
}

void GPIO::write(uint8_t pin, uint8_t value)
{
    if (pin >= RTT_GPIO_PIN_MAX) {
        return;
    }

#if AP_NOTIFY_GPIO_LED_RGB_ENABLED && defined(AP_NOTIFY_GPIO_LED_RGB_RED_PIN)
    /* RGB LED pins on CUAV V5: PH10(R) / PH11(G) / PH12(B), active-low.
     * ChibiOS configures these as OPENDRAIN. The LED turns ON when pin is
     * LOW (open-drain sinks current). Write BSRR directly.
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

    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    uint32_t mask2 = 3UL << (bit * 2);

    /*
     * ChibiOS semantics (GPIO.cpp:254-263): writing to an input-configured
     * pin controls pull-up/pull-down resistors.  Read MODER to check.
     */
    uint32_t mode = (gpio->MODER >> (bit * 2)) & 0x03;
    if (mode == 0) {
        /* Pin is in INPUT mode — set pull-up/pull-down via PUPDR */
        gpio->PUPDR = (gpio->PUPDR & ~mask2) | ((value ? 1U : 2U) << (bit * 2));
        __DSB();
        return;
    }

    /* Output mode — atomically set/reset via BSRR */
    if (value) {
        gpio->BSRR = 1UL << bit;
    } else {
        gpio->BSRR = 1UL << (bit + 16);
    }
}

void GPIO::toggle(uint8_t pin)
{
    if (pin >= RTT_GPIO_PIN_MAX) {
        return;
    }
    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    /* Atomically toggle via ODR XOR (same as STM32 HAL_GPIO_TogglePin) */
    gpio->ODR ^= (1UL << bit);
}

AP_HAL::DigitalSource* GPIO::channel(uint16_t n)
{
    return NEW_NOTHROW DigitalSource(n);
}

#include "hal_usb_lld_rtt.h"

bool GPIO::usb_connected()
{
    return usb_lld_get_connected_rtt();
}

/* --- Interrupt support ------------------------------------------ */

/*
 * EXTI interrupt handling is retained through the RT-Thread pin device
 * framework (rt_pin_attach_irq / rt_pin_irq_enable / rt_pin_detach_irq).
 * This is an OS-level NVIC service, not a GPIO data-path operation.
 * The ChibiOS equivalent (pal_lld_enablepadevent) also programs EXTI
 * registers directly, but RT-Thread handles the NVIC ISR routing and
 * callback dispatch internally.  Keeping this as an OS-level boundary
 * is the correct HAL isolation choice (see ADR-005).
 *
 * The _irq_trampoline still uses CMSIS IDR register for the pin state
 * read (replacing rt_pin_read).
 */

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

    /*
     * Read pin state from IDR directly (not rt_pin_read).
     */
    if (st->pin < RTT_GPIO_PIN_MAX) {
        GPIO_TypeDef *gpio = _rtt_pin_gpio(st->pin);
        uint8_t bit = _rtt_pin_bit(st->pin);
        bool state = (gpio->IDR >> bit) & 1U;
        uint32_t ts = AP_HAL::micros();

        if (st->isr_fn) {
            st->isr_fn(st->pin, state, ts);
        } else if (st->simple_fn) {
            st->simple_fn();
        }
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

    /*
     * Set pin to input mode via CMSIS before attaching interrupt.
     * This matches ChibiOS behavior where EXTI requires input mode.
     */
    if (pin < RTT_GPIO_PIN_MAX) {
        GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
        uint8_t bit = _rtt_pin_bit(pin);
        uint32_t mask2 = 3UL << (bit * 2);
        gpio->PUPDR = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));   /* no pull */
        gpio->MODER = (gpio->MODER & ~mask2) | (0U << (bit * 2));   /* input */
        __DSB();
    }

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

    /*
     * Set pin to input mode via CMSIS before attaching interrupt.
     */
    if (pin < RTT_GPIO_PIN_MAX) {
        GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
        uint8_t bit = _rtt_pin_bit(pin);
        uint32_t mask2 = 3UL << (bit * 2);
        gpio->PUPDR = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));
        gpio->MODER = (gpio->MODER & ~mask2) | (0U << (bit * 2));
        __DSB();
    }

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

    /* Set pin to input mode via CMSIS */
    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    uint32_t mask2 = 3UL << (bit * 2);
    gpio->PUPDR = (gpio->PUPDR & ~mask2) | (0U << (bit * 2));
    gpio->MODER = (gpio->MODER & ~mask2) | (0U << (bit * 2));
    __DSB();

    uint8_t initial = (gpio->IDR >> bit) & 1U;
    uint64_t start = AP_HAL::micros64();

    while (true) {
        uint8_t current = (gpio->IDR >> bit) & 1U;
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
    if (pin >= RTT_GPIO_PIN_MAX) return false;
    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    mode = (gpio->MODER >> (bit * 2)) & 0x03;
    return true;
}

void GPIO::set_mode(uint8_t pin, uint32_t mode)
{
    if (pin >= RTT_GPIO_PIN_MAX) return;
    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    uint32_t val = gpio->MODER;
    val &= ~(0x03U << (bit * 2));
    val |= (mode & 0x03U) << (bit * 2);
    gpio->MODER = val;
    __DSB();
}

/*
 * AF configuration helper — programs GPIOx_AFRL/AFRH.
 * ChibiOS reference: _pal_lld_setgroupmode() L105-129 (hal_pal_lld.c).
 */
void GPIO::set_af(uint8_t pin, uint8_t af_num)
{
    if (pin >= RTT_GPIO_PIN_MAX) return;
    /* SWD pin protection */
    if (pin == RTT_GPIO_PIN_SWDIO || pin == RTT_GPIO_PIN_SWCLK) return;

    GPIO_TypeDef *gpio = _rtt_pin_gpio(pin);
    uint8_t bit = _rtt_pin_bit(pin);
    uint32_t mask4 = 0xFUL << ((bit & 7) * 4);

    if (bit < 8) {
        gpio->AFR[0] = (gpio->AFR[0] & ~mask4) | ((uint32_t)af_num << (bit * 4));
    } else {
        gpio->AFR[1] = (gpio->AFR[1] & ~mask4) | ((uint32_t)af_num << ((bit - 8) * 4));
    }
}
