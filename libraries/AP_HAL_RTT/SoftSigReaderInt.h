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
 * Replaces ChibiOS EICU input capture with GPIO EXTI + DWT cycle counter
 * for microsecond-resolution RC signal decoding.
 */
#pragma once

#include <AP_HAL/utility/RingBuffer.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include "HAL_RTT_Namespace.h"

#include <rtthread.h>

#ifndef SOFTSIG_MAX_SIGNAL_TRANSITIONS
#define SOFTSIG_MAX_SIGNAL_TRANSITIONS 128
#endif

namespace RTT
{

class SoftSigReaderInt
{
public:
    SoftSigReaderInt();
    /* Do not allow copies */
    CLASS_NO_COPY(SoftSigReaderInt);

    // get singleton
    static SoftSigReaderInt *get_singleton(void)
    {
        return _singleton;
    }

    /*
     * Initialise pulse capture on the given GPIO pin.
     * @param pin  RT-Thread pin number (e.g. GET_PIN('C', 6) for PC6)
     */
    void init(rt_base_t pin);

    /*
     * Read one captured pulse pair.
     * Returns true when data is available, filling widths0 (period)
     * and widths1 (pulse width) in microseconds.
     */
    bool read(uint32_t &widths0, uint32_t &widths1);

    /* Disable pulse capture and detach the interrupt */
    void disable(void);

private:
    // singleton
    static SoftSigReaderInt *_singleton;

    /* Pulse edge state — tracked inside ISR */
    static void _irq_handler(void *arg);

    typedef struct PACKED {
        uint16_t w0;   // timestamp of rising edge (mod 2^16 us)
        uint16_t w1;   // timestamp of falling edge (mod 2^16 us)
    } pulse_t;

    ObjectBuffer<pulse_t> sigbuf{SOFTSIG_MAX_SIGNAL_TRANSITIONS};

    /* Pin and state */
    rt_base_t _pin;
    uint16_t  _last_value;

    /* ISR state: held per-instance, accessed by static handler */
    int  _last_state;       // 0 or 1 — last known pin level
    uint32_t _last_time;    // raw DWT timestamp of last edge
    pulse_t _current_pulse; // in-progress pulse accumulation
    bool _initialised;
};

} // namespace RTT
