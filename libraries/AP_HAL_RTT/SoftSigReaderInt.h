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
 * RTT port — TIM input capture soft signal reader (interrupt variant)
 * ADR-005: Replaces ChibiOS EICU abstraction with direct CMSIS TIM access.
 *           ChibiOS uses EICU abstraction atop STM32 TIM input capture;
 *           RTT has no EICU equivalent, so we access TIM registers directly.
 */
#pragma once

#include <AP_HAL/utility/RingBuffer.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include "HAL_RTT_Namespace.h"

/* CMSIS device header for TIM_TypeDef, NVIC, RCC registers */
#include <stm32f7xx.h>

#include <stdint.h>

#ifndef SOFTSIG_MAX_SIGNAL_TRANSITIONS
#define SOFTSIG_MAX_SIGNAL_TRANSITIONS 128
#endif

#define INPUT_CAPTURE_FREQUENCY 1000000  // 1 MHz capture tick

namespace RTT
{

class SoftSigReaderInt
{
public:
    SoftSigReaderInt();

    CLASS_NO_COPY(SoftSigReaderInt);

    static SoftSigReaderInt *get_singleton(void)
    {
        return _singleton;
    }

    /*
     * Initialise TIM input capture on the given timer + channel pair.
     * @param tim    pointer to the TIM peripheral (e.g. TIM1, TIM2, ...)
     * @param chan   capture channel (0-based: 0=CH1, 1=CH2, 2=CH3, 3=CH4)
     * @param irq_n  NVIC IRQ number (e.g. TIM1_CC_IRQn)
     */
    void init(TIM_TypeDef *tim, uint8_t chan, IRQn_Type irq_n);

    /*
     * Read one captured pulse pair.
     * Returns true when data is available, filling:
     *   widths0 — period (falling-to-falling)
     *   widths1 — pulse width (falling-to-rising)
     */
    bool read(uint32_t &widths0, uint32_t &widths1);

    void disable(void);

    /*
     * IRQ entry point — called from soft_sig_reader_irq_handler()
     * (C-linkage wrapper for NVIC dispatch). Public because it's
     * invoked from C code outside the class.
     */
    static void _irq_handler(void);

private:
    /* Read CCR register for 0-based channel index */
    static inline uint32_t ccr(TIM_TypeDef *tim, uint8_t ch)
    {
        switch (ch) {
        case 0: return tim->CCR1;
        case 1: return tim->CCR2;
        case 2: return tim->CCR3;
        case 3: return tim->CCR4;
        default: return 0;
        }
    }

    /* Get CCMR register pointer for 0-based channel index */
    static inline volatile uint32_t *ccmr(TIM_TypeDef *tim, uint8_t ch)
    {
        return (ch < 2) ? &tim->CCMR1 : &tim->CCMR2;
    }

    static SoftSigReaderInt *_singleton;

    TIM_TypeDef *_tim;
    uint8_t      _main_channel;
    uint8_t      _aux_channel;
    IRQn_Type    _irq_n;

    typedef struct PACKED {
        uint16_t w0;   // falling edge timestamp
        uint16_t w1;   // rising edge timestamp
    } pulse_t;

    ObjectBuffer<pulse_t> sigbuf{SOFTSIG_MAX_SIGNAL_TRANSITIONS};

    uint16_t  last_value;

    static uint8_t get_pair_channel(uint8_t chan);
    static uint32_t get_tim_clk(TIM_TypeDef *tim);
};

} // namespace RTT

/*
 * C-linkage IRQ handler for NVIC dispatch — outside the namespace.
 * Implemented in SoftSigReaderInt.cpp.
 */
extern "C" void soft_sig_reader_irq_handler(void);
