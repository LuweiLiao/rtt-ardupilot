/*
 * AP_HAL_RTT — AnalogIn for CUAV V5
 * Direct STM32F7 CMSIS register access for ADC1.
 *
 * WARNING: STM32F7 ADCv3 register offsets differ from the F4-compatible
 * CMSIS ADC_TypeDef structure!  The struct maps:
 *   SMPR1 → 0x0C (real F7: CFGR)   ← WRONG!
 *   SMPR2 → 0x10 (real F7: CFGR2)  ← WRONG!
 * We use hard-coded offsets to bypass this.
 *
 * F7 ADC register map:
 *   0x000: ISR     0x004: IER      0x008: CR       0x00C: CFGR
 *   0x010: CFGR2   0x014: SMPR1    0x018: SMPR2    0x01C: TR1
 *   0x020: TR2     0x024: TR3      0x028: -        0x02C: SQR1
 *   0x030: SQR2    0x034: SQR3     0x038: SQR4     0x03C: JSQR
 *   0x04C: DR
 *
 * Reference: ChibiOS ADCv3 hal_adc_lld.c
 */

#include "AnalogIn.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/board/rtt.h>
#include <stm32f7xx.h>
#include <rtthread.h>

namespace RTT
{

// Logical (hwdef index) → ADC1 channel number
static const uint8_t _ch_map[9] = {0, 1, 2, 3, 8, 10, 11, 14, 4};
#define VDD_5V_SENS_INDEX 6

static bool _adc_inited = false;
static volatile uint32_t rtt_adc_timeout_count = 0;
static volatile uint32_t rtt_adc_last_raw = 0;

// F7 ADC1 base address
#define F7_ADC1      ((__IO uint32_t*)0x40012000)

// Register offsets from ADC base
#define OFF_ISR      0x00
#define OFF_IER      0x04
#define OFF_CR       0x08
#define OFF_CFGR     0x0C
#define OFF_CFGR2    0x10
#define OFF_SMPR1    0x14
#define OFF_SMPR2    0x18
#define OFF_TR1      0x1C
#define OFF_SQR1     0x2C
#define OFF_SQR2     0x30
#define OFF_SQR3     0x34
#define OFF_SQR4     0x38
#define OFF_DR       0x4C

// ADC register access macros
#define ADC_ISR      (F7_ADC1[OFF_ISR/4])
#define ADC_IER      (F7_ADC1[OFF_IER/4])
#define ADC_CR       (F7_ADC1[OFF_CR/4])
#define ADC_CFGR     (F7_ADC1[OFF_CFGR/4])
#define ADC_CFGR2    (F7_ADC1[OFF_CFGR2/4])
#define ADC_SMPR1    (F7_ADC1[OFF_SMPR1/4])
#define ADC_SMPR2    (F7_ADC1[OFF_SMPR2/4])
#define ADC_SQR1     (F7_ADC1[OFF_SQR1/4])
#define ADC_SQR3     (F7_ADC1[OFF_SQR3/4])
#define ADC_DR       (F7_ADC1[OFF_DR/4])

// F7 native ADC bit definitions
#define CR_ADVREGEN    (1U << 28)
#define CR_ADEN        (1U << 0)
#define CR_ADCAL       (1U << 31)
#define CR_ADCALDIF    (1U << 30)
#define CR_ADSTART     (1U << 2)
#define CR_ADDIS       (1U << 1)
#define CR_ADSTP       (1U << 4)
#define ISR_ADRDY      (1U << 0)
#define ISR_EOC        (1U << 2)
#define ISR_EOS        (1U << 3)
#define ISR_OVR        (1U << 4)
#define CFGR_CONT      (1U << 13)      /* Continuous conversion */
#define CFGR_DMAEN     (1U << 0)
#define CFGR_DMACFG    (1U << 1)
// NOTE: F7 CFGR bit12 is AUTOFF (auto-off), NOT JQDIS (that's H7 ADCv3).
// Do NOT set bit12 unless you want the ADC to auto-disable after conversion.
#define CFGR_RES_12BIT (0U << 3)
#define CFGR_EOCS_UPON_EACH (1U << 10)

// ADC common register (CCR) definitions for STM32F7 (RM0410 §19.3)
// F7 has only ADCPRE (bits 16-17, 2-bit field):
//   ADCPRE=00 → PCLK2/2    ADCPRE=01 → PCLK2/4
//   ADCPRE=10 → PCLK2/6    ADCPRE=11 → PCLK2/8
// NOTE: F7 does NOT have PRESC (bits 18-21) or CKMODE — those are H7 ADCv3 only!
// Use CMSIS-defined ADC_CCR_ADCPRE_Msk, ADC_CCR_ADCPRE_0, ADC_CCR_ADCPRE_1 instead.

static void _adc_init_once(void)
{
    if (_adc_inited) return;

    // Enable GPIO clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    // Configure ADC pins as analog
    GPIOA->MODER |= 0x3FF;       // PA0-4 analog
    GPIOB->MODER |= 0x3;         // PB0 analog
    GPIOC->MODER |= 0x30F;       // PC0,PC1,PC4 analog

    // Enable ADC1 clock on APB2
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    // ADC clock: Synchronous mode from PCLK2/APB2 = 108MHz, ADCPRE=01 = /4 = 27MHz
    // On STM32F7, the ADC max clock is 36MHz (RM0410 §19.3.7).
    // F7 CCR has ADCPRE (bits 16-17, 2-bit):
    //   00 = PCLK2/2 = 54MHz (overspeed!)
    //   01 = PCLK2/4 = 27MHz (safe) ✓
    //   10 = PCLK2/6 = 18MHz
    //   11 = PCLK2/8 = 13.5MHz
    // NOTE: F7 does NOT have PRESC bits 18-21 — that is H7 ADCv3 only!
    // Previous code wrote (4<<18) which F7 hardware ignored, leaving ADCPRE=00=54MHz → ADC overclocked.
    ADC123_COMMON->CCR = (ADC123_COMMON->CCR & ~ADC_CCR_ADCPRE_Msk) |
                         ADC_CCR_ADCPRE_0 |   // ADCPRE=01 = PCLK2/4 = 27MHz
                         ADC_CCR_TSVREFE;     // Temperature sensor enable

    // ==== ADC power-up (ChibiOS ADCv3 sequence) ====
    // 1. Enable voltage regulator
    ADC_CR = 0U;                     // Clear CR first (RM requirement)
    ADC_CR = CR_ADVREGEN;            // Enable voltage regulator

    // 2. Wait for regulator startup (~20µs)
    for (volatile uint32_t i = 0; i < 10000U; i++) { __NOP(); }

    // 3. Calibrate: two-step write per RM0410 — first set ADCALDIF,
    //    THEN add ADCAL (ChibiOS pattern).  Single-write may not work.
    //    Differential calibration for master ADC.
    ADC_CR = CR_ADVREGEN | CR_ADCALDIF;
    ADC_CR = CR_ADVREGEN | CR_ADCALDIF | CR_ADCAL;
    while (ADC_CR & CR_ADCAL) { __NOP(); }

    // 4. Wait post-calibration settling
    for (volatile uint32_t i = 0; i < 5000U; i++) { __NOP(); }

    // 5. Single-ended calibration for master ADC.
    ADC_CR = CR_ADVREGEN;
    ADC_CR = CR_ADVREGEN | CR_ADCAL;
    while (ADC_CR & CR_ADCAL) { __NOP(); }

    // 6. Wait post-calibration settling
    for (volatile uint32_t i = 0; i < 5000U; i++) { __NOP(); }

    // 7. Enable ADC (OR-assignment to preserve ADVREGEN, per ChibiOS pattern)
    ADC_CR |= CR_ADEN;
    {
        volatile uint32_t timeout = 100000U;
        while (!(ADC_ISR & ISR_ADRDY) && timeout) {
            __NOP();
            timeout--;
        }
        ADC_ISR = ISR_ADRDY;  // Clear ADRDY
    }

    // 8. Configure CFGR: single conversion, software trigger, EOC on each conv
    //    Default: CONT=0 (single), EXTSEL=0 (SW start), RES=12bit
    //    NOTE: F7 CFGR bit12 is AUTOFF, NOT JQDIS (H7).  Must NOT set it.
    //    Only EOCS=1 (bit10): EOC raised after each conversion in the sequence.
    ADC_CFGR = (1U << 10);           // EOCS=1

    // 9. Clear any stale status
    (void)ADC_ISR;
    (void)ADC_DR;

    _adc_inited = true;

    if (!(ADC_ISR & ISR_ADRDY) && !rtt_adc_timeout_count) {
        rtt_adc_timeout_count = 0xDEAD;
    }
}

static uint32_t _adc_read(uint8_t ch)
{
    // Set sample time (SMP=7 = 480 cycles, at F7 native SMPR offset 0x14)
    if (ch < 10) {
        ADC_SMPR2 = (7U << (ch * 3));
    } else {
        ADC_SMPR1 = (7U << ((ch - 10) * 3));
    }

    // Set channel in SQR3 (1-channel sequence)
    ADC_SQR3 = ch;
    ADC_SQR1 = 0;   // L=0 means 1 conversion in sequence

    // Clear stale flags: EOC, OVR (write-1-to-clear)
    ADC_ISR = ISR_EOC | ISR_OVR;
    // Read DR to clear any stale EOC
    (void)ADC_DR;
    // Ensure all register writes complete before starting conversion
    __DSB();
    __ISB();

    // Start conversion (F7 native ADSTART, bit 2 of CR)
    // ADC clock is PCLK2/4 = 108/4 = 27MHz.
    // At 480-cycle sample time + ~12 ADC clock cycles for conversion,
    // max conversion time ≈ 492 cycles / 27MHz ≈ 18.2µs.
    // 50000 NOP loops at 216MHz ≈ 231µs gives 12x margin.
    ADC_CR |= CR_ADSTART;
    __DSB();

    for (volatile uint32_t t = 0; t < 50000; t++) {
        uint32_t isr = ADC_ISR;
        if (isr & ISR_EOC) {
            uint32_t val = ADC_DR & 0xFFF;
            rtt_adc_last_raw = val;
            return val;
        }
        if (isr & ISR_OVR) {
            // Overrun: clear OVR flag and read DR, try again
            (void)ADC_DR;
            ADC_ISR = ISR_OVR;
            // Restart conversion
            ADC_CR |= CR_ADSTART;
            __DSB();
        }
        // Use NOP for short waits — conversion at 27MHz takes ~18µs
        // which is ~3000 NOP iterations at 216MHz
        __NOP();
    }

    // Timeout — ADC is stuck. Attempt recovery:
    // 1. Clear overrun if present
    if (ADC_ISR & ISR_OVR) {
        (void)ADC_DR;
        ADC_ISR = ISR_OVR;
    }
    // 2. Try stopping any ongoing conversion
    ADC_CR |= CR_ADSTP;
    __DSB();
    {
        volatile uint32_t stp_timeout = 1000;
        while ((ADC_CR & CR_ADSTART) && --stp_timeout) { __NOP(); }
    }

    rtt_adc_timeout_count++;
    (void)ADC_DR;
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
    if ((uint8_t)n < ARRAY_SIZE(_ch_map) && (_ch_map[n] == 10 || _ch_map[n] == 11)) {
        _sources[n].set_scale(2.0f);
    } else {
        _sources[n].set_scale(1.0f);
    }
    IGNORE_RETURN(_sources[n].set_pin(n));
    return &_sources[n];
}

float AnalogIn::board_voltage() { return _board_voltage; }
float AnalogIn::servorail_voltage() { return _servorail_voltage; }
uint16_t AnalogIn::power_status_flags() { return _power_flags; }

void AnalogIn::_timer_tick()
{
    if (!_initialized) return;

    for (uint8_t i = 0; i < ARRAY_SIZE(_ch_map); i++) {
        uint32_t raw = _adc_read(_ch_map[i]);
        if (i < RTT_ANALOG_MAX_CHANNELS) {
            _sources[i]._add_sample((float)raw);
        }
    }

    float vdd = _sources[VDD_5V_SENS_INDEX].read_latest() * (3.3f / 4096.0f) * 2.0f;
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
