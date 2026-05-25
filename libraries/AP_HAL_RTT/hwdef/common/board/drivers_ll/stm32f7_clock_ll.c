/*
 * STM32F767 clock configuration using pure CMSIS register writes.
 *
 * Target: CUAV V5 — 8 MHz HSE crystal → PLL → 216 MHz SYSCLK
 *   HCLK  = 216 MHz (AHB  /1)
 *   PCLK1 =  54 MHz (APB1 /4)
 *   PCLK2 = 108 MHz (APB2 /2)
 *   USB48  =  48 MHz (PLL Q = PLL / (Q*2)) → PLL = (8/8)*432 = 432, Q=9 → 432/18 = 24MHz
 *
 * Fallback: HSE crystal → HSE bypass → HSI
 */

#include "board.h"

/* GDB-readable: 0=none, 1=HSE, 2=HSE_BYPASS, 3=HSI */
volatile uint8_t clock_source_used;

#define CLOCK_TIMEOUT          100000U

/* PLL dividers */
#define PLL_M                  8
#define PLL_N                  432
#define PLL_P_DIV              0                              /* 0x00000 → PLLP=/2 */
#define PLL_Q                  9

/*
 * PLLCFGR value with HSE (8 MHz → PLL = 8/8*432 = 432 MHz):
 *   PLLP = /2 → 432/2 = 216 MHz SYSCLK
 *   PLLQ = /9 → 432/9 = 48 MHz for 48M domain (USB, SDMMC)
 */
#define PLLCFGR_HSE  (RCC_PLLCFGR_PLLSRC_HSE                                              | \
                      (PLL_M << RCC_PLLCFGR_PLLM_Pos)                                      | \
                      (PLL_N << RCC_PLLCFGR_PLLN_Pos)                                      | \
                      PLL_P_DIV                                                             | \
                      (PLL_Q << RCC_PLLCFGR_PLLQ_Pos))

/*
 * PLLCFGR value with HSI (16 MHz → PLL = 16/8*432 = 864 MHz → overflow!
 * HSI fallback needs different M/N.  HSI=16MHz, M=16, N=432 → PLL=432MHz, /2=216MHz.
 */
#define PLL_M_HSI               16
#define PLLCFGR_HSI  (0 << RCC_PLLCFGR_PLLSRC_Pos                                         | \
                      (PLL_M_HSI << RCC_PLLCFGR_PLLM_Pos)                                   | \
                      (PLL_N << RCC_PLLCFGR_PLLN_Pos)                                       | \
                      PLL_P_DIV                                                             | \
                      (PLL_Q << RCC_PLLCFGR_PLLQ_Pos))

/* CFGR: HCLK=/1 (HPRE=0), APB1=/4 (PPRE1=DIV4), APB2=/2 (PPRE2=DIV2), SW=PLL */
#define CFGR_HCLK              RCC_CFGR_HPRE_DIV1
#define CFGR_PCLK1             RCC_CFGR_PPRE1_DIV4
#define CFGR_PCLK2             RCC_CFGR_PPRE2_DIV2
#define CFGR_SW_PLL            RCC_CFGR_SW_PLL

#define CFGR_VALUE             (CFGR_HCLK | CFGR_PCLK1 | CFGR_PCLK2 | CFGR_SW_PLL)

static int clock_try_hse_crystal(void)
{
    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = CLOCK_TIMEOUT;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
        if (--timeout == 0) {
            RCC->CR &= ~RCC_CR_HSEON;
            return 0;
        }
    }
    return 1;
}

static int clock_try_hse_bypass(void)
{
    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = CLOCK_TIMEOUT;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
        if (--timeout == 0) {
            RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_HSEBYP);
            return 0;
        }
    }
    return 1;
}

static void clock_pll_config_and_enable(uint32_t pllcfgr_val)
{
    RCC->PLLCFGR = pllcfgr_val;
    RCC->CR |= RCC_CR_PLLON;
    uint32_t timeout = CLOCK_TIMEOUT;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
        if (--timeout == 0) {
            Error_Handler();
        }
    }
}

void rtt_clock_init(void)
{
    static volatile uint8_t clock_configured = 0;
    if (clock_configured) {
        return;
    }
    clock_source_used = 0;

    /* Reset to HSI — bootloader may leave clocks in arbitrary state */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) { }
    RCC->CFGR = 0;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0) { }
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON | RCC_CR_CSSON);
    while (RCC->CR & RCC_CR_PLLRDY) { }
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CIR = 0;

    /* Flash latency: 5WS + PRFTEN + ARTEN for 216 MHz with OverDrive */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN | FLASH_ACR_ARTEN;
    (void)FLASH->ACR;  /* ensure write completes */

    /* Enable PWR clock */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;

    /* Voltage regulator Scale 1 (high performance) */
    PWR->CR1 |= PWR_CR1_VOS;
    /* Over-drive enable */
    PWR->CR1 |= PWR_CR1_ODEN;
    while (!(PWR->CSR1 & PWR_CSR1_ODRDY)) { }
    /* Over-drive switching */
    PWR->CR1 |= PWR_CR1_ODSWEN;
    while (!(PWR->CSR1 & PWR_CSR1_ODSWRDY)) { }

    /* Try HSE crystal (8 MHz on CUAV V5) */
    if (clock_try_hse_crystal()) {
        clock_pll_config_and_enable(PLLCFGR_HSE);
        clock_source_used = 1;
    }
    /* Try HSE bypass (external oscillator) */
    else if (clock_try_hse_bypass()) {
        clock_pll_config_and_enable(PLLCFGR_HSE);
        clock_source_used = 2;
    }
    /* Fallback: HSI (already running after reset) */
    else {
        clock_pll_config_and_enable(PLLCFGR_HSI);
        clock_source_used = 3;
    }

    /* Configure bus prescalers and switch system clock to PLL */
    RCC->CFGR = CFGR_VALUE;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }

    SystemCoreClock = 216000000U;

    /*
     * Peripheral clock sources (DCKCFGR2).
     *   CK48MSEL  = 0 → 48MHz domain from PLL Q
     *   SDMMC1SEL = 0 → SDMMC1 from 48MHz domain (PLL Q)
     *   USART3SEL = 0 → PCLK1 (54 MHz)
     *   UART7SEL  = 0 → PCLK1 (54 MHz)
     *
     * After a full RCC reset above, DCKCFGR2 is preserved (not reset by
     * the RCC->CFGR=0 sequence since it's a separate register).  Make sure
     * all relevant bits are zeroed explicitly.
     */
    RCC->DCKCFGR2 &= ~(RCC_DCKCFGR2_CK48MSEL     |
                        RCC_DCKCFGR2_SDMMC1SEL    |
                        RCC_DCKCFGR2_USART3SEL    |
                        RCC_DCKCFGR2_UART7SEL);

    clock_configured = 1;
}

/*
 * Enable all GPIO port clocks and common DMA clocks.
 * Called once early in board init, before any peripheral uses GPIO.
 */
void rtt_enable_peripheral_clocks(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN |
                    RCC_AHB1ENR_GPIODEN |
                    RCC_AHB1ENR_GPIOEEN |
                    RCC_AHB1ENR_GPIOFEN |
                    RCC_AHB1ENR_GPIOGEN |
                    RCC_AHB1ENR_GPIOHEN |
                    RCC_AHB1ENR_GPIOIEN |
                    RCC_AHB1ENR_DMA1EN  |
                    RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
}
