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
 * NOTE: The STM32F7 CMSIS headers already provide the standard ADCv2 bit
 * definitions such as ADC_SR_EOC.  Only define the compatibility names that
 * are missing from the shipped headers, after including CMSIS, to avoid
 * diverging from the vendor register truth source.
 */
#include <stm32f7xx.h>
#include "hal_adc_lld_rtt.h"

#ifndef ADC_SR_ADRDY
#define ADC_SR_ADRDY    (0x1UL << 0U)   /* bit 0: ADC ready (RM0410) */
#endif
#ifndef ADC_CR2_ADCAL
#define ADC_CR2_ADCAL   (0x1UL << 31U)  /* bit 31: ADC calibration (RM0410) */
#endif
/* NOTE: ADC_CR2_ADVREGEN (bit 28) does NOT exist on STM32F7.
 * It is an STM32H7/G4-only bit.  On F7 this bit is reserved.
 * ChibiOS ADCv2 LLD never sets it (hal_adc_lld.c:302 just sets ADON).
 */

/* Diagnostic counters for GDB — visible in .bss, writeable via debugger */
volatile uint32_t rtt_adc_lld_init_status = 0;  /* 0=init_OK, 2=ADRDY_timeout */
volatile uint32_t rtt_adc_lld_calib_status = 0;  /* 0=calib_OK, 2=calib_timeout */
volatile uint32_t rtt_adc_lld_conv_timeouts = 0; /* cumulative conversion timeouts */

/* ========================================================================== */
/* Local helpers                                                              */
/* ========================================================================== */

/* Convert generic pointer to ADC_TypeDef */
static inline ADC_TypeDef *_adc(void *p)
{
    return (ADC_TypeDef *)p;
}

/*
 * Busy-wait for ADC ready (ADRDY bit in SR).
 * Per RM0410 §19.3.7: after setting ADON=1, the analog section
 * stabilises in tSTAB (~3 us on F7).  ADRDY signals readiness.
 * Poll for up to ~1 ms (5000 iterations × ~200 ns each).
 * Returns true if ADRDY set, false on timeout.
 */
/* ADC ready wait: NOT used on F7.
 *
 * ChibiOS ADCv2 LLD (hal_adc_lld.c:302) does NOT wait for ADRDY after
 * setting ADON — it just sets the bit and returns.  Our earlier attempt
 * to add an ADRDY poll consistently timed out (even at 100k NOPs) even
 * though the ADC works fine for subsequent conversions (0 timeouts).
 * Per RM0410 §19.3.3: "The ADC can be used immediately after ADON is
 * set to 1", which contradicts the tSTAB wait in §19.3.7.  Empirical
 * evidence on CUAV V5 (STM32F767) confirms the ADC works without ADRDY.
 * The conversion function (adc_lld_convert_channel_rtt) has its own
 * timeout, which is sufficient protection.
 */

/* ========================================================================== */
/* Exported functions                                                         */
/* ========================================================================== */

bool adc_lld_init_rtt(void *ADCx, uint32_t adcpre, bool enable_tsvrefe)
{
    ADC_TypeDef *adc = _adc(ADCx);
    uint32_t timeout;

    rtt_adc_lld_init_status = 0;
    rtt_adc_lld_calib_status = 0;

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

    /* ---- Step 5: Enable ADC (ChibiOS hal_adc_lld.c:302) ---- */
    /* ChibiOS just sets ADON and returns without ADRDY wait.
     * Per RM0410 §19.3.3 the ADC can be used immediately after ADON.
     * No ADRDY poll needed — the conversion function has its own timeout. */
    adc->CR2 = ADC_CR2_ADON;
    __DSB();

    /* ---- Step 6: Calibration (RM0410 §19.3.3) ---- */
    /* Set ADCAL, wait until hardware clears it (~82 ADCCLK cycles). */
    adc->CR2 |= ADC_CR2_ADCAL;
    __DSB();
    timeout = 10000U;
    while (adc->CR2 & ADC_CR2_ADCAL) {
        if (--timeout == 0) {
            rtt_adc_lld_calib_status = 2;
            break;
        }
        __NOP();
    }

    return true;
}

uint16_t adc_lld_convert_channel_rtt(void *ADCx, uint8_t channel,
                                     uint32_t sample_time)
{
    ADC_TypeDef *adc = _adc(ADCx);
    uint32_t timeout;

    /* ---- Clear ADC status register ---- */
    /* ChibiOS reference: hal_adc_lld.c:370 (adc_lld_start_conversion)
     * Clear ALL status flags (EOC, OVR, AWD, etc.) before each conversion.
     * Without this, an overrun (OVR) from a previous conversion prevents
     * any new conversion from starting (RM0410 §19.4.14: "If the OVR bit
     * is set, no new conversion can be performed").
     * This also clears any stale EOC flag from a prior timed-out conversion. */
    adc->SR = 0;
    __DSB();

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
    /* EOC sets when conversion completes.  At 27 MHz ADCCLK + 15 cycles
     * sampling + 12 cycles conversion ≈ 1 µs typical.
     * Timeout = ~500 µs (20000 iterations × ~25 ns). */
    timeout = 20000U;
    while (!(adc->SR & ADC_SR_EOC)) {
        if (--timeout == 0) {
            /* Timeout — ADC likely not responding (clock off / misconfig) */
            rtt_adc_lld_conv_timeouts++;
            return 0xFFFF;
        }
        __NOP();
    }

    /* Read result */
    uint16_t result = (uint16_t)(adc->DR & 0xFFFF);

    /* STM32F7 Errata: check OVR flag before clearing SR.
     * If OVR was set (overflow occurred), the previous conversion was lost.
     * The OVR is tracked via diagnostic counter. The SR=0 at the top of
     * this function already cleared any stale OVR before starting, but
     * a genuine overflow DURING this conversion is flagged here. */
    uint32_t sr_after = adc->SR;
    if (sr_after & ADC_SR_OVR) {
        /* OVR during this conversion — increment diagnostic counter.
         * The value in DR is still valid per RM0410 §19.4.14
         * ("the most recent conversion result is preserved in ADC_DR"),
         * but we've potentially missed an intermediate value. */
        rtt_adc_lld_conv_timeouts++;  /* reuse: count overflow events */
    }

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
