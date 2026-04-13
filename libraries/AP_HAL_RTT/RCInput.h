/*
 * AP_HAL_RTT — RCInput (aligned with ChibiOS)
 * Uses local buffer + mutex pattern: _timer_tick() copies data from
 * AP_RCProtocol to local _rc_values[] under mutex; read() reads from
 * the local buffer. This decouples the main thread from the protocol
 * layer's internal _new_input flag.
 */

#pragma once

#include <AP_HAL/RCInput.h>
#include "HAL_RTT_Namespace.h"
#include "Semaphores.h"

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
    uint16_t _rc_values[RC_INPUT_MAX_CHANNELS] = {};
    uint64_t _last_read = 0;
    uint8_t  _num_channels = 0;
    Semaphore rcin_mutex;
    int16_t  _rssi = -1;
    int16_t  _rx_link_quality = -1;
    uint32_t _rcin_timestamp_last_signal = 0;
    bool     _init = false;
};

} // namespace RTT
