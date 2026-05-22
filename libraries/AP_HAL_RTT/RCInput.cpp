/*
 * AP_HAL_RTT — RCInput (aligned with ChibiOS)
 * Uses local buffer + mutex pattern: _timer_tick() copies data from
 * AP_RCProtocol to local _rc_values[] under mutex; read() reads from
 * the local buffer. This decouples the main thread from the protocol
 * layer's internal _new_input flag.
 */

#include "RCInput.h"
#include <AP_HAL/AP_HAL.h>

#include <AP_RCProtocol/AP_RCProtocol_config.h>
#if AP_RCPROTOCOL_ENABLED
#include <AP_RCProtocol/AP_RCProtocol.h>
#endif

#include <string.h>

namespace RTT
{

void RCInput::init()
{
#if AP_RCPROTOCOL_ENABLED
    AP::RC().init();
#endif
    _init = true;
}

bool RCInput::new_input()
{
    if (!_init) {
        return false;
    }
    bool valid;
    {
        WITH_SEMAPHORE(rcin_mutex);
        valid = _rcin_timestamp_last_signal != _last_read;
        _last_read = _rcin_timestamp_last_signal;
    }
    return valid;
}

uint8_t RCInput::num_channels()
{
    if (!_init) {
        return 0;
    }
    return _num_channels;
}

uint16_t RCInput::read(uint8_t ch)
{
    if (!_init || ch >= MIN(RC_INPUT_MAX_CHANNELS, _num_channels)) {
        return 0;
    }
    uint16_t v;
    {
        WITH_SEMAPHORE(rcin_mutex);
        v = _rc_values[ch];
    }
    return v;
}

uint8_t RCInput::read(uint16_t* periods, uint8_t len)
{
    if (!_init) {
        return 0;
    }
    if (len > RC_INPUT_MAX_CHANNELS) {
        len = RC_INPUT_MAX_CHANNELS;
    }
    {
        WITH_SEMAPHORE(rcin_mutex);
        memcpy(periods, _rc_values, len * sizeof(periods[0]));
    }
    return len;
}

void RCInput::pulse_input_enable(bool enable)
{
    // RTT does not have ICU/EICU hardware capture; RC input decoding
    // is handled via rcprot.update() polling in _timer_tick().
    // This stub matches the ChibiOS virtual interface but is a no-op.
    (void)enable;
}

void RCInput::_timer_tick(void)
{
    if (!_init) {
        return;
    }
#if AP_RCPROTOCOL_ENABLED
    AP_RCProtocol &rcprot = AP::RC();
    rcprot.update();

    if (rcprot.new_input()) {
        WITH_SEMAPHORE(rcin_mutex);
        _rcin_timestamp_last_signal = AP_HAL::micros();
        _num_channels = rcprot.num_channels();
        _num_channels = MIN(_num_channels, RC_INPUT_MAX_CHANNELS);
        rcprot.read(_rc_values, _num_channels);
        _rssi = rcprot.get_RSSI();
        _rx_link_quality = rcprot.get_rx_link_quality();
    }
#endif
}

} // namespace RTT
