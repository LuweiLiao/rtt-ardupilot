/*
 * AP_HAL_RTT — RCInput (aligned with ChibiOS)
 * Delegates to AP_RCProtocol for serial RC processing.
 * _timer_tick() called from dedicated rcin thread at 1 kHz.
 */

#pragma once

#include <AP_HAL/RCInput.h>
#include "HAL_RTT_Namespace.h"

#ifndef RC_INPUT_MAX_CHANNELS
#define RC_INPUT_MAX_CHANNELS 18
#endif

namespace RTT
{

class RCInput : public AP_HAL::RCInput
{
public:
    void init() override;
    bool new_input() override;
    uint8_t num_channels() override;
    uint16_t read(uint8_t ch) override;
    uint8_t read(uint16_t* periods, uint8_t len) override;

    void pulse_input_enable(bool enable) override;

    int16_t get_rssi(void) override { return _rssi; }
    int16_t get_rx_link_quality(void) override { return _rx_link_quality; }

    void _timer_tick(void);

private:
    int16_t _rssi = -1;
    int16_t _rx_link_quality = -1;
    bool _init = false;
};

} // namespace RTT
