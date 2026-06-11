/*
 * AP_HAL_RTT — Util implementation
 * High-precision time via DWT CYCCNT (Cortex-M7).
 * System ID from STM32 UID registers.
 * available_memory from RT-Thread heap stats.
 * thread_info lists all RT-Thread threads with stack usage.
 *
 * Reference: AP_HAL_ChibiOS/Util.cpp — structure and API align with
 * the ChibiOS HAL implementation at every function boundary.
 * @SYS/dma.txt via Shared_DMA::dma_info() can be wired when drivers adopt shared_dma.
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

#define RTT_TIME_DBG_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
extern "C" {
volatile uint32_t rtt_dbg_timebase_rt_tick_hz RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_cpu_hz RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_units_per_sec RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_millis_source RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_dwt_enabled RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_micros_delta_max RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_micros_backwards RTT_TIME_DBG_BSS;
volatile uint32_t rtt_dbg_timebase_millis_backwards RTT_TIME_DBG_BSS;
}

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
    rtt_dbg_timebase_rt_tick_hz = RT_TICK_PER_SECOND;
    rtt_dbg_timebase_cpu_hz = SystemCoreClock;
    rtt_dbg_timebase_units_per_sec = 1000000U;
    rtt_dbg_timebase_dwt_enabled = (DWT_CTRL & 1U) ? 1U : 0U;
}

uint32_t Util::get_millis() const
{
    /*
     * ChibiOS derives millis() from the same high-resolution timer used by
     * micros64() (hrt_millis32()).  Keep RTT on one AP_HAL timebase too:
     * RT_TICK_PER_SECOND remains the RT-Thread scheduler tick, while ArduPilot
     * time is the DWT-backed 1 MHz hrt-equivalent.
     */
    const uint32_t now_ms = (uint32_t)(get_micros64() / 1000ULL);
    rtt_dbg_timebase_millis_source = 1U; /* 1=DWT hrt, not rt_tick_get */
    return now_ms;
}

uint64_t Util::get_micros64() const
{
    _dwt_init();
    const uint32_t tick_hz = RT_TICK_PER_SECOND ? RT_TICK_PER_SECOND : 1000U;
    const uint32_t cpu_hz = SystemCoreClock ? SystemCoreClock : 216000000U;

    rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t cyc = DWT_CYCCNT;

    /*
     * [Cybernetics Ch.4] Closed-loop: DWT_CYCCNT is free-running and is not
     * phase-locked to RT-Thread's tick.  Combining rt_tick_get() with
     * (CYCCNT % tick_period) can move time backwards near a tick boundary,
     * which corrupts AP_Scheduler and GCS/PARAM time-budget feedback.  Keep a
     * DWT delta accumulator instead; this mirrors ChibiOS' monotonic hrt model
     * and only uses rt_tick_get() to seed the boot-time epoch.
     */
    static bool dwt_time_valid;
    static uint32_t last_cyc;
    static uint64_t accumulated_us;
    static uint64_t fractional_cycles;
    static uint64_t last_returned_us;

    if (!dwt_time_valid) {
        last_cyc = cyc;
        accumulated_us = (uint64_t)rt_tick_get() * 1000000ULL / tick_hz;
        fractional_cycles = 0;
        last_returned_us = accumulated_us;
        dwt_time_valid = true;
        rt_hw_interrupt_enable(level);
        return accumulated_us;
    }

    const uint32_t delta_cycles = cyc - last_cyc;
    last_cyc = cyc;
    const uint64_t scaled = fractional_cycles + (uint64_t)delta_cycles * 1000000ULL;
    const uint32_t delta_us = (uint32_t)(scaled / cpu_hz);
    if (delta_us > rtt_dbg_timebase_micros_delta_max) {
        rtt_dbg_timebase_micros_delta_max = delta_us;
    }
    accumulated_us += delta_us;
    fractional_cycles = scaled % cpu_hz;
    const uint64_t now_us = accumulated_us;
    if (now_us < last_returned_us) {
        rtt_dbg_timebase_micros_backwards++;
        rtt_dbg_timebase_millis_backwards = rtt_dbg_timebase_micros_backwards;
    }
    last_returned_us = now_us;
    rt_hw_interrupt_enable(level);
    return now_us;
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
 *  already uses SRAM1_APP (HEAP_BEGIN set past linker _end in SRAM1).
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
 *  Watchdog reset reason — captured in HAL_RTT_Class::run() before
 *  RCC_CSR RMVF clear (ChibiOS board.c: stm32_watchdog_save_reason).
 * --------------------------------------------------------------- */
uint32_t rtt_boot_rcc_csr;

bool Util::was_watchdog_reset() const
{
    /* bit 29 = IWDGRSTF, bit 28 = WWDGRSTF */
    return (rtt_boot_rcc_csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0;
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
    struct thread_snapshot {
        char name[RT_NAME_MAX];
        uint32_t stack_size;
        uint32_t stack_used;
        uint8_t prio;
        uint8_t stat;
    };
    thread_snapshot snapshots[32];
    uint8_t snapshot_count = 0;

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

        if (snapshot_count >= ARRAY_SIZE(snapshots)) {
            break;
        }

        thread_snapshot &snapshot = snapshots[snapshot_count++];
        rt_strncpy(snapshot.name, thread->parent.name, sizeof(snapshot.name));
        snapshot.name[sizeof(snapshot.name) - 1] = '\0';
        snapshot.stack_size = stack_size;
        snapshot.stack_used = stack_used;
        snapshot.stat = RT_SCHED_CTX(thread).stat;
        snapshot.prio = RT_SCHED_PRIV(thread).current_priority;
    }
    rt_exit_critical();

    for (uint8_t i = 0; i < snapshot_count; i++) {
        const thread_snapshot &snapshot = snapshots[i];

        const char *stat_str;
        switch (snapshot.stat & RT_THREAD_STAT_MASK) {
        case RT_THREAD_READY:   stat_str = "RDY"; break;
        case RT_THREAD_SUSPEND: stat_str = "SUS"; break;
        case RT_THREAD_RUNNING: stat_str = "RUN"; break;
        case RT_THREAD_CLOSE:   stat_str = "CLS"; break;
        default:                stat_str = "???"; break;
        }

        str.printf("%-16s %4d %8lu %8lu %5s\n",
                   snapshot.name,
                   (int)snapshot.prio,
                   (unsigned long)snapshot.stack_size,
                   (unsigned long)snapshot.stack_used,
                   stat_str);
    }
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
