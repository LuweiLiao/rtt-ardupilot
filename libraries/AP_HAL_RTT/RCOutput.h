/*
 * AP_HAL_RTT — RC Output (PWM)
 * Direct TIM register access (TIM1, TIM4, TIM12) for servo/ESC output.
 * Replaces RT-Thread PWM device framework with register-level control.
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
#include <stm32f7xx.h>       // TIM_TypeDef, RCC, register bit definitions

/* STM32 HAL legacy.h defines ALL_CHANNELS → ADC_ALL_CHANNELS, which
 * conflicts with ArduPilot RCOutput_serial.cpp usage.  Undefine here
 * so the macro doesn't leak into other ArduPilot sources. */
#ifdef ALL_CHANNELS
#undef ALL_CHANNELS
#endif

#define RTT_RCOUT_MAX_CHANNELS 16

namespace RTT
{

/*
 * Channel-to-timer mapping entry.
 * 'tim'       — TIM_TypeDef pointer (e.g. TIM1, TIM4, TIM12)
 * 'channel' — 0-based capture/compare channel index (0..3 for CCR1..CCR4)
 */
struct tim_channel_config {
    TIM_TypeDef *tim;
    uint8_t      channel;   // 0-based: 0=CCR1, 1=CCR2, 2=CCR3, 3=CCR4
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

    /* ---- TIM register access ----
     * Instead of rt_device_pwm handles, store the TIM_TypeDef pointer
     * and 0-based channel number for each output channel.
     */
    TIM_TypeDef *_tim_dev[RTT_RCOUT_MAX_CHANNELS];     // timer peripheral
    uint8_t      _tim_chan[RTT_RCOUT_MAX_CHANNELS];     // 0-based channel

    /* Per-timer state for frequency / prescaler tracking.
     * Up to 3 timers used on CUAV V5: TIM1, TIM4, TIM12.
     */
    struct timer_state {
        TIM_TypeDef *tim;
        uint32_t     clock_hz;     // timer input clock (after ×2 from APB)
        uint16_t     period_arr;   // current ARR value (period-1)
        uint16_t     prescaler;    // current PSC value
    };
    static constexpr uint8_t _num_timers = 3;
    timer_state _timer[_num_timers];

    /* Find the timer_state index for a given TIM_TypeDef*, or -1. */
    int8_t _timer_idx(TIM_TypeDef *tim) const;

    /* Enable RCC clock for a timer peripheral. */
    static void _timer_clock_enable(TIM_TypeDef *tim);

    /* Configure CCMR registers for PWM1 output mode with preload. */
    static void _timer_ccmr_init(TIM_TypeDef *tim);

    /* Program ARR and PSC for a given timer and frequency.
     * Returns the computed ARR value (period-1).
     */
    uint16_t _timer_set_freq_internal(TIM_TypeDef *tim, uint16_t freq_hz);

    /* Write a pulse width in timer counts to a specific CCR. */
    static void _timer_write_ccr(TIM_TypeDef *tim, uint8_t channel, uint32_t ccr_val);

    /* Enable/disable a capture/compare output channel via CCER. */
    static void _timer_ccer_enable(TIM_TypeDef *tim, uint8_t channel, bool enable);

    /* Get the total number of channels per timer based on hw mapping. */
    static uint8_t _timer_num_channels(TIM_TypeDef *tim);

    /* ---- Low-level helpers ---- */
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

    // Timer clock constants
    static constexpr uint32_t TIM1_CLOCK = 216000000U;   // APB2=108MHz, ×2
    static constexpr uint32_t TIM4_CLOCK = 108000000U;   // APB1= 54MHz, ×2
    static constexpr uint32_t TIM12_CLOCK = 108000000U;  // APB1= 54MHz, ×2
};

} // namespace RTT
