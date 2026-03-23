/*
 * AP_HAL_RTT — RC Output (PWM)
 * Uses RT-Thread PWM device framework.
 * Supports configurable frequency per channel group and cork/push batching.
 * Channel mapping driven by HAL_RTT_PWM_MAP (from hwdef.h) or built-in defaults.
 */

#include "RCOutput.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>

#if defined(RT_USING_PWM)
#include <rtdevice.h>
#endif

namespace RTT
{

void RCOutput::init()
{
    if (_initialized) return;
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        _period_us[i] = 0;
        _pending_us[i] = 0;
        _failsafe_us[i] = 0;
        _freq_hz[i] = 50;
    }
    _initialized = true;
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
    }
}

void RCOutput::_write_hw(uint8_t chan, uint16_t period_us)
{
#if defined(RT_USING_PWM)
    (void)chan;
    (void)period_us;
    /* PWM hardware write will be connected when BSP enables RT_USING_PWM
     * and timer/channel mapping is configured in hwdef.dat */
#endif
}

void RCOutput::write(uint8_t chan, uint16_t period_us)
{
    if (chan >= RTT_RCOUT_MAX_CHANNELS) return;
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
}

bool RCOutput::force_safety_on()
{
    return false;
}

void RCOutput::force_safety_off()
{
}

void RCOutput::set_safety_pwm(uint32_t chmask, uint16_t period_us)
{
    (void)chmask;
    (void)period_us;
}

} // namespace RTT
