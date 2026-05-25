/*
 * AP_HAL_RTT — Util implementation
 * High-precision time via DWT CYCCNT (Cortex-M7).
 * System ID from STM32 UID registers.
 * available_memory from RT-Thread heap stats.
 * thread_info lists all RT-Thread threads with stack usage.
 *
 * Reference: AP_HAL_ChibiOS/Util.cpp — structure and API align with
 * the ChibiOS HAL implementation at every function boundary.
 */

#include "AP_HAL_RTT/Util.h"
#include "RCOutput.h"
#include <AP_Common/ExpandingString.h>
#include <AP_Math/AP_Math.h>          /* for MIN(), get_random16() fallback */
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
    const rt_tick_t tick = rt_tick_get();
    return (uint32_t)((uint64_t)tick * 1000ULL / RT_TICK_PER_SECOND);
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
    snprintf(buf, 50, "CUAVv5-RTT %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
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
    /* Clamp to caller's buffer size (ChibiOS ref: line 356) */
    len = MIN(12, len);
    const uint8_t *uid = (const uint8_t *)STM32_UID_BASE;
    for (uint8_t i = 0; i < len; i++) {
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
 *  Tone Alarm — stub until PWM buzzer pin is configured.
 *  Returns false: no tone alarm hardware available (ChibiOS ref: line 128-134)
 * --------------------------------------------------------------- */
bool Util::toneAlarm_init(uint8_t types)
{
    (void)types;
    return false;
}

void Util::toneAlarm_set_buzzer_tone(float frequency, float volume, uint32_t duration_ms)
{
    (void)frequency;
    (void)volume;
    (void)duration_ms;
}

/* ---------------------------------------------------------------
 *  Watchdog
 *  Cache reset reason at first call (ChibiOS ref: watchdog.c:104-117)
 * --------------------------------------------------------------- */
static uint32_t _watchdog_reset_reason;

static void _watchdog_save_reason(void)
{
    if (_watchdog_reset_reason == 0) {
        _watchdog_reset_reason = RCC->CSR;
    }
}

bool Util::was_watchdog_reset() const
{
    _watchdog_save_reason();
    /* bit 29 = IWDGRSTF, bit 28 = WWDGRSTF */
    return (_watchdog_reset_reason & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0;
}

/* ---------------------------------------------------------------
 *  Random values
 *  STM32F767 has a true RNG at RCC->AHB2ENR bit 6.
 *  Falls back to pseudo-random if RNG not ready.
 *  Reference: ChibiOS stm32_util.c:498-564, Util.cpp:681-708
 * --------------------------------------------------------------- */
#ifndef RNG
#error "get_random_vals requires RNG peripheral (STM32F7)"
#endif

bool Util::get_random_vals(uint8_t* data, size_t size)
{
    /* Enable RNG clock */
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
    __DSB();  /* ensure clock is stable before accessing RNG registers */

    RNG->CR |= RNG_CR_RNGEN;
    __DSB();

    size_t filled = 0;

    while (filled < size) {
        uint32_t timeout = 10000;
        while ((!(RNG->SR & RNG_SR_DRDY)) && timeout > 0) {
            timeout--;
        }
        if (timeout == 0) {
            /* HW RNG failed — fill remainder with software PRNG
               (ChibiOS ref: Util.cpp:685-703) */
            while (filled < size) {
                uint16_t val = get_random16();
                for (uint8_t j = 0; j < 2 && filled < size; j++, filled++) {
                    data[filled] = (uint8_t)(val >> (j * 8));
                }
            }
            return true;
        }
        uint32_t val = RNG->DR;
        for (uint8_t j = 0; j < 4 && filled < size; j++, filled++) {
            data[filled] = (uint8_t)(val >> (j * 8));
        }
    }
    return true;
}

/* ---------------------------------------------------------------
 *  Armed state
 *  Reference: ChibiOS Util.cpp:811-817
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
