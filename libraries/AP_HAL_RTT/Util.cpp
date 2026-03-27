/*
 * AP_HAL_RTT — Util implementation
 * High-precision time via DWT CYCCNT (Cortex-M7).
 * System ID from STM32 UID registers.
 * available_memory from RT-Thread heap stats.
 * thread_info lists all RT-Thread threads with stack usage.
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
    const uint32_t tick_period_us = 1000000U / RT_TICK_PER_SECOND;

    rt_base_t level = rt_hw_interrupt_disable();
    rt_tick_t tick = rt_tick_get();
    uint32_t cyc = DWT_CYCCNT;
    rt_hw_interrupt_enable(level);

    uint64_t tick_us = (uint64_t)tick * 1000000ULL / RT_TICK_PER_SECOND;
    uint32_t sub_us = (cyc / _cpu_freq_mhz) % tick_period_us;
    return tick_us + sub_us;
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
    str.printf("ThreadsV2\n");
    str.printf("%-16s %4s %8s %8s %5s\n", "Name", "Prio", "StackSz", "StackUse", "Stat");

    rt_thread_t thread;
    struct rt_object_information *info;
    struct rt_list_node *node;

    info = rt_object_get_information(RT_Object_Class_Thread);
    if (info == RT_NULL) return;

    rt_enter_critical();
    for (node = info->object_list.next; node != &(info->object_list); node = node->next) {
        /* rt_list_entry does pointer arithmetic that may trigger cast-align warning */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
        thread = rt_list_entry(node, struct rt_thread, parent.list);
#pragma GCC diagnostic pop

        uint32_t stack_size = thread->stack_size;
        uint32_t stack_used = 0;

#ifdef RT_USING_OVERFLOW_CHECK
        uint8_t *sp = (uint8_t *)thread->stack_addr;
        while (sp < (uint8_t *)thread->stack_addr + stack_size) {
            if (*sp != '#') break;
            sp++;
        }
        stack_used = stack_size - ((rt_ubase_t)sp - (rt_ubase_t)thread->stack_addr);
#endif

        uint8_t stat = RT_SCHED_CTX(thread).stat;
        uint8_t prio = RT_SCHED_PRIV(thread).current_priority;

        const char *stat_str;
        switch (stat & RT_THREAD_STAT_MASK) {
        case RT_THREAD_READY:   stat_str = "RDY"; break;
        case RT_THREAD_SUSPEND: stat_str = "SUS"; break;
        case RT_THREAD_RUNNING: stat_str = "RUN"; break;
        case RT_THREAD_CLOSE:   stat_str = "CLS"; break;
        default:                stat_str = "???"; break;
        }

        str.printf("%-16s %4d %8lu %8lu %5s\n",
                   thread->parent.name,
                   (int)prio,
                   (unsigned long)stack_size,
                   (unsigned long)stack_used,
                   stat_str);
    }
    rt_exit_critical();
}
