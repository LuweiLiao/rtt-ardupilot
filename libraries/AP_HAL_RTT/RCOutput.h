/*
 * AP_HAL_RTT — RC Output (PWM)
 * Uses RT-Thread PWM device framework for servo/ESC output.
 * Supports configurable frequency and cork/push batching.
 */

#pragma once

#include <AP_HAL/RCOutput.h>
#include "HAL_RTT_Namespace.h"

#define RTT_RCOUT_MAX_CHANNELS 16

namespace RTT
{

class RCOutput : public AP_HAL::RCOutput
{
public:
    void init() override;
    void set_freq(uint32_t chmask, uint16_t freq_hz) override;
    uint16_t get_freq(uint8_t chan) override;
    void enable_ch(uint8_t chan) override;
    void disable_ch(uint8_t chan) override;
    void write(uint8_t chan, uint16_t period_us) override;
    void cork() override;
    void push() override;
    uint16_t read(uint8_t chan) override;
    void read(uint16_t* period_us, uint8_t len) override;
    uint16_t read_last_sent(uint8_t chan) override;
    void read_last_sent(uint16_t* period_us, uint8_t len) override;
    void set_failsafe_pwm(uint32_t chmask, uint16_t period_us) override;
    bool force_safety_on() override;
    void force_safety_off() override;
    void set_safety_pwm(uint32_t chmask, uint16_t period_us);
    uint32_t get_num_channels() const { return _num_channels; }

private:
    uint16_t _period_us[RTT_RCOUT_MAX_CHANNELS];
    uint16_t _pending_us[RTT_RCOUT_MAX_CHANNELS];
    uint16_t _failsafe_us[RTT_RCOUT_MAX_CHANNELS];
    uint16_t _freq_hz[RTT_RCOUT_MAX_CHANNELS];
    uint32_t _enabled_mask = 0;
    uint8_t _num_channels = 0;
    bool _corked = false;
    bool _initialized = false;

    void _write_hw(uint8_t chan, uint16_t period_us);
};

} // namespace RTT
