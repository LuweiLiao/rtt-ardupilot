/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 * 2019-1-10      e31207077    add stm32f767-st-nucleo bsp
 */

#include "board.h"
#include <rtthread.h>

/*
 * RT-Thread's STM32 HAL_Drivers weak board init still calls the legacy CubeMX
 * hook.  Keep that ABI available while routing the real clock setup through
 * the ArduPilot RTT LL implementation.
 */
void SystemClock_Config(void)
{
    rtt_clock_init();
}
