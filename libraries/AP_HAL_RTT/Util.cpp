/*
 * AP_HAL_RTT — Util implementation
 * High-precision time via DWT CYCCNT (Cortex-M7).
 * System ID from STM32 UID registers.
 * available_memory from RT-Thread heap stats.
 * thread_info lists all RT-Thread threads with stack usage.
 */

#include "AP_HAL_RTT/Util.h"
#include "RCOutput.h"
#include <AP_Common/ExpandingString.h>
#include <rtthread.h>
#include <stdio.h>
#include <stm32f7xx.h>

#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
extern AP_IOMCU iomcu;
#endif

extern RTT::RCOutput *rtt_rcout_instance;

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
    // Use 64-bit intermediate to avoid uint32_t overflow when
    // RT_TICK_PER_SECOND > 1000.  With 10kHz ticks the naive
    // (tick * 1000) overflows after ~429 seconds.
#if RT_TICK_PER_SECOND == 1000
    return (uint32_t)rt_tick_get();
#else
    const rt_tick_t tick = rt_tick_get();
    return (uint32_t)((uint64_t)tick * 1000ULL / RT_TICK_PER_SECOND);
#endif
}

uint64_t Util::get_micros64() const
{
    _dwt_init();

#if RT_TICK_PER_SECOND == 1000
    const uint32_t tick_period_us = 1000U;
#else
    const uint32_t tick_period_us = 1000000U / RT_TICK_PER_SECOND;
#endif

    rt_base_t level = rt_hw_interrupt_disable();
    rt_tick_t tick = rt_tick_get();
    uint32_t cyc = DWT_CYCCNT;
    rt_hw_interrupt_enable(level);

#if RT_TICK_PER_SECOND == 1000
    uint64_t tick_us = (uint64_t)tick * 1000ULL;
#else
    uint64_t tick_us = (uint64_t)tick * 1000000ULL / RT_TICK_PER_SECOND;
#endif
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
#ifdef CHIBIOS_SHORT_BOARD_NAME
    snprintf(buf, 50, "%s %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
             CHIBIOS_SHORT_BOARD_NAME,
#else
    snprintf(buf, 50, "CUAVv5-RTT %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
#endif
             (unsigned)((uid[2]>>24)&0xFF), (unsigned)((uid[2]>>16)&0xFF),
             (unsigned)((uid[2]>> 8)&0xFF), (unsigned)((uid[2]>> 0)&0xFF),
             (unsigned)((uid[1]>>24)&0xFF), (unsigned)((uid[1]>>16)&0xFF),
             (unsigned)((uid[1]>> 8)&0xFF), (unsigned)((uid[1]>> 0)&0xFF),
             (unsigned)((uid[0]>>24)&0xFF), (unsigned)((uid[0]>>16)&0xFF),
             (unsigned)((uid[0]>> 8)&0xFF), (unsigned)((uid[0]>> 0)&0xFF));
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

static uint64_t _utc_set_usec = 0;
static uint64_t _utc_at_set = 0;

void Util::set_hw_rtc(uint64_t time_utc_usec)
{
    _utc_set_usec = time_utc_usec;
    _utc_at_set = get_micros64();
}

uint64_t Util::get_hw_rtc() const
{
    if (_utc_set_usec == 0) {
        return 0;
    }
    return _utc_set_usec + (get_micros64() - _utc_at_set);
}

/* ---------------------------------------------------------------
 *  Memory allocation — DMA-safe vs. normal
 *  On STM32F7: DTCM is NOT DMA-accessible, so DMA_SAFE allocations
 *  must come from SRAM1/SRAM2. Standard malloc() from RT-Thread heap
 *  already uses SRAM1 (HEAP_BEGIN set past _ebss in SRAM1).
 * --------------------------------------------------------------- */
void *Util::malloc_type(size_t size, AP_HAL::Util::Memory_Type mem_type)
{
    (void)mem_type;
    /* RT-Thread heap is in SRAM1 (DMA-accessible), so all calloc() is DMA-safe.
       MEM_FAST could use DTCM (faster access) but we'd need a separate pool;
       for now everything goes to SRAM1. */
    void *p = calloc(1, size);
    return p;
}

void Util::free_type(void *ptr, size_t size, AP_HAL::Util::Memory_Type mem_type)
{
    (void)size;
    (void)mem_type;
    free(ptr);
}

/* ---------------------------------------------------------------
 *  Safety switch — CUAV V5 has no standalone safety switch on FMU.
 *  IOMCU handles it, but we don't have IOMCU support.
 *  Return SAFETY_ARMED so ArduPilot doesn't block outputs.
 * --------------------------------------------------------------- */
enum AP_HAL::Util::safety_state Util::safety_switch_state(void)
{
#if HAL_WITH_IO_MCU
    /* Read safety state from IOMCU (physical safety switch on CUAV V5) */
    return iomcu.get_safety_switch_state();
#else
    /* No IOMCU — read from RCOutput's local safety state */
    if (rtt_rcout_instance) {
        if (!was_watchdog_reset()) {
            persistent_data.safety_state = (AP_HAL::Util::safety_state)rtt_rcout_instance->safety_state;
        }
        return (AP_HAL::Util::safety_state)rtt_rcout_instance->safety_state;
    }
    return AP_HAL::Util::SAFETY_ARMED;
#endif
}

/* ---------------------------------------------------------------
 *  Tone Alarm — stub until PWM buzzer pin is configured
 * --------------------------------------------------------------- */
bool Util::toneAlarm_init(uint8_t types)
{
    (void)types;
    return true;
}

void Util::toneAlarm_set_buzzer_tone(float frequency, float volume, uint32_t duration_ms)
{
    (void)frequency;
    (void)volume;
    (void)duration_ms;
}

/* ---------------------------------------------------------------
 *  Watchdog
 * --------------------------------------------------------------- */
bool Util::was_watchdog_reset() const
{
    /* RCC_CSR flags: bit 29 = IWDGRSTF, bit 28 = WWDGRSTF */
    uint32_t csr = RCC->CSR;
    if (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) {
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------
 *  Random values
 *  STM32F767 has a true RNG at RCC->AHB2ENR bit 6.
 *  Falls back to pseudo-random if RNG not ready.
 * --------------------------------------------------------------- */
#define RCC_AHB2ENR (*(volatile uint32_t *)0x40023834)
#define RNG_CR      (*(volatile uint32_t *)0x50060800)
#define RNG_SR      (*(volatile uint32_t *)0x50060804)
#define RNG_DR      (*(volatile uint32_t *)0x50060808)

bool Util::get_random_vals(uint8_t* data, size_t size)
{
    RCC_AHB2ENR |= (1U << 6);
    RNG_CR |= (1U << 2);

    for (size_t i = 0; i < size; ) {
        uint32_t timeout = 10000;
        while (!(RNG_SR & 1U) && timeout > 0) {
            timeout--;
        }
        if (timeout == 0) {
            return false;
        }
        uint32_t val = RNG_DR;
        for (uint8_t j = 0; j < 4 && i < size; j++, i++) {
            data[i] = (uint8_t)(val >> (j * 8));
        }
    }
    return true;
}

/* ---------------------------------------------------------------
 *  Armed state
 * --------------------------------------------------------------- */
void Util::set_soft_armed(const bool b)
{
    AP_HAL::Util::set_soft_armed(b);
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

void Util::mem_info(ExpandingString& str)
{
    rt_size_t total = 0, used = 0, max_used = 0;
    rt_memory_info(&total, &used, &max_used);
    str.printf("MemInfoV1\n");
    str.printf("total: %lu\n", (unsigned long)total);
    str.printf("used:  %lu\n", (unsigned long)used);
    str.printf("max:   %lu\n", (unsigned long)max_used);
    str.printf("free:  %lu\n", (unsigned long)(total - used));
}
