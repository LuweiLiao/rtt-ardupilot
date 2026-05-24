/*
 * AP_HAL_RTT — AnalogIn for CUAV V5 (STM32F767)
 *
 * Uses the hal_adc_lld_rtt driver for CMSIS-register-level ADC access.
 * ChibiOS reference: libraries/AP_HAL_ChibiOS/AnalogIn.cpp
 *
 * This is a simplified polled-mode implementation: on every _timer_tick(),
 * we sequentially convert each channel using the LLD's single-conversion
 * function.  No DMA is used.  This is adequate for ~8 channels at 100Hz
 * (800 conversions/s, ~30us each = ~2.4% CPU).
 *
 * STM32F767 uses standard ADCv2 peripheral (same as F4).
 * CMSIS ADC_TypeDef layout (stm32f767xx.h):
 *   SR(0x00), CR1(0x04), CR2(0x08), SMPR1(0x0C), SMPR2(0x10),
 *   HTR(0x24), LTR(0x28), SQR1(0x2C), SQR2(0x30), SQR3(0x34), DR(0x4C)
 *
 * ADC clock: PCLK2=108MHz via APB2 prescaler.
 * ADCPRE=1 -> PCLK2/4 = 27MHz (within 36MHz max).
 * Sampling time: 480 cycles (~17.8us) for internal temp sensor,
 *                15 cycles (~0.56us) for external channels.
 */

#include "AnalogIn.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/board/rtt.h>
#include <stm32f7xx.h>
#include <rtthread.h>

/* ADC LLD */
extern "C" {
#include "drivers/hal_adc_lld_rtt.h"
}

namespace RTT
{

// Logical (hwdef index) → ADC1 channel number
// CUAV V5 analog pins:
//   ch0=ADC_IN0(PIN1_1),  ch1=ADC_IN1(PIN1_2),
//   ch2=ADC_IN2(PIN1_3),  ch3=ADC_IN3(PIN1_4),
//   ch4=ADC_IN8(VDD_5V_SENS on PA3), ch5=ADC_IN10(BATT_CURR_SENS on PC0),
//   ch6=ADC_IN11(BATT_VOLT_SENS on PC1),  ch7=ADC_IN14(PRESS_ADC on PC4),
//   ch8=ADC_IN4(VDD_3V3_SENS on PA4)
static const uint8_t _ch_map[9] = {0, 1, 2, 3, 8, 10, 11, 14, 4};

// Index of the 5V rail sense (for board_voltage calculation)
#define VDD_5V_SENS_INDEX 6

// Sampling time: 480 cycles for internal channels (temp/VREF),
// 15 cycles for fast external channels
#define ADC_SAMPLE_TIME_FAST       ADC_LLD_SAMPLE_15
#define ADC_SAMPLE_TIME_SLOW       ADC_LLD_SAMPLE_480

static bool _adc_inited = false;
static volatile uint32_t rtt_adc_conversion_count = 0;
static volatile uint32_t rtt_adc_last_raw = 0;
static volatile uint32_t rtt_adc_timeout_count = 0;

/* ======================================================================== */
/* ADC LLD wrapper                                                          */
/* ======================================================================== */

static void _adc_init_once(void)
{
    if (_adc_inited) { return; }

    // Configure GPIO pins for analog mode
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    // GPIOA: PA0,1,2,3,4,5,6,7,8 as analog (MODER bits 0-17 = 0x3FFFF)
    // PA0=ANA0(ADC_IN0), PA1=ANA1(ADC_IN1), PA2=ANA2(ADC_IN2),
    // PA3=ANA3(ADC_IN8), PA4=VDD_3V3_SENS(ADC_IN4), PA5=ANA5(ADC_IN5),
    // PA6=ANA6(ADC_IN6), PA7=ANA7(ADC_IN7)
    GPIOA->MODER |= 0x3FFFF;   // PA0~PA8 analog

    // GPIOB: PB0, PB1 as analog (ADC_IN8, ADC_IN9)
    GPIOB->MODER |= 0xF;       // PB0, PB1 analog

    // GPIOC: PC0, PC1, PC2, PC3, PC4 as analog (ADC_IN10~14)
    GPIOC->MODER |= 0x3FF;     // PC0~PC4 analog

    // Initialize ADC1 via LLD
    // ADCPRE=1 means PCLK2/4 = 27MHz (within 36MHz limit)
    // Enable TSVREFE for internal temp sensor / VREFINT
    if (!adc_lld_init_rtt(ADC1, ADC_LLD_ADCPRE_DIV4, true)) {
        // ADC initialization failed
        return;
    }

    _adc_inited = true;
}

static uint16_t _adc_read(uint8_t ch)
{
    if (!_adc_inited) {
        return 0;
    }

    // Internal channels (sensor, VREFINT, VBAT) need longer sampling time
    // per RM0410 §19: internal sensor requires sampling time >= 10us
    // At 27MHz ADCCLK, 480 cycles = 480/27MHz = 17.8us ✅
    uint32_t sample_time;
    if (ch >= 16) {
        sample_time = ADC_SAMPLE_TIME_SLOW;  // 480 cycles for internal
    } else {
        sample_time = ADC_SAMPLE_TIME_FAST;  // 15 cycles for external
    }

    uint16_t result = adc_lld_convert_channel_rtt(ADC1, ch, sample_time);

    if (result == 0xFFFF) {
        // Timeout
        rtt_adc_timeout_count++;
        return 0;
    }

    rtt_adc_last_raw = result;
    rtt_adc_conversion_count++;

    return result;
}

/* ======================================================================== */
/* AnalogSource                                                             */
/* ======================================================================== */

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
    return read_average() * (3.3f / 4096.0f) * _scale;
}

float AnalogSource::voltage_latest()
{
    return _latest_value * (3.3f / 4096.0f) * _scale;
}

float AnalogSource::voltage_average_ratiometric()
{
    return voltage_average();
}

/* ======================================================================== */
/* AnalogIn                                                                 */
/* ======================================================================== */

void AnalogIn::init()
{
    if (_initialized) return;
    _adc_init_once();
    _initialized = true;
}

AP_HAL::AnalogSource* AnalogIn::channel(int16_t n)
{
    init();
    if (n < 0 || n >= RTT_ANALOG_MAX_CHANNELS) {
        return nullptr;
    }

    // Set scale factor based on channel type.
    // Channels that map to ADC_IN10 (BATT_CURR, logical ch5) and
    // ADC_IN11 (BATT_VOLT, logical ch6) have voltage dividers (scale=2).
    // Channel VDD_5V_SENS (logical ch6→ADC_IN11) also uses scale=2.
    if ((uint8_t)n < ARRAY_SIZE(_ch_map)) {
        uint8_t adc_ch = _ch_map[n];
        if (adc_ch == 10 || adc_ch == 11) {
            // Battery voltage/current sense - voltage divider needs scaling
            _sources[n].set_scale(2.0f);
        } else if (adc_ch == 14) {
            // PRESSURE ADC - divider on some boards
            _sources[n].set_scale(1.0f);
        } else {
            _sources[n].set_scale(1.0f);
        }
    } else {
        _sources[n].set_scale(1.0f);
    }

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
    if (!_initialized) return;

    // Convert each channel sequentially
    for (uint8_t i = 0; i < ARRAY_SIZE(_ch_map); i++) {
        uint16_t raw = _adc_read(_ch_map[i]);
        if (i < RTT_ANALOG_MAX_CHANNELS) {
            _sources[i]._add_sample((float)raw);
        }
    }

    // Compute board voltage from VDD_5V_SENS channel
    // ADC_IN11 with 2:1 divider: V = (count/4096) * 3.3V * 2
    float vdd = _sources[VDD_5V_SENS_INDEX].read_latest() *
                (3.3f / 4096.0f) * 2.0f;
    if (vdd > 0.5f) {
        _board_voltage = vdd;
    }

    // Update power status flags
    uint16_t flags = 0;
    if (_board_voltage > 4.5f) {
        flags |= (uint16_t)PowerStatusFlag::BRICK_VALID;
    }
    if (_board_voltage > 4.0f && _board_voltage < 5.5f) {
        flags |= (uint16_t)PowerStatusFlag::USB_CONNECTED;
    }
    _power_flags = flags;
    _accumulated_power_flags |= flags;
}

} // namespace RTT
