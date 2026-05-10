/*
 * AP_HAL_RTT — AnalogIn for CUAV V5
 * Direct STM32F7 CMSIS register access for ADC1.
 *
 * Bypasses RTT's drv_adc.c HAL layer to avoid SPI interference.
 */

#include "AnalogIn.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/board/rtt.h>
#include <stm32f7xx.h>
#include <rtthread.h>

namespace RTT
{

// Logical channel 0-7 → ADC1 channel number
static const uint8_t _ch_map[8] = {0, 1, 2, 3, 8, 10, 11, 14};

static bool _adc_inited = false;

// Debug diagnostics — kept for GDB inspection
static volatile uint32_t rtt_adc_timeout_count = 0;
static volatile uint32_t rtt_adc_last_raw = 0;

static void _adc_init_once(void)
{
    if (_adc_inited) return;

    // Enable GPIO clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    // Read back to ensure clock is on
    (void)RCC->AHB1ENR;

    // Configure ADC pins as analog
    GPIOA->MODER |= 0xFF;        // PA0-3 analog (BATT/BATT2 VOLTAGE/CURRENT)
    GPIOB->MODER |= 0x3;         // PB0 analog (RSSI)
    GPIOC->MODER |= 0x30F;       // PC0,PC1,PC4 analog (VDD_5V/3V3/SPARE)

    // Enable ADC1 clock on APB2
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    // Read back to ensure clock is on
    (void)RCC->APB2ENR;

    // ADC common prescaler: PCLK2/4
    ADC123_COMMON->CCR = (ADC123_COMMON->CCR & ~(3U << 16)) | (1U << 16);

    // Enable temperature sensor and Vrefint for ADC channels 10/11
    ADC123_COMMON->CCR |= ADC_CCR_TSVREFE;

    // Disable ADC1 and configure CR1
    ADC1->CR2 = 0;
    ADC1->CR1 = 0;  // 12-bit, no scan

    // First ADON write: wake up from power-down
    ADC1->CR2 = ADC_CR2_ADON;

    // Wait for ADC internal regulator to stabilize after wake-up
    for (volatile uint32_t i = 0; i < 1000; i++) { __NOP(); }

    // Verify ADON took effect
    if (!(ADC1->CR2 & ADC_CR2_ADON)) {
        return;  // ADC failed to wake
    }

    // Second ADON write: fully enable with EOCS
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_EOCS;

    // Wait for ADC ready for conversion
    for (volatile uint32_t i = 0; i < 1000; i++) { __NOP(); }

    _adc_inited = true;
}

static uint32_t _adc_read(uint8_t ch)
{
    // Sample time 112 cycles
    if (ch < 10) {
        ADC1->SMPR2 = (7U << (ch * 3));
    } else {
        ADC1->SMPR1 = (7U << ((ch - 10) * 3));
    }
    ADC1->SQR3 = ch;
    ADC1->SQR1 = 0;

    // Clear all status flags, then start conversion
    ADC1->SR = 0;

    // If ADC is still busy from prior conversion (STRP stuck), reset and restart
    if (ADC1->SR & ADC_SR_STRT) {
        ADC1->CR2 &= ~ADC_CR2_ADON;   // disable to clear STRP
        __NOP(); __NOP(); __NOP();
        ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_EOCS;  // re-enable without SWSTART
    }
    ADC1->CR2 |= ADC_CR2_SWSTART;

    // Poll EOC with simple counter timeout (~0.5ms at 216MHz)
    // ADC conversion typically completes in <1us; 100000 loops is generous.
    for (volatile uint32_t t = 0; t < 100000; t++) {
        if (ADC1->SR & ADC_SR_EOC) {
            uint32_t val = ADC1->DR & 0xFFF;
            rtt_adc_last_raw = val;
            return val;
        }
    }
    // Timeout — read DR to clear flags
    rtt_adc_timeout_count++;
    (void)ADC1->DR;
    return 0;
}

/* AnalogSource */
void AnalogSource::_add_sample(float v) { _sum += v; _count++; _latest_value = v; }
float AnalogSource::read_average() { if (_count > 0) { _value = _sum / _count; _sum = 0; _count = 0; } return _value; }
float AnalogSource::read_latest() { return _latest_value; }
bool AnalogSource::set_pin(uint8_t p) { _pin = (int16_t)p; return true; }
float AnalogSource::voltage_average() { return read_average() * (3.3f / 4096.0f) * _scale; }
float AnalogSource::voltage_latest() { return _latest_value * (3.3f / 4096.0f) * _scale; }
float AnalogSource::voltage_average_ratiometric() { return voltage_average(); }

/* AnalogIn */
void AnalogIn::init() {
    if (_initialized) return;
    _adc_init_once();
    _initialized = true;
}

AP_HAL::AnalogSource* AnalogIn::channel(int16_t n) {
    init();
    if (n < 0 || n >= RTT_ANALOG_MAX_CHANNELS) return nullptr;
    _sources[n].set_scale((n == 10 || n == 11) ? 2.0f : 1.0f);
    IGNORE_RETURN(_sources[n].set_pin(n));
    return &_sources[n];
}

float AnalogIn::board_voltage() { return _board_voltage; }
float AnalogIn::servorail_voltage() { return _servorail_voltage; }
uint16_t AnalogIn::power_status_flags() { return _power_flags; }

void AnalogIn::_timer_tick()
{
    if (!_initialized) return;

    // Read all 8 mapped channels
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t raw = _adc_read(_ch_map[i]);
        if (i < RTT_ANALOG_MAX_CHANNELS) {
            _sources[i]._add_sample((float)raw);
        }
        // Debug removed: ADC FIRST message pollutes MAVLink.
    }

    // Debug removed: ADC STATUS rt_kprintf pollutes MAVLink over CDC.
    // Use GDB to inspect rtt_adc_timeout_count / rtt_adc_last_raw if needed.

    // Board voltage: ch_map[5]=ch10 (PC0) already read above, use _sources[5]
    // _sources[5] scale is set to 2.0 in channel() for ch10/11
    float vdd = _sources[5].read_latest() * (3.3f / 4096.0f) * 2.0f;
    if (vdd > 0.5f) {
        _board_voltage = vdd;
    }

    uint16_t flags = 0;
    if (_board_voltage > 4.5f) flags |= (uint16_t)PowerStatusFlag::BRICK_VALID;
    if (_board_voltage > 4.0f && _board_voltage < 5.5f) flags |= (uint16_t)PowerStatusFlag::USB_CONNECTED;
    _power_flags = flags;
    _accumulated_power_flags |= flags;
}

} // namespace RTT
