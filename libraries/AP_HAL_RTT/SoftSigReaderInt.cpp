/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * RTT port — GPIO interrupt-based soft signal reader (interrupt variant)
 *
 * Replaces ChibiOS EICU (Enhanced Input Capture Unit) hardware timer
 * capture with GPIO EXTI interrupts + DWT cycle counter for
 * microsecond-resolution RC signal decoding.
 *
 * Architecture:
 *   - attaches an RT-Thread pin interrupt (both rising and falling edges)
 *   - in the ISR: reads DWT_CYCCNT directly for high-precision timestamps
 *   - accumulates rising-edge (w0) and falling-edge (w1) 16-bit timestamps
 *   - when a complete pulse pair is captured, pushes it to the ring buffer
 *   - read() computes period (w0 - last_value) and pulse width (w1 - w0)
 *     with uint16_t modulo arithmetic matching the ChibiOS EICU semantics
 */

#include "SoftSigReaderInt.h"

#include <AP_HAL/AP_HAL.h>
#include <drivers/dev_pin.h>

#include <stm32f7xx.h>

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT

using namespace RTT;
extern const AP_HAL::HAL& hal;

/* DWT Cycle Counter register — read directly in ISR for speed */
#define DWT_CYCCNT_REG  (*(volatile uint32_t *)0xE0001004)

/* singleton instance */
SoftSigReaderInt *SoftSigReaderInt::_singleton;

SoftSigReaderInt::SoftSigReaderInt()
    : _pin(0)
    , _last_value(0)
    , _last_state(0)
    , _last_time(0)
    , _initialised(false)
{
    _current_pulse.w0 = 0;
    _current_pulse.w1 = 0;
    _singleton = this;
}

/*
  IRQ handler — called on every rising and falling edge of the RCIN pin.
  Pin level is read directly from the GPIO IDR register for speed.
  Timestamps use DWT CYCCNT divided by (cpu_freq / 1MHz) for us resolution,
  then truncated to 16 bits to match the pulse_t storage format.
 */
void SoftSigReaderInt::_irq_handler(void *arg)
{
    SoftSigReaderInt *self = (SoftSigReaderInt *)arg;
    if (!self || !self->_initialised) {
        return;
    }

    /* Read current pin level from GPIO IDR — faster than rt_pin_read() */
    GPIO_TypeDef *gpio = (GPIO_TypeDef *)(GPIOA_BASE + 0x400U * (self->_pin >> 4));
    uint32_t pin_bit = 1U << (self->_pin & 0x0F);
    int current_state = (gpio->IDR & pin_bit) ? 1 : 0;

    /* Read DWT cycle counter and convert to microseconds */
    static uint32_t us_div = 0;
    if (us_div == 0) {
        /* SystemCoreClock is set once by SystemClock_Config before main() */
        us_div = SystemCoreClock / 1000000U;
    }
    uint32_t now = DWT_CYCCNT_REG / us_div;

    /*
     * Edge detection: only process when the pin level has actually changed.
     * This filters out any spurious same-level re-triggers from the EXTI
     * controller.
     */
    if (self->_last_state != current_state) {
        if (self->_last_state == 0) {
            /* Rising edge — start of a new pulse */
            self->_current_pulse.w0 = (uint16_t)(now & 0xFFFF);
        } else {
            /* Falling edge — end of the pulse */
            self->_current_pulse.w1 = (uint16_t)(now & 0xFFFF);
        }
        self->_last_state = current_state;
        self->_last_time = now;

        /* If both edges captured, push the completed pulse to the ring buffer */
        if (self->_current_pulse.w0 != 0 && self->_current_pulse.w1 != 0) {
            self->sigbuf.push(self->_current_pulse);
            self->_current_pulse.w0 = 0;
            self->_current_pulse.w1 = 0;
        }
    }

    /*
     * Detect missed pulses via timeout.
     * If more than 50ms have elapsed since the last edge without completing
     * a pulse, reset the accumulated state and push a zero-width pulse to
     * signal the protocol parser to reset.
     */
    if ((now - self->_last_time) > 50000) {
        if (self->_current_pulse.w0 != 0) {
            /* Incomplete pulse — push a reset marker */
            pulse_t reset_pulse = {0, 0};
            self->sigbuf.push(reset_pulse);
            self->_current_pulse.w0 = 0;
            self->_current_pulse.w1 = 0;
        }
        self->_last_time = now;
    }

    /*
     * Check for missed overflow events by comparing against elapsed time.
     * This is the software equivalent of STM32_TIM_SR_CC1OF checking in the
     * ChibiOS EICU handler.  If we went more than 2 * typical servo frame
     * without seeing a valid pair, inject a reset pulse.
     */
    if (self->sigbuf.available() == 0) {
        uint32_t elapsed = now - self->_last_time;
        if (elapsed > 40000) {
            pulse_t reset_pulse = {0, 0};
            self->sigbuf.push(reset_pulse);
            self->_last_time = now;
        }
    }
}

void SoftSigReaderInt::init(rt_base_t pin)
{
    if (_initialised) {
        disable();
    }

    _pin = pin;
    _last_value = 0;
    _last_state = 0;
    _last_time = 0;
    _current_pulse.w0 = 0;
    _current_pulse.w1 = 0;

    /* Configure GPIO as input with pull-up */
    rt_pin_mode(_pin, PIN_MODE_INPUT);
    rt_pin_write(_pin, PIN_HIGH);

    /* Verify DWT is enabled (should have been done by board init) */
    if (!(*(volatile uint32_t *)0xE0001000 & 1)) {
        /* Re-init DWT if not already running */
        *(volatile uint32_t *)0xE000EDFC |= (1U << 24);   /* DEMCR: enable DWT */
        *(volatile uint32_t *)0xE0001FB0 = 0xC5ACCE55;    /* DWT_LAR unlock (Cortex-M7) */
        DWT_CYCCNT_REG = 0U;
        *(volatile uint32_t *)0xE0001000 |= 1U;            /* DWT_CTRL: enable CYCCNT */
    }

    /* Attach interrupt on both edges */
    rt_pin_attach_irq(_pin, PIN_IRQ_MODE_RISING_FALLING, _irq_handler, this);
    rt_pin_irq_enable(_pin, PIN_IRQ_ENABLE);

    _initialised = true;
}

void SoftSigReaderInt::disable(void)
{
    if (!_initialised) {
        return;
    }
    rt_pin_irq_enable(_pin, PIN_IRQ_DISABLE);
    rt_pin_detach_irq(_pin);
    _initialised = false;
}

bool SoftSigReaderInt::read(uint32_t &widths0, uint32_t &widths1)
{
    if (sigbuf.available() >= 2) {
        pulse_t pulse;
        if (sigbuf.pop(pulse)) {
            widths0 = uint16_t(pulse.w0 - _last_value);
            widths1 = uint16_t(pulse.w1 - pulse.w0);
            _last_value = pulse.w1;
            return true;
        }
    }
    return false;
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTT
