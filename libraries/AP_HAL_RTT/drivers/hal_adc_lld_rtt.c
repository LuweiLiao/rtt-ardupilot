/*
 * RTT adaptation of ChibiOS STM32 ADCv2 LLD (hal_adc_lld_rtt.c).
 *
 * ChibiOS reference:
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/ADCv2/hal_adc_lld.c
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/ADCv2/hal_adc_lld.h
 *
 * Adaptations for RTT:
 *  - Direct ADC_TypeDef* instead of ADCDriver struct
 *  - Polled single conversion (no DMA, no ADCDriver state machine)
 *  - No ChibiOS OSAL / chSysLock / chSysUnlock
 *  - No DMA stream allocation (simplified: poll EOC directly)
 *  - No shared ADC interrupt handler
 *
 * STM32F767 uses standard ADCv2 peripheral.
 * Reference manual: RM0410 §19 (ADC)
 *
 * NOTE: This CMSIS package (stm32f7_cmsis_driver-latest) does not define
 * ADC_CR2_CAL (bit 31) or ADC_SR_ADRDY (bit 0), but the STM32F7 silicon
 * actually has these bits per RM0410.
 */

#include "hal_adc_lld_rtt.h"
#include <stm32f7xx.h>

/* ========================================================================== */
/* Local helpers                                                              */
/* ========================================================================== */

/* Convert generic pointer to ADC_TypeDef */
static inline ADC_TypeDef *_adc(void *p)
{
    return (ADC_TypeDef *)p;
}

/* ========================================================================== */
/* Exported functions                                                         */
/* ========================================================================== */

bool adc_lld_init_rtt(void *ADCx, uint32_t adcpre, bool enable_tsvrefe)
{
    ADC_TypeDef *adc = _adc(ADCx);

    /* ---- Step 1: Enable ADC clock ---- */
    /* ChibiOS reference: hal_adc_lld.c:259 (rccEnableADC1) */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    __DSB();

    /* ---- Step 2: Reset ADC peripheral ---- */
    /* Clean start -- clears any stale calibration or state */
    RCC->APB2RSTR |= RCC_APB2RSTR_ADCRST;
    __DSB();
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADCRST;
    __DSB();

    /* ---- Step 3: Configure ADC common register (CCR) ---- */
    /* ChibiOS reference: hal_adc_lld.c:295-296 */
    ADC123_COMMON->CCR = (ADC123_COMMON->CCR &
                           ~(ADC_CCR_ADCPRE_Msk | ADC_CCR_TSVREFE)) |
                          (adcpre << ADC_CCR_ADCPRE_Pos) |
                          (enable_tsvrefe ? ADC_CCR_TSVREFE : 0);
    __DSB();

    /* ---- Step 4: Initial register state ---- */
    /* ChibiOS reference: hal_adc_lld.c:300-302 */
    adc->CR1 = 0;
    adc->CR2 = 0;
    __DSB();

    /* ---- Step 5: Enable ADC ---- */
    /* ChibiOS reference: hal_adc_lld.c:302
     * ADON powers up the ADC.  After a stabilization time (~1 ADC clock)
     * the ADC is ready.  ChibiOS does not poll ADRDY -- it trusts the
     * hardware to stabilize before the first conversion.
     */
    adc->CR2 = ADC_CR2_ADON;
    (void)adc->CR2;
    __DSB();

    /* Small delay for stabilization (~3 ADC clock cycles at 27MHz = ~0.1us) */
    {
        volatile uint32_t _d = 100;
        while (_d--) { __NOP(); }
    }

    /* Clear any stale status flags */
    adc->SR = 0;

    /* Set 12-bit resolution (default) */
    adc->CR1 &= ~ADC_CR1_RES_Msk;

    return true;
}

uint16_t adc_lld_convert_channel_rtt(void *ADCx, uint8_t channel,
                                     uint32_t sample_time)
{
    ADC_TypeDef *adc = _adc(ADCx);
    uint32_t timeout;

    /* ---- Configure conversion sequence ---- */
    /* Single channel in SQ1: set SQR3.SQ1 = channel, SQR1.L[3:0] = 0 (1 channel) */
    /* ChibiOS reference: hal_adc_lld.c:375-377 */
    adc->SQR1 = 0;                                          /* L=0 means 1 conversion */
    adc->SQR3 = (uint32_t)channel << ADC_SQR3_SQ1_Pos;

    /* ---- Configure sampling time ---- */
    /* Channels 0-9 use SMPR2; channels 10-18 use SMPR1 */
    /* ChibiOS reference: hal_adc_lld.c:371-372 */
    if (channel <= 9) {
        uint32_t shift = channel * 3;  /* 3 bits per channel */
        adc->SMPR2 = (adc->SMPR2 & ~(0x7UL << shift)) |
                     (sample_time << shift);
    } else if (channel <= 18) {
        uint32_t idx = channel - 10;
        uint32_t shift = idx * 3;
        adc->SMPR1 = (adc->SMPR1 & ~(0x7UL << shift)) |
                     (sample_time << shift);
    }

    /* ---- Configure CR1 ---- */
    /* ChibiOS reference: hal_adc_lld.c:380 */
    /* Set SCAN (bit 8) and OVRIE (overflow interrupt, bit 26) */
    adc->CR1 = ADC_CR1_SCAN;
    /* Clear resolution bits (12-bit) */
    adc->CR1 &= ~ADC_CR1_RES_Msk;

    /* ---- Start conversion ---- */
    /* ChibiOS reference: hal_adc_lld.c:382-395 */
    /* CR2: ADON (must stay set), SWSTART = 0 first */
    /* In single conversion mode, CONT=0, SWSTART=0 initially,
     * then set SWSTART to trigger */
    adc->CR2 = (adc->CR2 & ~ADC_CR2_SWSTART) | ADC_CR2_ADON;
    __DSB();

    /* Now trigger the conversion */
    adc->CR2 |= ADC_CR2_SWSTART;
    (void)adc->CR2;
    __DSB();

    /* ---- Wait for EOC ---- */
    /* ChibiOS reference: polled analog read pattern */
    /* EOC sets when conversion completes */
    timeout = 100000U;
    while (!(adc->SR & ADC_SR_EOC)) {
        if (--timeout == 0) {
            /* Timeout - return error value */
            return 0xFFFF;
        }
        __NOP();
    }

    /* Read result */
    uint16_t result = (uint16_t)(adc->DR & 0xFFFF);

    /* Clear EOC (read DR clears EOC automatically on ADCv2) */
    /* Also clear STR if set */
    adc->SR = 0;

    return result;
}

void adc_lld_enable_tsvrefe_rtt(void)
{
    /* ChibiOS reference: hal_adc_lld.c:420-423 */
    ADC123_COMMON->CCR |= ADC_CCR_TSVREFE;
    (void)ADC123_COMMON->CCR;
}

void adc_lld_disable_tsvrefe_rtt(void)
{
    /* ChibiOS reference: hal_adc_lld.c:431-434 */
    ADC123_COMMON->CCR &= ~ADC_CCR_TSVREFE;
    (void)ADC123_COMMON->CCR;
}

void adc_lld_enable_vbate_rtt(void)
{
    /* ChibiOS reference: hal_adc_lld.c:442-445 */
    ADC123_COMMON->CCR |= ADC_CCR_VBATE;
    (void)ADC123_COMMON->CCR;
}

void adc_lld_disable_vbate_rtt(void)
{
    /* ChibiOS reference: hal_adc_lld.c:453-456 */
    ADC123_COMMON->CCR &= ~ADC_CCR_VBATE;
    (void)ADC123_COMMON->CCR;
}
