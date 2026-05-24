/*
 * RTT adaptation of ChibiOS STM32 ADCv2 LLD.
 * ChibiOS reference: modules/ChibiOS/os/hal/ports/STM32/LLD/ADCv2/hal_adc_lld.c
 *                    modules/ChibiOS/os/hal/ports/STM32/LLD/ADCv2/hal_adc_lld.h
 *
 * Key differences from ChibiOS:
 *  - Direct ADC_TypeDef* parameter instead of ADCDriver struct
 *  - Polled single conversion (no DMA, no circular buffer)
 *  - No ChibiOS OSAL dependencies (osalDbgAssert, chSysLock removed)
 *  - No ADC IRQ handler (polled mode only)
 *  - Simplified: single-shot conversions only
 *
 * STM32F767 (CUAV V5) has standard ADCv2 peripheral:
 *   SR (0x00), CR1 (0x04), CR2 (0x08), SMPR1 (0x0C), SMPR2 (0x10),
 *   HTR (0x24), LTR (0x28), SQR1 (0x2C), SQR2 (0x30), SQR3 (0x34),
 *   DR (0x4C)
 *   Common: ADC_BASE = 0x40012300, CCR (+0x304), TSVREFE bit 23
 *
 * ADC clock: PCLK2=108MHz, ADCPRE=1 (PCLK2/4=27MHz) -- within 36MHz max
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ADC clock prescaler (CCR bits 16-17) ---- */
#define ADC_LLD_ADCPRE_DIV2     0
#define ADC_LLD_ADCPRE_DIV4     1
#define ADC_LLD_ADCPRE_DIV6     2
#define ADC_LLD_ADCPRE_DIV8     3

/* ---- Sampling time (SMPR field values) ---- */
#define ADC_LLD_SAMPLE_3        0   /* 3 cycles */
#define ADC_LLD_SAMPLE_15       1   /* 15 cycles */
#define ADC_LLD_SAMPLE_28       2   /* 28 cycles */
#define ADC_LLD_SAMPLE_56       3   /* 56 cycles */
#define ADC_LLD_SAMPLE_84       4   /* 84 cycles */
#define ADC_LLD_SAMPLE_112      5   /* 112 cycles */
#define ADC_LLD_SAMPLE_144      6   /* 144 cycles */
#define ADC_LLD_SAMPLE_480      7   /* 480 cycles */

/* ---- Channel number constants ---- */
#define ADC_LLD_CH_IN0          0
#define ADC_LLD_CH_IN1          1
#define ADC_LLD_CH_IN2          2
#define ADC_LLD_CH_IN3          3
#define ADC_LLD_CH_IN4          4
#define ADC_LLD_CH_IN5          5
#define ADC_LLD_CH_IN6          6
#define ADC_LLD_CH_IN7          7
#define ADC_LLD_CH_IN8          8
#define ADC_LLD_CH_IN9          9
#define ADC_LLD_CH_IN10         10
#define ADC_LLD_CH_IN11         11
#define ADC_LLD_CH_IN12         12
#define ADC_LLD_CH_IN13         13
#define ADC_LLD_CH_IN14         14
#define ADC_LLD_CH_IN15         15
#define ADC_LLD_CH_SENSOR       16  /* Internal temperature sensor (ADC1 only) */
#define ADC_LLD_CH_VREFINT      17  /* Internal reference voltage (ADC1 only) */
#define ADC_LLD_CH_VBAT         18  /* VBAT (ADC1 only) */

/*
 * adc_lld_init_rtt — Initialize and calibrate the ADC peripheral.
 *
 * Steps (matching ChibiOS ADCv2 hal_adc_lld.c: adc_lld_start):
 *   1. Enable ADC clock (RCC APB2ENR)
 *   2. Reset ADC peripheral (APB2RSTR toggle)
 *   3. Configure ADC common register (CCR): prescaler + TSVREFE
 *   4. Calibrate (ADCAL -> wait until clear)
 *   5. Enable ADC (ADON -> wait ADRDY)
 *
 * @param ADCx        ADC peripheral base (ADC1, ADC2, or ADC3)
 * @param adcpre      ADC prescaler value (ADC_LLD_ADCPRE_DIVx)
 * @param enable_tsvrefe  true to enable temperature sensor / VREFINT
 * @return            true on success, false on calibration timeout
 */
bool adc_lld_init_rtt(void *ADCx, uint32_t adcpre, bool enable_tsvrefe);

/*
 * adc_lld_convert_channel_rtt — Single polled conversion.
 *
 * Configures a one-channel conversion group, starts SW trigger,
 * waits for EOC, returns the 12-bit result.
 *
 * Steps (matching ChibiOS ADCv2 hal_adc_lld.c: adc_lld_start_conversion):
 *   1. Configure SQRx with single channel
 *   2. Configure SMPR for appropriate sampling time
 *   3. Set CR1 (SCAN + OVRIE)
 *   4. Start conversion via CR2 SWSTART
 *   5. Poll SR EOC
 *   6. Read DR
 *
 * @param ADCx        ADC peripheral base
 * @param channel     ADC channel number (0-18)
 * @param sample_time Sampling time (ADC_LLD_SAMPLE_x)
 * @return            16-bit ADC conversion result (12-bit, right-aligned)
 *                    Returns 0xFFFF on timeout/error.
 */
uint16_t adc_lld_convert_channel_rtt(void *ADCx, uint8_t channel,
                                     uint32_t sample_time);

/*
 * adc_lld_enable_tsvrefe_rtt — Enable TSVREFE bit in ADC common CCR.
 * Must be called to sample internal temperature sensor or VREFINT.
 * ChibiOS reference: hal_adc_lld.c:420-423 (adcSTM32EnableTSVREFE)
 */
void adc_lld_enable_tsvrefe_rtt(void);

/*
 * adc_lld_disable_tsvrefe_rtt — Disable TSVREFE bit.
 * ChibiOS reference: hal_adc_lld.c:431-434 (adcSTM32DisableTSVREFE)
 */
void adc_lld_disable_tsvrefe_rtt(void);

/*
 * adc_lld_enable_vbate_rtt — Enable VBATE bit in ADC common CCR.
 * Must be called to sample VBAT channel.
 * ChibiOS reference: hal_adc_lld.c:442-445 (adcSTM32EnableVBATE)
 */
void adc_lld_enable_vbate_rtt(void);

/*
 * adc_lld_disable_vbate_rtt — Disable VBATE bit.
 * ChibiOS reference: hal_adc_lld.c:453-456 (adcSTM32DisableVBATE)
 */
void adc_lld_disable_vbate_rtt(void);

#ifdef __cplusplus
}
#endif
