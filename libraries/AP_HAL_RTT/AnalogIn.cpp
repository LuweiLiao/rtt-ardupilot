/*
 * AP_HAL_RTT — AnalogIn implementation
 * Uses RT-Thread ADC device framework.
 *
 * CUAV V5 ADC channel layout (matching ChibiOS fmuv5 hwdef):
 *   ADC1_CH0  (PA0) → BATT_VOLTAGE_SENS   SCALE(1)
 *   ADC1_CH1  (PA1) → BATT_CURRENT_SENS   SCALE(1)
 *   ADC1_CH2  (PA2) → BATT2_VOLTAGE_SENS  SCALE(1)
 *   ADC1_CH3  (PA3) → BATT2_CURRENT_SENS  SCALE(1)
 *   ADC1_CH14 (PC4) → SPARE1              SCALE(1)
 *   ADC1_CH4  (PA4) → SPARE2              SCALE(1)
 *   ADC1_CH8  (PB0) → RSSI_IN             SCALE(1)
 *   ADC1_CH10 (PC0) → VDD_5V_SENS         SCALE(2)
 *   ADC1_CH11 (PC1) → SCALED_V3V3         SCALE(2)
 */

#include "AnalogIn.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <AP_HAL/board/rtt.h>

#if defined(RT_USING_ADC)
extern "C" {
#include <drivers/adc.h>
}
#endif

#define ADC_VREF 3.3f
#define ADC_RESOLUTION 4095.0f
#define VOLTAGE_SCALING (ADC_VREF / ADC_RESOLUTION)

#ifndef RTT_ADC_VDD_5V_SENS_CHANNEL
#define RTT_ADC_VDD_5V_SENS_CHANNEL 10
#endif
#ifndef RTT_ADC_SCALED_V3V3_CHANNEL
#define RTT_ADC_SCALED_V3V3_CHANNEL 11
#endif
#ifndef RTT_ADC_VDD_5V_SCALE
#define RTT_ADC_VDD_5V_SCALE 2.0f
#endif

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
    return read_average() * VOLTAGE_SCALING * _scale;
}

float AnalogSource::voltage_latest()
{
    return _latest_value * VOLTAGE_SCALING * _scale;
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
    return _power_flags;
}

void AnalogIn::_timer_tick()
{
#if defined(RT_USING_ADC)
    rt_device_t dev = rt_device_find("adc1");
    if (!dev) return;

    for (int16_t i = 0; i < RTT_ANALOG_MAX_CHANNELS; i++) {
        rt_uint32_t val = rt_adc_read((struct rt_adc_device *)dev, i);
        _sources[i]._add_sample((float)(val & 0xFFF));
    }

    rt_uint32_t vdd_raw = rt_adc_read((struct rt_adc_device *)dev, RTT_ADC_VDD_5V_SENS_CHANNEL);
    float v5 = (float)(vdd_raw & 0xFFF) * VOLTAGE_SCALING * RTT_ADC_VDD_5V_SCALE;
    if (v5 > 0.5f) {
        _board_voltage = v5;
    }

    uint16_t flags = 0;
    if (_board_voltage > 4.5f) {
        flags |= MAV_POWER_STATUS_BRICK_VALID;
    }
    if (_board_voltage > 4.0f && _board_voltage < 5.5f) {
        flags |= MAV_POWER_STATUS_USB_CONNECTED;
    }
    _power_flags = flags;
    _accumulated_power_flags |= flags;
#endif
}

} // namespace RTT
