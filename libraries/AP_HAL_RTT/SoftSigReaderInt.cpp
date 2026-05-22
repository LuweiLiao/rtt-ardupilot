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
 *
 * Replaces ChibiOS EICU abstraction with direct STM32 TIM register access.
 * ChibiOS Reference: libraries/AP_HAL_ChibiOS/SoftSigReaderInt.cpp
 *
 * ADR-005: CMSIS TIM input capture chosen over GPIO EXTI+DWT for 1:1
 * behaviour matching ChibiOS (hardware-timed captures, no ISR latency).
 */

#include "SoftSigReaderInt.h"

#include <AP_HAL/AP_HAL.h>

/* CMSIS device header for TIM_TypeDef, NVIC APIs, RCC registers */
#include <stm32f7xx.h>

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT

using namespace RTT;
extern const AP_HAL::HAL& hal;

/* singleton instance */
SoftSigReaderInt *SoftSigReaderInt::_singleton;

SoftSigReaderInt::SoftSigReaderInt()
    : _tim(nullptr)
    , _main_channel(0)
    , _aux_channel(0)
    , _irq_n(TIM1_CC_IRQn)
    , last_value(0)
{
    _singleton = this;
}

uint8_t SoftSigReaderInt::get_pair_channel(uint8_t chan)
{
    /* ChibiOS EICU pair mapping: CH1↔CH2, CH3↔CH4 */
    switch (chan) {
        case 0: return 1;
        case 1: return 0;
        case 2: return 3;
        case 3: return 2;
        default: return 0;
    }
}

/*
 * Determine timer clock frequency.
 * STM32F767 (RM0430 §6.3.3):
 *   APB1 timer clock = PCLK1 × 2 if PPRE1 > 1
 *   APB2 timer clock = PCLK2 × 2 if PPRE2 > 1
 * TIM1, TIM8-11 on APB2; all others on APB1.
 */
uint32_t SoftSigReaderInt::get_tim_clk(TIM_TypeDef *tim)
{
    uint32_t cfgr = RCC->CFGR;
    uint32_t ppre1 = (cfgr >> 8) & 7;
    uint32_t ppre2 = (cfgr >> 11) & 7;

    uint32_t pclk, ppre;
    if (tim == TIM1 || tim == TIM8 || tim == TIM9 ||
        tim == TIM10 || tim == TIM11) {
        /* APB2: PCLK2 = HCLK */
        pclk = SystemCoreClock / ((1U << ppre2) >> 1);
        ppre = ppre2;
    } else {
        /* APB1: PCLK1 = HCLK / 4 */
        pclk = SystemCoreClock / ((1U << ppre1) >> 1);
        ppre = ppre1;
    }

    /* If APB prescaler > 1, timer clock = 2× PCLK */
    if (ppre >= 4) {
        return pclk * 2;
    }
    return pclk;
}

void SoftSigReaderInt::init(TIM_TypeDef *tim, uint8_t chan, IRQn_Type irq_n)
{
    last_value = 0;

    /* Store configuration */
    _tim = tim;
    _main_channel = chan;
    _aux_channel = get_pair_channel(chan);
    _irq_n = irq_n;

    /* Compute timer clock frequency */
    uint32_t _tim_clk = get_tim_clk(tim);

    /* Compute prescaler for 1 MHz capture tick */
    uint32_t psc = (_tim_clk / INPUT_CAPTURE_FREQUENCY) - 1;

    /*
     * Disable the timer during configuration.
     * ChibiOS: eicuStart() disables then enables the timer.
     */
    tim->CR1 &= ~TIM_CR1_CEN;

    /* Configure prescaler and auto-reload (0xFFFF for 16-bit timers) */
    tim->PSC = psc;
    tim->ARR = 0xFFFF;
    tim->EGR = TIM_EGR_UG;  /* Force update to reload PSC/ARR */

    /* Wait for update flag */
    while (!(tim->SR & TIM_SR_UIF));
    tim->SR &= ~TIM_SR_UIF;

    /* Clear capture-compare registers and flags */
    tim->CCR1 = 0;
    tim->CCR2 = 0;
    tim->CCR3 = 0;
    tim->CCR4 = 0;
    tim->SR = 0;

    /*
     * Configure main channel (falling edge capture, no IRQ).
     * Share data (ICx mapped on TIx): CCxS=01
     * Input filter: ICxF=2 (4 timer clock cycles, matching ChibiOS)
     */
    volatile uint32_t *ccmr_reg = SoftSigReaderInt::ccmr(tim, chan);
    uint32_t reg_val = *ccmr_reg;
    uint32_t shift = (chan & 1) * 8;

    /* Clear and set CCxS = 01 (input, TIx) */
    reg_val &= ~(TIM_CCMR1_CC1S << shift);
    reg_val |= (TIM_CCMR1_CC1S_0 << shift);

    /* Set ICxF = 2 (4 timer clock cycles filter) */
    reg_val &= ~(TIM_CCMR1_IC1F << shift);
    reg_val |= (2 << (4 + shift));

    *ccmr_reg = reg_val;

    /* Configure polarity: falling edge for main channel */
#ifdef HAL_RCIN_IS_INVERTED
    tim->CCER |= (TIM_CCER_CC1P << chan);             /* capture on rising edge */
    tim->CCER &= ~((TIM_CCER_CC1NP) << chan);
#else
    tim->CCER &= ~((TIM_CCER_CC1P | TIM_CCER_CC1NP) << chan);  /* capture on falling edge */
#endif
    tim->CCER &= ~(TIM_CCER_CC1E << chan);   /* Disable capture */

    /*
     * Configure aux channel (rising edge capture, IRQ enabled).
     *
     * CCxS = 10 (TIM_CCMR1_CC1S_1): cross-map to the opposite TI input.
     * ChibiOS reference: stm32_timer_set_channel_input(tim, aux_chan, 2)
     * (stm32_util.c:58-68) with input_source=2.
     *
     * Why CCxS=10 instead of CCxS=01:
     *   Main channel uses CCxS=01 (ICx on TIx) — capturing on its own TI.
     *   Aux  channel uses CCxS=10 (ICx on TI{x+1} or TI{x-1}) — capturing
     *   the *same* physical input signal as the main channel, just with
     *   opposite edge polarity. Both channels must connect to the same
     *   wire to measure pulse width (main->falling, aux->rising).
     *
     * STM32F7 CCxS mapping (RM0430 Section 30.4.3):
     *   CH1 CC1S=10: IC1 mapped on TI2  |  CH2 CC2S=10: IC2 mapped on TI1
     *   CH3 CC3S=10: IC3 mapped on TI4  |  CH4 CC4S=10: IC4 mapped on TI3
     */
    volatile uint32_t *aux_ccmr_reg = SoftSigReaderInt::ccmr(tim, _aux_channel);
    reg_val = *aux_ccmr_reg;
    uint32_t aux_shift = (_aux_channel & 1) * 8;

    reg_val &= ~(TIM_CCMR1_CC1S << aux_shift);
    reg_val |= (TIM_CCMR1_CC1S_1 << aux_shift);
    reg_val &= ~(TIM_CCMR1_IC1F << aux_shift);
    reg_val |= (2 << (4 + aux_shift));
    *aux_ccmr_reg = reg_val;

    /* Configure polarity: rising edge for aux channel */
#ifdef HAL_RCIN_IS_INVERTED
    tim->CCER &= ~((TIM_CCER_CC1P | TIM_CCER_CC1NP) << _aux_channel);  /* capture on falling edge */
#else
    tim->CCER |= (TIM_CCER_CC1P << _aux_channel);             /* capture on rising edge */
    tim->CCER &= ~((TIM_CCER_CC1NP) << _aux_channel);
#endif
    tim->CCER &= ~(TIM_CCER_CC1E << _aux_channel);  /* Disable capture */

    /*
     * Enable capture on both channels.
     */
    tim->CCER |= (TIM_CCER_CC1E << chan);
    tim->CCER |= (TIM_CCER_CC1E << _aux_channel);

    /*
     * Enable capture-compare interrupt for aux channel only.
     * ChibiOS: capture_cb = _irq_handler only on aux channel.
     */
    tim->DIER |= (TIM_DIER_CC1IE << _aux_channel);

    /*
     * Enable the timer.
     * ChibiOS: eicuEnable() enables the timer.
     */
    tim->CR1 |= TIM_CR1_CEN;

    /*
     * Configure NVIC for the timer's capture-compare IRQ.
     * Priority 2 matches ChibiOS default for EICU interrupts.
     */
    NVIC_SetPriority(_irq_n, 2);
    NVIC_EnableIRQ(_irq_n);
}

void SoftSigReaderInt::disable(void)
{
    if (_tim == nullptr) {
        return;
    }

    /* Disable IRQ for aux channel */
    _tim->DIER &= ~(TIM_DIER_CC1IE << _aux_channel);

    /* Disable capture on both channels */
    _tim->CCER &= ~((TIM_CCER_CC1E << _main_channel) |
                     (TIM_CCER_CC1E << _aux_channel));

    /* Disable timer */
    _tim->CR1 &= ~TIM_CR1_CEN;

    /* Disable NVIC IRQ */
    NVIC_DisableIRQ(_irq_n);
}

/*
 * IRQ handler — called on every capture event on the aux (rising edge) channel.
 * ChibiOS reference: SoftSigReaderInt.cpp lines 99-119.
 *
 * Reads both CCR values directly from the hardware registers.
 * The timer hardware captures timestamps at the exact edge moment,
 * so there is no ISR latency jitter.
 *
 * Over-capture detection (CCxOF): if a new capture occurred before this
 * ISR read the previous value, we push a zero-width pulse to signal
 * the protocol parser to reset.
 *
 * D-Cache note: sigbuf (ObjectBuffer<pulse_t>) is written in ISR context
 * and read from _timer_tick (thread context). On STM32F7 with D-Cache
 * enabled, the thread may read stale cached values after the ISR writes
 * through to memory. As of 2026-05, CUAV V5 RTT port disables D-Cache
 * for DMA compatibility, so this is not an issue. If D-Cache is re-enabled
 * in the future, add SCB_InvalidateDCache_by_Addr() before sigbuf.pop()
 * in read(), or place sigbuf in a non-cacheable MPU region.
 */
void SoftSigReaderInt::_irq_handler(void)
{
    SoftSigReaderInt *self = _singleton;
    if (self == nullptr || self->_tim == nullptr) {
        return;
    }

    TIM_TypeDef *tim = self->_tim;
    uint8_t main_ch = self->_main_channel;
    uint8_t aux_ch = self->_aux_channel;

    pulse_t pulse;
    pulse.w0 = (uint16_t)SoftSigReaderInt::ccr(tim, main_ch);
    pulse.w1 = (uint16_t)SoftSigReaderInt::ccr(tim, aux_ch);

    self->sigbuf.push(pulse);

    /*
     * Check for over-capture on either channel.
     * ChibiOS reference: SoftSigReaderInt.cpp lines 109-118.
     * STM32 TIM SR over-capture flags:
     *   CH1: SR bit 9  (CC1OF)
     *   CH2: SR bit 10 (CC2OF)
     *   CH3: SR bit 11 (CC3OF)
     *   CH4: SR bit 12 (CC4OF)
     */
    uint32_t mask = (TIM_SR_CC1OF << main_ch) | (TIM_SR_CC1OF << aux_ch);
    if ((tim->SR & mask) != 0) {
        /* Missed capture — push zero-width pulse to reset parser */
        pulse.w0 = 0;
        pulse.w1 = 0;
        self->sigbuf.push(pulse);

        /* Clear overcapture flags */
        tim->SR = tim->SR & ~mask;
    }

    /* Clear the capture-compare interrupt flag for aux channel */
    tim->SR = tim->SR & ~(TIM_SR_CC1IF << aux_ch);
}

bool SoftSigReaderInt::read(uint32_t &widths0, uint32_t &widths1)
{
    if (sigbuf.available() >= 2) {
        pulse_t pulse;
        if (sigbuf.pop(pulse)) {
            /* ChibiOS reference: SoftSigReaderInt.cpp lines 125-129 */
            widths0 = uint16_t(pulse.w0 - last_value);
            widths1 = uint16_t(pulse.w1 - pulse.w0);
            last_value = pulse.w1;
            return true;
        }
    }
    return false;
}

/*
 * C-linkage wrapper for NVIC vector table dispatch.
 *
 * Each timer's capture-compare IRQ handler should call this function.
 * Example board-level handler:
 *
 *   extern "C" void TIM1_CC_IRQHandler(void) {
 *       soft_sig_reader_irq_handler();
 *   }
 */
extern "C" void soft_sig_reader_irq_handler(void)
{
    RTT::SoftSigReaderInt::_irq_handler();
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTT
