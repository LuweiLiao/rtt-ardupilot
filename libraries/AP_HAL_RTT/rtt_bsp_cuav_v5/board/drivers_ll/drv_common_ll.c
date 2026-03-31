/*
 * LL common driver — SysTick via CMSIS + DWT timing.
 * Provides the same timing infrastructure as drv_common.c but without
 * HAL_SYSTICK_Config / HAL_IncTick dependency in the hot path.
 */

#include "drv_common_ll.h"
#include "board.h"
#include <rtthread.h>

#define DWT_CTRL_REG    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT_REG  (*(volatile uint32_t *)0xE0001004)
#define SCB_DEMCR_REG   (*(volatile uint32_t *)0xE000EDFC)

extern uint32_t SystemCoreClock;

void systick_ll_init(uint32_t ticks_per_second)
{
    /* CMSIS SysTick_Config: sets LOAD, clears VAL, enables SysTick with interrupt */
    uint32_t reload = (SystemCoreClock / ticks_per_second) - 1U;
    SysTick->LOAD = reload;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
    NVIC_SetPriority(SysTick_IRQn, 0xFF);
}

void dwt_ll_init(void)
{
    SCB_DEMCR_REG |= (1U << 24);                            /* Enable DWT */
    *(volatile uint32_t *)0xE0001FB0 = 0xC5ACCE55;          /* DWT_LAR unlock (Cortex-M7) */
    DWT_CYCCNT_REG = 0U;
    DWT_CTRL_REG  |= 1U;                                    /* Enable CYCCNT */
}

uint32_t dwt_ll_get_us(void)
{
    return DWT_CYCCNT_REG / (SystemCoreClock / 1000000U);
}

void dwt_ll_delay_us(uint32_t us)
{
    uint32_t cycles = us * (SystemCoreClock / 1000000U);
    uint32_t start = DWT_CYCCNT_REG;
    while ((DWT_CYCCNT_REG - start) < cycles) { }
}
