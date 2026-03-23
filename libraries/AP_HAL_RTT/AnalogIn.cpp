/*
 * AP_HAL_RTT — AnalogIn implementation
 * Uses RT-Thread ADC device framework.
 * VDD_5V_SENS channel provides board_voltage().
 */

#include "AnalogIn.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>

#if defined(RT_USING_ADC)
extern "C" {
#include <drivers/adc.h>
}
#endif

#define ADC_VREF 3.3f
#define ADC_RESOLUTION 4095.0f
#define VOLTAGE_SCALING (ADC_VREF / ADC_RESOLUTION)

namespace RTT
{

void AnalogSource::_add_sample(float v)
{
    _sum += v;
    _count++;
    _latest_value = v;
}

float AnalogSource::read_average()
{
    if (_count > 0) {
        _value = _sum / _count;
        _sum = 0;
        _count = 0;
    }
    return _value;
}

float AnalogSource::read_latest()
{
    return _latest_value;
}

bool AnalogSource::set_pin(uint8_t p)
{
    _pin = (int16_t)p;
    return true;
}

float AnalogSource::voltage_average()
{
    return read_average() * VOLTAGE_SCALING;
}

float AnalogSource::voltage_latest()
{
    return _latest_value * VOLTAGE_SCALING;
}

float AnalogSource::voltage_average_ratiometric()
{
    return voltage_average();
}

void AnalogIn::init()
{
    if (_initialized) return;
#if defined(RT_USING_ADC)
    rt_device_t dev = rt_device_find("adc1");
    if (dev) {
        for (uint8_t ch = 0; ch < 16; ch++) {
            rt_adc_enable((struct rt_adc_device *)dev, ch);
        }
    }
#endif
    _initialized = true;
}

AP_HAL::AnalogSource* AnalogIn::channel(int16_t n)
{
    init();
    if (n < 0 || n >= RTT_ANALOG_MAX_CHANNELS) return nullptr;
    IGNORE_RETURN(_sources[n].set_pin(n));
    return &_sources[n];
}

float AnalogIn::board_voltage()
{
    return _board_voltage;
}

float AnalogIn::servorail_voltage()
{
    return _servorail_voltage;
}

uint16_t AnalogIn::power_status_flags()
{
    return 0;
}

void AnalogIn::_timer_tick()
{
#if defined(RT_USING_ADC)
    rt_device_t dev = rt_device_find("adc1");
    if (!dev) return;

    for (int16_t i = 0; i < RTT_ANALOG_MAX_CHANNELS; i++) {
        if (_sources[i].read_latest() >= 0 || true) {
            rt_uint32_t val = rt_adc_read((struct rt_adc_device *)dev, i);
            _sources[i]._add_sample((float)(val & 0xFFF));
        }
    }

    /* VDD_5V_SENS: scale factor 2x voltage divider */
    rt_uint32_t vdd = rt_adc_read((struct rt_adc_device *)dev, 10);
    float v5 = (float)(vdd & 0xFFF) * VOLTAGE_SCALING * 2.0f;
    if (v5 > 0.5f) _board_voltage = v5;
#endif
}

} // namespace RTT
