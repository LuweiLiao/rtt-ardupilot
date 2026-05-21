/*
 * AP_HAL_RTT — RC Output (PWM)
 * Uses RT-Thread PWM device framework for servo/ESC output.
 * Channel-to-timer mapping driven by pwm_channel_map[].
 *
 * DShot support: command queue, channel masks, IOMCU routing.
 * Low-level DMA pulse generation (ChibiOS pwm_group) is not available
 * on RTT — DShot commands are forwarded to IOMCU when iomcu_dshot is
 * active; for FMU channels the command is queued and sent via the
 * next push() cycle (see RCOutput_serial.cpp).
 */

#pragma once

#include <AP_HAL/RCOutput.h>
#include <AP_HAL/utility/RingBuffer.h>
#include "HAL_RTT_Namespace.h"
#include <rtthread.h>

#define RTT_RCOUT_MAX_CHANNELS 16

struct rt_device_pwm;

namespace RTT
{

struct pwm_channel_config {
    const char *dev_name;
    uint8_t     timer_ch;
};

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

    void set_default_rate(uint16_t rate_hz) override;
    void set_output_mode(uint32_t mask, enum output_mode mode) override;
    enum output_mode get_output_mode(uint32_t &mask) override;
    void timer_tick(void) override;
    void timer_info(ExpandingString &str) override;

    // ---- DShot support (ChibiOS RCOutput_serial.cpp L137-186) ----
    void set_reversed_mask(uint32_t chanmask) override;
    void set_reversible_mask(uint32_t chanmask) override;
    uint32_t get_reversed_mask() override { return _reversed_mask; }
    void send_dshot_command(uint8_t command, uint8_t chan,
                            uint32_t command_timeout_ms,
                            uint16_t repeat_count,
                            bool priority) override;
    void update_channel_masks() override;
    void set_active_escs_mask(uint32_t chanmask) override {
        _active_escs_mask |= (chanmask >> chan_offset);
    }
    void set_dshot_esc_type(DshotEscType esc_type) override {
        _dshot_esc_type = esc_type;
    }
    DshotEscType get_dshot_esc_type() const override {
        return _dshot_esc_type;
    }
    void set_dshot_rate(uint8_t dshot_rate, uint16_t loop_rate_hz) override;
    void set_dshot_period(uint32_t period_us, uint8_t dshot_rate) override {
        _dshot_period_us = period_us;
        _dshot_rate = dshot_rate;
    }
    uint32_t get_dshot_period_us() const override { return _dshot_period_us; }
    void disable_channel_mask_updates() override {
        _disable_channel_mask_updates = true;
    }
    void enable_channel_mask_updates() override {
        _disable_channel_mask_updates = false;
    }

    // Safety switch state (read by Util::safety_switch_state)
    uint8_t safety_state = 0;  // AP_HAL::Util::SAFETY_DISARMED
    uint32_t safety_mask = 0;
    uint32_t safety_update_ms = 0;
    bool iomcu_enabled = false;

    void safety_update(void);

private:
    uint16_t _period_us[RTT_RCOUT_MAX_CHANNELS];
    uint16_t _pending_us[RTT_RCOUT_MAX_CHANNELS];
    uint16_t _failsafe_us[RTT_RCOUT_MAX_CHANNELS];
    uint16_t _freq_hz[RTT_RCOUT_MAX_CHANNELS];
    uint32_t _enabled_mask = 0;
    uint8_t _num_channels = 0;
    uint16_t _default_rate_hz = 50;
    enum output_mode _output_mode = MODE_PWM_NORMAL;
    bool _corked = false;
    bool _initialized = false;

    struct rt_device_pwm *_pwm_dev[RTT_RCOUT_MAX_CHANNELS];

    void _write_hw(uint8_t chan, uint16_t period_us);

    // ---- DShot infrastructure (ChibiOS reference: RCOutput.h L600-630) ----
    struct DshotCommandPacket {
        uint8_t command;
        uint32_t cycle;
        uint8_t chan;
    };

    ObjectBuffer<DshotCommandPacket> _dshot_command_queue{8};
    DshotCommandPacket _dshot_current_command;
    uint32_t _reversed_mask = 0;
    uint32_t _reversible_mask = 0;
    uint32_t _active_escs_mask = 0;
    DshotEscType _dshot_esc_type = DSHOT_ESC_NONE;
    bool _disable_channel_mask_updates = false;
    uint32_t _dshot_period_us = 400;
    uint8_t _dshot_rate = 0;
    uint8_t _dshot_cycle = 0;
    // offset of FMU channels (0 when no IOMCU, 8 with IOMCU)
    uint8_t chan_offset = 0;
    // true when IOMCU handles DShot output
    bool iomcu_dshot = false;
};

} // namespace RTT
