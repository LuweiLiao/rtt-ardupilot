/*
 * LL common driver — SysTick via CMSIS + DWT microsecond timing.
 * Designed to replace drv_common.c's HAL_SYSTICK_Config path.
 */
#ifndef __DRV_COMMON_LL_H__
#define __DRV_COMMON_LL_H__

#include <stdint.h>

/* Initialize SysTick using CMSIS directly (no HAL_SYSTICK_Config) */
void systick_ll_init(uint32_t ticks_per_second);

/* DWT cycle counter initialization */
void dwt_ll_init(void);

/* Microsecond timestamp from DWT CYCCNT */
uint32_t dwt_ll_get_us(void);

/* Busy-wait microsecond delay using DWT (pre-scheduler, no OS) */
void dwt_ll_delay_us(uint32_t us);

#endif /* __DRV_COMMON_LL_H__ */
