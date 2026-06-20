/*
 * AP_HAL_RTT — RC Output (PWM)
 * Direct TIM register access (TIM1, TIM4, TIM12).
 * Replaces RT-Thread PWM device framework (rt_pwm_*) with
 * register-level control matching the ChibiOS hal_pwm_lld.c pattern.
 *
 * CUAV V5 PWM mapping (same as ChibiOS fmuv5):
 *   CH1 → TIM1_CH4 (PE14)    CH5 → TIM4_CH2 (PD13)
 *   CH2 → TIM1_CH3 (PA10)    CH6 → TIM4_CH3 (PD14)
 *   CH3 → TIM1_CH2 (PE11)    CH7 → TIM12_CH1 (PH6)
 *   CH4 → TIM1_CH1 (PE9)     CH8 → TIM12_CH2 (PH9)
 *
 * Timer clocks (CUAV V5: 216 MHz SYSCLK):
 *   TIM1   on APB2 (108 MHz ×2 = 216 MHz)
 *   TIM4   on APB1 ( 54 MHz ×2 = 108 MHz)
 *   TIM12  on APB1 ( 54 MHz ×2 = 108 MHz)
 */

#include "RCOutput.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Common/ExpandingString.h>
#include <AP_BoardConfig/AP_BoardConfig.h>
#include <AP_InternalError/AP_InternalError.h>
#include <rtthread.h>

extern const AP_HAL::HAL& hal;

#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
extern AP_IOMCU iomcu;
#endif

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#define RTT_RCOUT_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
volatile uint32_t rtt_dbg_rcout_push_calls RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_local_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_local_accum_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_local_max_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_iomcu_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_iomcu_accum_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_iomcu_max_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_total_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_total_accum_us RTT_RCOUT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_rcout_push_total_max_us RTT_RCOUT_DBG_DTCM_BSS;

static inline void rtt_dbg_rcout_update_max(volatile uint32_t &target, uint32_t value)
{
    if (value > target) {
        target = value;
    }
}
#endif

namespace RTT
{

/*
 * CUAV V5 channel-to-timer mapping.
 * timer_ch is 0-based: 0=CCR1, 1=CCR2, 2=CCR3, 3=CCR4.
 *
 * The order here matches the physical RC output channel order (CH1..CH8).
 */
static const tim_channel_config _cuav_v5_map[] = {
    { TIM1,  3 },   // CH1 → TIM1_CH4 → CCR4  (channel 3)
    { TIM1,  2 },   // CH2 → TIM1_CH3 → CCR3  (channel 2)
    { TIM1,  1 },   // CH3 → TIM1_CH2 → CCR2  (channel 1)
    { TIM1,  0 },   // CH4 → TIM1_CH1 → CCR1  (channel 0)
    { TIM4,  1 },   // CH5 → TIM4_CH2 → CCR2  (channel 1)
    { TIM4,  2 },   // CH6 → TIM4_CH3 → CCR3  (channel 2)
    { TIM12, 0 },   // CH7 → TIM12_CH1 → CCR1 (channel 0)
    { TIM12, 1 },   // CH8 → TIM12_CH2 → CCR2 (channel 1)
};
static constexpr uint8_t _cuav_v5_map_count = sizeof(_cuav_v5_map) / sizeof(_cuav_v5_map[0]);

/* ---- Helper: find timer_state index for a given TIM_TypeDef* ---- */
int8_t RCOutput::_timer_idx(TIM_TypeDef *tim) const
{
    for (uint8_t i = 0; i < _num_timers; i++) {
        if (_timer[i].tim == tim) {
            return int8_t(i);
        }
    }
    return -1;
}

/* ---- RCC clock gating (sub-task 7) ---- */
void RCOutput::_timer_clock_enable(TIM_TypeDef *tim)
{
    if (tim == TIM1) {
        RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
        (void)RCC->APB2ENR;  // ensure write completes
    } else if (tim == TIM4) {
        RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
        (void)RCC->APB1ENR;
    } else if (tim == TIM12) {
        RCC->APB1ENR |= RCC_APB1ENR_TIM12EN;
        (void)RCC->APB1ENR;
    }
}

/* ---- CCMR register init: PWM mode 1 + preload for all channels (sub-task 3) ---- */
void RCOutput::_timer_ccmr_init(TIM_TypeDef *tim)
{
    /* PWM mode 1 (OCxM = 6) with preload enable (OCxPE = 1).
     * For 4-channel timers (TIM1, TIM4) configure CCMR1 (ch1,ch2) + CCMR2 (ch3,ch4).
     * For 2-channel timers (TIM12) configure CCMR1 only (ch1,ch2).
     */
    uint32_t ccmr1 = 0;
    uint32_t ccmr2 = 0;

    /* Channel 1: PWM1 + preload */
    ccmr1 |= (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2);  // OC1M = 6 (PWM mode 1, 0b0110)
    ccmr1 |= TIM_CCMR1_OC1PE;                          // preload enable

    /* Channel 2: PWM1 + preload */
    ccmr1 |= (TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2);   // OC2M = 6
    ccmr1 |= TIM_CCMR1_OC2PE;                           // preload enable

    if (tim != TIM12) {
        /* TIM1 and TIM4 have 4 channels */
        ccmr2 |= (TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2);  // OC3M = 6
        ccmr2 |= TIM_CCMR2_OC3PE;                            // preload enable

        ccmr2 |= (TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2);  // OC4M = 6
        ccmr2 |= TIM_CCMR2_OC4PE;                            // preload enable
    }

    tim->CCMR1 = ccmr1;
    if (tim != TIM12) {
        tim->CCMR2 = ccmr2;
    }

    /* Ensure 0-based counter direction (upcounting) */
    tim->CR1 &= ~TIM_CR1_DIR;
}

/* ---- ARR/PSC frequency setup (sub-task 1) ---- */
uint16_t RCOutput::_timer_set_freq_internal(TIM_TypeDef *tim, uint16_t freq_hz)
{
    int8_t idx = _timer_idx(tim);
    if (idx < 0) {
        return 0;
    }

    uint32_t clock = _timer[idx].clock_hz;
    if (freq_hz == 0) {
        freq_hz = 50;   // fallback default
    }

    /* Calculate prescaler: want ARR near 0xFFFF for best resolution.
     * psc = (clock / (freq * 0xFFFF)) — then ARR = clock / (freq * (psc+1)) - 1.
     */
    uint32_t psc = (clock / (uint32_t)freq_hz + 0xFFFF) / 0x10000;
    if (psc > 0) {
        psc -= 1;       // PSC register is psc-1
    }
    uint16_t arr = (uint16_t)((clock / (uint32_t)freq_hz) / (psc + 1)) - 1;

    _timer[idx].prescaler = (uint16_t)psc;
    _timer[idx].period_arr = arr;

    tim->PSC = psc;
    tim->ARR = arr;

    /* Generate update event to reload shadow registers */
    tim->EGR |= TIM_EGR_UG;

    return arr;
}

/* ---- CCR write (sub-task 2) ---- */
void RCOutput::_timer_write_ccr(TIM_TypeDef *tim, uint8_t channel, uint32_t ccr_val)
{
    switch (channel) {
    case 0:  tim->CCR1 = ccr_val; break;
    case 1:  tim->CCR2 = ccr_val; break;
    case 2:  tim->CCR3 = ccr_val; break;
    case 3:  tim->CCR4 = ccr_val; break;
    default: break;
    }
}

/* ---- CCER channel enable/disable (sub-task 3) ---- */
void RCOutput::_timer_ccer_enable(TIM_TypeDef *tim, uint8_t channel, bool enable)
{
    const uint32_t ch_bits[4] = {
        TIM_CCER_CC1E,   // channel 0 → CC1E (bit 0)
        TIM_CCER_CC2E,   // channel 1 → CC2E (bit 4)
        TIM_CCER_CC3E,   // channel 2 → CC3E (bit 8)
        TIM_CCER_CC4E,   // channel 3 → CC4E (bit 12)
    };
    if (channel < 4) {
        if (enable) {
            tim->CCER |= ch_bits[channel];
        } else {
            tim->CCER &= ~ch_bits[channel];
        }
    }
}

/* ---- Number of channels per timer ---- */
uint8_t RCOutput::_timer_num_channels(TIM_TypeDef *tim)
{
    if (tim == TIM1 || tim == TIM4) {
        return 4;
    }
    if (tim == TIM12) {
        return 2;
    }
    return 0;
}

/* ================================================================== */
/*                   RCOutput interface methods                        */
/* ================================================================== */

void RCOutput::init()
{
    if (_initialized) return;

    /* Initialise per-timer state (clock constants) */
    _timer[0].tim       = TIM1;
    _timer[0].clock_hz  = TIM1_CLOCK;
    _timer[0].period_arr = 0;
    _timer[0].prescaler  = 0;

    _timer[1].tim       = TIM4;
    _timer[1].clock_hz  = TIM4_CLOCK;
    _timer[1].period_arr = 0;
    _timer[1].prescaler  = 0;

    _timer[2].tim       = TIM12;
    _timer[2].clock_hz  = TIM12_CLOCK;
    _timer[2].period_arr = 0;
    _timer[2].prescaler  = 0;

    /* Zero-initialise channel arrays */
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _period_us[i] = 0;
        _pending_us[i] = 0;
        _failsafe_us[i] = 0;
        _freq_hz[i] = 50;
        _tim_dev[i] = nullptr;
        _tim_chan[i] = 0;
    }

    /* ---- Step 1: Enable RCC clocks for all three timers (sub-task 7) ---- */
    _timer_clock_enable(TIM1);
    _timer_clock_enable(TIM4);
    _timer_clock_enable(TIM12);

    /* ---- Step 2: Configure CCMR for PWM1 output mode (sub-task 3) ---- */
    _timer_ccmr_init(TIM1);
    _timer_ccmr_init(TIM4);
    _timer_ccmr_init(TIM12);

    /* ---- Step 3: Initialise CCER — all channels disabled, active high ---- */
    TIM1->CCER  = 0;
    TIM4->CCER  = 0;
    TIM12->CCER = 0;

    /* ---- Step 4: Set default frequency (50 Hz) for all timers ---- */
    _timer_set_freq_internal(TIM1, 50);
    _timer_set_freq_internal(TIM4, 50);
    _timer_set_freq_internal(TIM12, 50);

    /* ---- Step 5: Clear status and pending IRQs ---- */
    TIM1->SR  = 0;
    TIM4->SR  = 0;
    TIM12->SR = 0;

    /* ---- Step 6: BDTR — main output enable for TIM1 (break timer, sub-tasks 4,5) ---- */
    TIM1->BDTR = TIM_BDTR_MOE;   // Main Output Enable
    // TIM4 and TIM12 have no BDTR

    /* ---- Step 7: Start timers (CR1: ARPE + URS + CEN) ---- */
    const uint32_t cr1_start = TIM_CR1_ARPE | TIM_CR1_URS | TIM_CR1_CEN;
    TIM1->CR1  = cr1_start;
    TIM4->CR1  = cr1_start;
    TIM12->CR1 = cr1_start;

    /* Fill channel mapping */
    for (uint8_t i = 0; i < _cuav_v5_map_count && i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _tim_dev[i]  = _cuav_v5_map[i].tim;
        _tim_chan[i] = _cuav_v5_map[i].channel;
    }

    _initialized = true;

#if HAL_WITH_IO_MCU
    if (AP_BoardConfig::io_enabled()) {
        // with IOMCU the local (FMU) channels start at 8
        chan_offset = 8;
        iomcu_enabled = true;
        iomcu.init();
    }
    if (AP_BoardConfig::io_dshot()) {
        iomcu_dshot = true;
    }
#endif
    // Register safety_update as a timer process at 10 Hz
    hal.scheduler->register_timer_process(FUNCTOR_BIND_MEMBER(&RCOutput::safety_update, void));
}

void RCOutput::set_freq(uint32_t chmask, uint16_t freq_hz)
{
    // Reference: ChibiOS RCOutput.cpp:444-463
    // Forward frequency change to IOMCU
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        uint16_t io_chmask = chmask & 0xFF;
        if (io_chmask) {
            iomcu.set_freq(io_chmask, freq_hz);
        }
    }
#endif

    // Convert to a local (FMU) channel mask
    chmask >>= chan_offset;
    if (chmask == 0) {
        return;
    }

    // Reference: ChibiOS RCOutput.cpp:481-483
    // Limit frequency to 400Hz for normal PWM (non-brushed)
    uint16_t capped_freq = freq_hz;
    if (capped_freq > 400 && _output_mode != MODE_PWM_BRUSHED) {
        capped_freq = 400;
    }

    /* Collect unique timers that need reconfiguration */
    bool tim1_needs_update = false;
    bool tim4_needs_update = false;
    bool tim12_needs_update = false;

    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        if (chmask & (1U << i)) {
            _freq_hz[i] = capped_freq;
            if (_tim_dev[i] == TIM1)     tim1_needs_update = true;
            if (_tim_dev[i] == TIM4)     tim4_needs_update = true;
            if (_tim_dev[i] == TIM12)    tim12_needs_update = true;
        }
    }

    /* Apply ARR/PSC changes (shared per timer group) */
    if (tim1_needs_update)  _timer_set_freq_internal(TIM1, capped_freq);
    if (tim4_needs_update)  _timer_set_freq_internal(TIM4, capped_freq);
    if (tim12_needs_update) _timer_set_freq_internal(TIM12, capped_freq);
}

uint16_t RCOutput::get_freq(uint8_t chan)
{
#if HAL_WITH_IO_MCU
    if (chan < chan_offset && iomcu_enabled) {
        return iomcu.get_freq(chan);
    }
#endif
    if (chan >= chan_offset) {
        chan -= chan_offset;
    }
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        return _freq_hz[chan];
    }
    return 50;
}

void RCOutput::enable_ch(uint8_t chan)
{
#if HAL_WITH_IO_MCU
    if (chan < chan_offset && iomcu_enabled) {
        iomcu.enable_ch(chan);
        return;
    }
#endif
    if (chan >= chan_offset) {
        chan -= chan_offset;
    }
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        _enabled_mask |= (1U << chan);
        if (chan >= _num_channels) {
            _num_channels = chan + 1;
        }
    }
}

void RCOutput::disable_ch(uint8_t chan)
{
#if HAL_WITH_IO_MCU
    if (chan < chan_offset && iomcu_enabled) {
        iomcu.disable_ch(chan);
        return;
    }
#endif
    if (chan >= chan_offset) {
        chan -= chan_offset;
    }
    if (chan >= RTT_RCOUT_MAX_CHANNELS) {
        return;
    }
    _enabled_mask &= ~(1U << chan);
    if (_tim_dev[chan] != nullptr) {
        /* Disable CCER output and clear CCR (sub-tasks 2,3) */
        _timer_ccer_enable(_tim_dev[chan], _tim_chan[chan], false);
        _timer_write_ccr(_tim_dev[chan], _tim_chan[chan], 0);
    }
}

void RCOutput::_write_hw(uint8_t chan, uint16_t period_us)
{
    if (chan >= _cuav_v5_map_count || _tim_dev[chan] == nullptr) {
        return;
    }

    TIM_TypeDef *tim = _tim_dev[chan];
    uint8_t ch = _tim_chan[chan];

    /* Get current timer period (ARR+1) */
    int8_t idx = _timer_idx(tim);
    if (idx < 0) {
        return;
    }
    uint32_t timer_period = (uint32_t)_timer[idx].period_arr + 1;

    /* Convert pulse width (us) to timer counts.
     *   timer_freq  = timer clock / (prescaler+1)
     *   counts = (pulse_us / 1e6) * timer_freq
     *          = pulse_us * timer_freq / 1000000
     *
     * timer_freq = clock_hz / (prescaler + 1)
     * counts = period_us * (clock_hz / (prescaler+1)) / 1000000
     *        = period_us * clock_hz / ((prescaler+1) * 1000000)
     */
    uint32_t psc_1 = _timer[idx].prescaler + 1;
    uint64_t counts = (uint64_t)period_us * _timer[idx].clock_hz;
    counts /= (uint64_t)psc_1 * 1000000ULL;

    if (counts > timer_period) {
        counts = timer_period;  // clamp to 100% duty
    }

    _timer_write_ccr(tim, ch, (uint32_t)counts);

    /* Enable CCER output if not already (safe: repeated writes are idempotent) */
    _timer_ccer_enable(tim, ch, true);
}

void RCOutput::write(uint8_t chan, uint16_t period_us)
{
    if (chan >= RTT_RCOUT_MAX_CHANNELS) return;

    // Reference: ChibiOS RCOutput.cpp:723-727
    // Forward write to IOMCU for IO MCU channels
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        iomcu.write_channel(chan, period_us);
    }
#endif
    // If this is an IOMCU channel (< chan_offset), return after forwarding
    // Reference: ChibiOS RCOutput.cpp:729-731
    if (chan < chan_offset) {
        return;
    }

    // Reference: ChibiOS RCOutput.cpp:733-736
    // Safety: if DISARMED and channel is NOT in the safety whitelist, force to 0
    if (safety_state == AP_HAL::Util::SAFETY_DISARMED && !(safety_mask & (1U << chan))) {
        period_us = 0;
    }

    // Reference: ChibiOS RCOutput.cpp:738
    // Adjust channel index for local (FMU) access
    const uint8_t local_chan = chan - chan_offset;

    if (_corked) {
        _pending_us[local_chan] = period_us;
    } else {
        _period_us[local_chan] = period_us;
        _write_hw(local_chan, period_us);
    }
}

void RCOutput::cork()
{
    _corked = true;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _pending_us[i] = _period_us[i];
    }
    // Reference: ChibiOS RCOutput.cpp:1337-1341
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        iomcu.cork();
    }
#endif
}

void RCOutput::push()
{
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    const uint32_t rtt_dbg_start_us = AP_HAL::micros();
    rtt_dbg_rcout_push_calls++;
#endif
    // Reference: ChibiOS RCOutput.cpp:1349-1351
    if (!_corked) {
        INTERNAL_ERROR(AP_InternalError::error_t::flow_of_control);
    }
    _corked = false;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        if (_pending_us[i] != _period_us[i] || (_enabled_mask & (1U << i))) {
            _period_us[i] = _pending_us[i];
            _write_hw(i, _period_us[i]);
        }
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    const uint32_t rtt_dbg_after_local_us = AP_HAL::micros();
    const uint32_t rtt_dbg_local_us = rtt_dbg_after_local_us - rtt_dbg_start_us;
    rtt_dbg_rcout_push_local_us = rtt_dbg_local_us;
    rtt_dbg_rcout_push_local_accum_us += rtt_dbg_local_us;
    rtt_dbg_rcout_update_max(rtt_dbg_rcout_push_local_max_us, rtt_dbg_local_us);
#endif
    // Reference: ChibiOS RCOutput.cpp:1355-1358
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        const uint32_t rtt_dbg_iomcu_start_us = AP_HAL::micros();
#endif
        iomcu.push();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        const uint32_t rtt_dbg_iomcu_us = AP_HAL::micros() - rtt_dbg_iomcu_start_us;
        rtt_dbg_rcout_push_iomcu_us = rtt_dbg_iomcu_us;
        rtt_dbg_rcout_push_iomcu_accum_us += rtt_dbg_iomcu_us;
        rtt_dbg_rcout_update_max(rtt_dbg_rcout_push_iomcu_max_us, rtt_dbg_iomcu_us);
#endif
    }
#endif
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    const uint32_t rtt_dbg_total_us = AP_HAL::micros() - rtt_dbg_start_us;
    rtt_dbg_rcout_push_total_us = rtt_dbg_total_us;
    rtt_dbg_rcout_push_total_accum_us += rtt_dbg_total_us;
    rtt_dbg_rcout_update_max(rtt_dbg_rcout_push_total_max_us, rtt_dbg_total_us);
#endif
}

uint16_t RCOutput::read(uint8_t chan)
{
#if HAL_WITH_IO_MCU
    if (chan < chan_offset && iomcu_enabled) {
        return iomcu.read_channel(chan);
    }
#endif
    if (chan >= chan_offset) {
        chan -= chan_offset;
    }
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        return _period_us[chan];
    }
    return 0;
}

void RCOutput::read(uint16_t* period_us, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
#if HAL_WITH_IO_MCU
        if (i < chan_offset && iomcu_enabled) {
            period_us[i] = iomcu.read_channel(i);
            continue;
        }
#endif
        const uint8_t local_chan = (i >= chan_offset) ? i - chan_offset : i;
        period_us[i] = (local_chan < RTT_RCOUT_MAX_CHANNELS) ? _period_us[local_chan] : 0;
    }
}

uint16_t RCOutput::read_last_sent(uint8_t chan)
{
    return read(chan);
}

void RCOutput::read_last_sent(uint16_t* period_us, uint8_t len)
{
    read(period_us, len);
}

void RCOutput::set_failsafe_pwm(uint32_t chmask, uint16_t period_us)
{
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        if (chmask & (1U << i)) {
            _failsafe_us[i] = period_us;
        }
    }
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        iomcu.set_failsafe_pwm(chmask, period_us);
    }
#endif
}

bool RCOutput::force_safety_on()
{
    // Reference: ChibiOS RCOutput.cpp:2375-2383
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        return iomcu.force_safety_on();
    }
#endif
    safety_state = AP_HAL::Util::SAFETY_DISARMED;

    /* Trim all CCR values to zero on local channels */
    for (uint8_t i = 0; i < _num_channels; i++) {
        if (_tim_dev[i] != nullptr) {
            _timer_write_ccr(_tim_dev[i], _tim_chan[i], 0);
        }
    }

    /* Disable main output on TIM1 (BDTR) if present (sub-task 4) */
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    /* TIM4 and TIM12 have no BDTR */

    return true;
}

void RCOutput::force_safety_off()
{
    // Reference: ChibiOS RCOutput.cpp:2389-2397
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        iomcu.force_safety_off();
        return;
    }
#endif
    safety_state = AP_HAL::Util::SAFETY_ARMED;

    /* Re-enable main output on TIM1 (sub-task 4) */
    TIM1->BDTR |= TIM_BDTR_MOE;
}

void RCOutput::set_safety_pwm(uint32_t chmask, uint16_t period_us)
{
#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        iomcu.set_safety_mask(chmask);
    }
#endif
}

void RCOutput::set_default_rate(uint16_t rate_hz)
{
    _default_rate_hz = rate_hz;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        if (_freq_hz[i] == 50 || _freq_hz[i] == 0) {
            _freq_hz[i] = rate_hz;
        }
    }

    /* Apply default rate to all timers */
    _timer_set_freq_internal(TIM1, rate_hz);
    _timer_set_freq_internal(TIM4, rate_hz);
    _timer_set_freq_internal(TIM12, rate_hz);
}

void RCOutput::set_output_mode(uint32_t mask, enum output_mode mode)
{
    _output_mode = mode;

#if HAL_WITH_IO_MCU
    const uint16_t iomcu_mask = mask & ((1U << chan_offset) - 1U);
    if (iomcu_enabled && iomcu_mask &&
        (mode == MODE_PWM_ONESHOT ||
         mode == MODE_PWM_ONESHOT125 ||
         mode == MODE_PWM_BRUSHED ||
         (mode >= MODE_PWM_DSHOT150 && mode <= MODE_PWM_DSHOT600))) {
        iomcu.set_output_mode(iomcu_mask, mode);
    }
#endif
}

AP_HAL::RCOutput::output_mode RCOutput::get_output_mode(uint32_t &mask)
{
    mask = _enabled_mask;
    return _output_mode;
}

uint32_t RCOutput::get_disabled_channels(uint32_t digital_mask)
{
#if HAL_WITH_IO_MCU
    if (iomcu_dshot) {
        return iomcu.get_disabled_channels(digital_mask);
    }
#endif
    return 0;
}

void RCOutput::safety_update(void)
{
    uint32_t now = AP_HAL::millis();
    if (now - safety_update_ms < 100) {
        return;
    }
    safety_update_ms = now;

#if HAL_WITH_IO_MCU
    if (iomcu_enabled) {
        safety_state = iomcu.get_safety_switch_state();
    }
#endif

    // Read safety mask from board config
    const AP_BoardConfig *bc = AP_BoardConfig::get_singleton();
    if (bc) {
        safety_mask = bc->get_safety_mask();
    }
}

void RCOutput::timer_tick(void)
{
    if (!_initialized) {
        return;
    }
    for (uint8_t i = 0; i < _num_channels; i++) {
        if (_enabled_mask & (1U << i)) {
            _write_hw(i, _period_us[i]);
        }
    }
}

void RCOutput::timer_info(ExpandingString &str)
{
    str.printf("RCOutput: %u channels, default_rate=%u, mode=%u\n",
               (unsigned)_num_channels, (unsigned)_default_rate_hz,
               (unsigned)_output_mode);
    for (uint8_t i = 0; i < _num_channels; i++) {
        str.printf("  CH%u: %u us @ %u Hz %s  tim=%p ch=%u\n",
                   (unsigned)i, (unsigned)_period_us[i],
                   (unsigned)_freq_hz[i],
                   (_enabled_mask & (1U << i)) ? "EN" : "DIS",
                   (void*)_tim_dev[i],
                   (unsigned)_tim_chan[i]);
    }
}

} // namespace RTT
