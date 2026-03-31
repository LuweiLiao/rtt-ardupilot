/*
 * AP_HAL_RTT — AnalogIn
 * Board-independent ADC via RT-Thread ADC framework.
 * hwdef-driven channel scaling for CUAV V5 (same as ChibiOS fmuv5).
 */

#pragma once

#include <AP_HAL/AnalogIn.h>
#include "HAL_RTT_Namespace.h"

#define RTT_ANALOG_MAX_CHANNELS 16

namespace RTT
{

class AnalogSource : public AP_HAL::AnalogSource
{
public:
    AnalogSource() : _pin(-1), _value(0.0f), _latest_value(0.0f), _sum(0), _count(0), _scale(1.0f) {}
    AnalogSource(int16_t pin, float scale = 1.0f)
        : _pin(pin), _value(0.0f), _latest_value(0.0f), _sum(0), _count(0), _scale(scale) {}
    float read_average() override;
    float read_latest() override;
    bool set_pin(uint8_t p) override WARN_IF_UNUSED;
    float voltage_average() override;
    float voltage_latest() override;
    float voltage_average_ratiometric() override;
    void _add_sample(float v);
    void set_scale(float s) { _scale = s; }

private:
    int16_t _pin;
    float _value;
    float _latest_value;
    float _sum;
    uint16_t _count;
    float _scale;
};

class AnalogIn : public AP_HAL::AnalogIn
{
public:
    void init() override;
    AP_HAL::AnalogSource* channel(int16_t n) override;
    float board_voltage() override;
    float servorail_voltage() override;
    uint16_t power_status_flags() override;
    uint16_t accumulated_power_status_flags(void) const override { return _accumulated_power_flags; }
    void _timer_tick();

private:
    AnalogSource _sources[RTT_ANALOG_MAX_CHANNELS];
    float _board_voltage = 5.0f;
    float _servorail_voltage = 0.0f;
    uint16_t _power_flags = 0;
    uint16_t _accumulated_power_flags = 0;
    bool _initialized = false;
};

} // namespace RTT
