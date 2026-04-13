/*
 * AP_HAL_RTT — RC Output (PWM)
 * Uses RT-Thread PWM device framework.
 *
 * CUAV V5 PWM mapping (same as ChibiOS fmuv5):
 *   CH1 → TIM1_CH4 (PE14)    CH5 → TIM4_CH2 (PD13)
 *   CH2 → TIM1_CH3 (PA10)    CH6 → TIM4_CH3 (PD14)
 *   CH3 → TIM1_CH2 (PE11)    CH7 → TIM12_CH1 (PH6)
 *   CH4 → TIM1_CH1 (PE9)     CH8 → TIM12_CH2 (PH9)
 *
 * RT-Thread PWM device names: "pwm1", "pwm4", "pwm12"
 * Channel numbers are 1-based in RT-Thread PWM API.
 */

#include "RCOutput.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Common/ExpandingString.h>
#include <AP_BoardConfig/AP_BoardConfig.h>
#include <rtthread.h>

extern const AP_HAL::HAL& hal;

#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
extern AP_IOMCU iomcu;
#endif

#if defined(RT_USING_PWM)
#include <rtdevice.h>
#endif

namespace RTT
{

static const pwm_channel_config _cuav_v5_map[] = {
    { "pwm1", 4 },   // CH1 → TIM1_CH4
    { "pwm1", 3 },   // CH2 → TIM1_CH3
    { "pwm1", 2 },   // CH3 → TIM1_CH2
    { "pwm1", 1 },   // CH4 → TIM1_CH1
    { "pwm4", 2 },   // CH5 → TIM4_CH2
    { "pwm4", 3 },   // CH6 → TIM4_CH3
    { "pwm12", 1 },  // CH7 → TIM12_CH1
    { "pwm12", 2 },  // CH8 → TIM12_CH2
};
static const uint8_t _cuav_v5_map_count = sizeof(_cuav_v5_map) / sizeof(_cuav_v5_map[0]);

void RCOutput::init()
{
    if (_initialized) return;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _period_us[i] = 0;
        _pending_us[i] = 0;
        _failsafe_us[i] = 0;
        _freq_hz[i] = 50;
        _pwm_dev[i] = nullptr;
    }

#if defined(RT_USING_PWM)
    for (uint8_t i = 0; i < _cuav_v5_map_count && i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _pwm_dev[i] = (struct rt_device_pwm *)rt_device_find(_cuav_v5_map[i].dev_name);
    }
#endif

    _initialized = true;

#if HAL_WITH_IO_MCU
    iomcu_enabled = true;
#endif
    // Register safety_update as a timer process at 10 Hz
    hal.scheduler->register_timer_process(FUNCTOR_BIND_MEMBER(&RCOutput::safety_update, void));
}

void RCOutput::set_freq(uint32_t chmask, uint16_t freq_hz)
{
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        if (chmask & (1U << i)) {
            _freq_hz[i] = freq_hz;
        }
    }
}

uint16_t RCOutput::get_freq(uint8_t chan)
{
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        return _freq_hz[chan];
    }
    return 50;
}

void RCOutput::enable_ch(uint8_t chan)
{
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        _enabled_mask |= (1U << chan);
        if (chan >= _num_channels) {
            _num_channels = chan + 1;
        }
    }
}

void RCOutput::disable_ch(uint8_t chan)
{
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        _enabled_mask &= ~(1U << chan);
#if defined(RT_USING_PWM)
        if (_pwm_dev[chan] && chan < _cuav_v5_map_count) {
            rt_pwm_disable(_pwm_dev[chan], _cuav_v5_map[chan].timer_ch);
        }
#endif
    }
}

void RCOutput::_write_hw(uint8_t chan, uint16_t period_us)
{
#if defined(RT_USING_PWM)
    if (chan >= _cuav_v5_map_count || !_pwm_dev[chan]) {
        return;
    }
    uint32_t period_ns = 1000000000UL / _freq_hz[chan];
    uint32_t pulse_ns  = (uint32_t)period_us * 1000U;
    rt_pwm_set(_pwm_dev[chan], _cuav_v5_map[chan].timer_ch, period_ns, pulse_ns);
    rt_pwm_enable(_pwm_dev[chan], _cuav_v5_map[chan].timer_ch);
#else
    (void)chan;
    (void)period_us;
#endif
}

void RCOutput::write(uint8_t chan, uint16_t period_us)
{
    if (chan >= RTT_RCOUT_MAX_CHANNELS) return;
    // If safety is on and this channel is in the safety mask, suppress output
    if (safety_state == AP_HAL::Util::SAFETY_DISARMED && (safety_mask & (1U << chan))) {
        return;
    }
    if (_corked) {
        _pending_us[chan] = period_us;
    } else {
        _period_us[chan] = period_us;
        _write_hw(chan, period_us);
    }
}

void RCOutput::cork()
{
    _corked = true;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _pending_us[i] = _period_us[i];
    }
}

void RCOutput::push()
{
    _corked = false;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        if (_pending_us[i] != _period_us[i] || (_enabled_mask & (1U << i))) {
            _period_us[i] = _pending_us[i];
            _write_hw(i, _period_us[i]);
        }
    }
}

uint16_t RCOutput::read(uint8_t chan)
{
    if (chan < RTT_RCOUT_MAX_CHANNELS) {
        return _period_us[chan];
    }
    return 0;
}

void RCOutput::read(uint16_t* period_us, uint8_t len)
{
    for (uint8_t i = 0; i < len && i < RTT_RCOUT_MAX_CHANNELS; i++) {
        period_us[i] = _period_us[i];
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
    /* No IOMCU on RTT build; manage safety state locally.
     * Matches ChibiOS behaviour without IOMCU. */
    safety_state = AP_HAL::Util::SAFETY_DISARMED;
    return true;
}

void RCOutput::force_safety_off()
{
    safety_state = AP_HAL::Util::SAFETY_ARMED;
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
}

void RCOutput::set_output_mode(uint32_t mask, enum output_mode mode)
{
    (void)mask;
    _output_mode = mode;
}

AP_HAL::RCOutput::output_mode RCOutput::get_output_mode(uint32_t &mask)
{
    mask = _enabled_mask;
    return _output_mode;
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
        str.printf("  CH%u: %u us @ %u Hz %s\n",
                   (unsigned)i, (unsigned)_period_us[i],
                   (unsigned)_freq_hz[i],
                   (_enabled_mask & (1U << i)) ? "EN" : "DIS");
    }
}

} // namespace RTT
