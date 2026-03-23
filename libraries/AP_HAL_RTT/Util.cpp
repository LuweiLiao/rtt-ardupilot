/*
 * AP_HAL_RTT — Util implementation
 * High-precision time via DWT CYCCNT (Cortex-M7).
 * System ID from STM32 UID registers.
 * available_memory from RT-Thread heap stats.
 */

#include "AP_HAL_RTT/Util.h"
#include <AP_Common/ExpandingString.h>
#include <rtthread.h>
#include <stdio.h>

using namespace RTT;

/* DWT cycle counter for high-precision microseconds */
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004)
#define SCB_DEMCR  (*(volatile uint32_t *)0xE000EDFC)

static bool _dwt_initialized = false;
static uint32_t _cpu_freq_mhz = 216;

static void _dwt_init(void)
{
    if (_dwt_initialized) return;
    SCB_DEMCR |= (1U << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1U;
    _dwt_initialized = true;

    extern uint32_t SystemCoreClock;
    _cpu_freq_mhz = SystemCoreClock / 1000000;
    if (_cpu_freq_mhz == 0) _cpu_freq_mhz = 216;
}

uint32_t Util::get_millis() const
{
    return (uint32_t)(rt_tick_get() * 1000U / RT_TICK_PER_SECOND);
}

uint64_t Util::get_micros64() const
{
    _dwt_init();
    uint64_t tick_us = (uint64_t)rt_tick_get() * 1000000ULL / RT_TICK_PER_SECOND;
    uint32_t cyc = DWT_CYCCNT;
    uint32_t sub_us = cyc / _cpu_freq_mhz;
    uint32_t tick_period_us = 1000000U / RT_TICK_PER_SECOND;
    return tick_us + (sub_us % tick_period_us);
}

uint32_t Util::available_memory(void)
{
    rt_size_t total = 0, used = 0, max_used = 0;
    rt_memory_info(&total, &used, &max_used);
    return (uint32_t)(total - used);
}

/* STM32 Unique ID at 0x1FF0F420 (STM32F7) */
#define STM32_UID_BASE 0x1FF0F420U

bool Util::get_system_id(char buf[50])
{
    const uint32_t *uid = (const uint32_t *)STM32_UID_BASE;
    snprintf(buf, 50, "%08lx%08lx%08lx",
             (unsigned long)uid[2], (unsigned long)uid[1], (unsigned long)uid[0]);
    return true;
}

bool Util::get_system_id_unformatted(uint8_t buf[], uint8_t &len)
{
    len = 12;
    const uint8_t *uid = (const uint8_t *)STM32_UID_BASE;
    for (uint8_t i = 0; i < 12; i++) {
        buf[i] = uid[i];
    }
    return true;
}

void Util::set_hw_rtc(uint64_t time_utc_usec)
{
    (void)time_utc_usec;
}

uint64_t Util::get_hw_rtc() const
{
    return get_micros64();
}

void Util::thread_info(ExpandingString& str)
{
    str.printf("ThreadsV1\n");
}
