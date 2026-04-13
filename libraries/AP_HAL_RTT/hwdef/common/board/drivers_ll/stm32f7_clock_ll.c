/*
 * STM32F767 clock configuration using LL API (replaces HAL_RCC version).
 *
 * Target: CUAV V5 — 16 MHz HSE crystal → PLL → 216 MHz SYSCLK
 *   HCLK  = 216 MHz (AHB  /1)
 *   PCLK1 =  54 MHz (APB1 /4)
 *   PCLK2 = 108 MHz (APB2 /2)
 *   USB48  =  48 MHz (PLL Q=9 → 216/9*2 = 48 MHz via 48M domain)
 *
 * Fallback: HSE → HSE_BYPASS → HSI (same resilience as previous HAL version).
 */

#include "board.h"
#include "stm32f7xx_ll_rcc.h"
#include "stm32f7xx_ll_bus.h"
#include "stm32f7xx_ll_pwr.h"
#include "stm32f7xx_ll_cortex.h"
#include "stm32f7xx_ll_system.h"
#include "stm32f7xx_ll_utils.h"

/* GDB-readable: 0=none, 1=HSE, 2=HSE_BYPASS, 3=HSI */
volatile uint8_t clock_source_used;

#define CLOCK_LL_TIMEOUT  100000U

static int clock_try_hse_crystal(void)
{
    LL_RCC_HSE_Enable();
    uint32_t timeout = CLOCK_LL_TIMEOUT;
    while (!LL_RCC_HSE_IsReady()) {
        if (--timeout == 0) {
            LL_RCC_HSE_Disable();
            return 0;
        }
    }
    return 1;
}

static int clock_try_hse_bypass(void)
{
    LL_RCC_HSE_EnableBypass();
    LL_RCC_HSE_Enable();
    uint32_t timeout = CLOCK_LL_TIMEOUT;
    while (!LL_RCC_HSE_IsReady()) {
        if (--timeout == 0) {
            LL_RCC_HSE_Disable();
            LL_RCC_HSE_DisableBypass();
            return 0;
        }
    }
    return 1;
}

static void clock_pll_config_and_enable(uint32_t pll_source)
{
    LL_RCC_PLL_ConfigDomain_SYS(pll_source, LL_RCC_PLLM_DIV_8, 216, LL_RCC_PLLP_DIV_2);
    LL_RCC_PLL_ConfigDomain_48M(pll_source, LL_RCC_PLLM_DIV_8, 216, LL_RCC_PLLQ_DIV_9);
    LL_RCC_PLL_Enable();

    uint32_t timeout = CLOCK_LL_TIMEOUT;
    while (!LL_RCC_PLL_IsReady()) {
        if (--timeout == 0) {
            Error_Handler();
        }
    }
}

void SystemClock_Config(void)
{
    static volatile uint8_t clock_configured = 0;
    if (clock_configured) {
        return;
    }
    clock_source_used = 0;

    /* Bootloader may leave clocks in arbitrary state — reset to HSI.
     * Inline RCC reset avoids needing stm32f7xx_ll_rcc.c compiled. */
    SET_BIT(RCC->CR, RCC_CR_HSION);
    while (!READ_BIT(RCC->CR, RCC_CR_HSIRDY)) { }
    WRITE_REG(RCC->CFGR, 0U);
    while (READ_BIT(RCC->CFGR, RCC_CFGR_SWS) != 0U) { }
    CLEAR_BIT(RCC->CR, RCC_CR_PLLON | RCC_CR_HSEON | RCC_CR_CSSON);
    while (READ_BIT(RCC->CR, RCC_CR_PLLRDY)) { }
    CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
    WRITE_REG(RCC->CIR, 0U);

    /* Flash latency must be set BEFORE increasing clock speed */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_7);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_7) { }

    /* Enable PWR clock and set voltage scaling */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

    /* Enable Over-Drive for 216 MHz operation */
    LL_PWR_EnableOverDriveMode();
    while (!LL_PWR_IsActiveFlag_OD()) { }
    LL_PWR_EnableOverDriveSwitching();
    while (!LL_PWR_IsActiveFlag_ODSW()) { }

    /* Try HSE crystal (16 MHz on CUAV V5) */
    if (clock_try_hse_crystal()) {
        clock_pll_config_and_enable(LL_RCC_PLLSOURCE_HSE);
        clock_source_used = 1;
    }
    /* Try HSE bypass (external oscillator) */
    else if (clock_try_hse_bypass()) {
        clock_pll_config_and_enable(LL_RCC_PLLSOURCE_HSE);
        clock_source_used = 2;
    }
    /* Fallback: HSI (already running after reset) */
    else {
        clock_pll_config_and_enable(LL_RCC_PLLSOURCE_HSI);
        clock_source_used = 3;
    }

    /* Configure bus prescalers */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_4);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);

    /* Switch system clock to PLL */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) { }

    SystemCoreClock = 216000000U;

    /* Peripheral clock sources */
    LL_RCC_SetCK48MClockSource(LL_RCC_CK48M_CLKSOURCE_PLL);
    LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_PLL);
    LL_RCC_SetSDMMCClockSource(LL_RCC_SDMMC1_CLKSOURCE_PLL48CLK);
    LL_RCC_SetUSARTClockSource(LL_RCC_USART3_CLKSOURCE_PCLK1);
    LL_RCC_SetUARTClockSource(LL_RCC_UART7_CLKSOURCE_PCLK1);

    clock_configured = 1;
}
