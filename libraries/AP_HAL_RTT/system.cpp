/*
 * AP_HAL_RTT — system functions (aligned with ChibiOS)
 * ChibiOS reference: libraries/AP_HAL_ChibiOS/system.cpp
 *
 * Provides AP_HAL::init / panic / millis / micros64 / millis16 / micros16.
 * millis/micros64 delegate to Util which uses DWT CYCCNT for sub-tick precision.
 * millis64 derived from micros64/1000 to ensure 64-bit range (no wrap at 2^32).
 *
 * RT-Thread provides its own HardFault_Handler (via context_gcc.S), so we
 * only supply weak fallback handlers for BusFault/UsageFault/MemManage.
 * NMI_Handler comes from board CubeMX stm32f7xx_it.c.
 * __cxa_pure_virtual / __dso_handle come from RT-Thread cxx_crt/cxx_crt_init.c.
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/system.h>
#include <AP_InternalError/AP_InternalError.h>
#include "AP_HAL_RTT/Util.h"
#include <rtthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stm32f7xx.h>

extern const AP_HAL::HAL& hal;

namespace AP_HAL {

// ChibiOS ref: system.cpp:323-325
void init()
{
}

// ChibiOS ref: system.cpp:327-348 (with INTERNAL_ERROR + repeated-print loop)
// RTT simplification: prints once, records internal error, then halts.
// Cannot replicate the full chThdSleep-based print loop because RTT
// scheduler cannot yield in panic state without risk of re-entering panic.
void panic(const char *errormsg, ...)
{
    INTERNAL_ERROR(AP_InternalError::error_t::panic);
    char buf[128];
    va_list ap;
    va_start(ap, errormsg);
    rt_kprintf("AP_HAL::panic: ");
    (void)vsnprintf(buf, sizeof(buf), errormsg, ap);
    va_end(ap);
    rt_kprintf("%s\n", buf);

    __disable_irq();
    while (1) {
        /* spin with interrupts disabled — mirrors ChibiOS */
    }
}

// ChibiOS ref: system.cpp:375-378 (hrt_millis32)
// Delegates to Util::get_millis() which derives from rt_tick_get()
uint32_t millis()
{
    return ((const RTT::Util*)hal.util)->get_millis();
}

// ChibiOS ref: system.cpp:350-362 (hrt_micros32 / st_lld_get_counter)
// Derives from micros64() to share the DWT-based implementation.
uint32_t micros()
{
    return (uint32_t)(micros64() & 0xFFFFFFFFU);
}

// ChibiOS ref: system.cpp:390-393 (hrt_millis64)
// IMPORTANT: derived from micros64()/1000, NOT from 32-bit millis(),
// to ensure the 64-bit value does not wrap at 2^32 ms (~49.7 days).
uint64_t millis64()
{
    return micros64() / 1000;
}

// ChibiOS ref: system.cpp:385-388 (hrt_micros64)
uint64_t micros64()
{
    return ((const RTT::Util*)hal.util)->get_micros64();
}

// ChibiOS ref: system.cpp:380-383 (hrt_millis32 & 0xFFFF)
uint16_t millis16()
{
    return (uint16_t)(millis() & 0xFFFF);
}

// ChibiOS ref: system.cpp:364-373 (st_lld_get_counter & 0xFFFF)
uint16_t micros16()
{
    return (uint16_t)(micros() & 0xFFFF);
}

}  // namespace AP_HAL

/* ----------------------------------------------------------------
 *  Fault handlers — RT-Thread context_gcc.S owns HardFault_Handler
 *  (it saves context for rt_hw_hard_fault_exception).
 *
 *  We provide weak handlers for the remaining faults as fallback;
 *  CubeMX stm32f7xx_it.c provides the primary definitions.
 *  ChibiOS ref: system.cpp:88-250 (full HardFault/BusFault/UsageFault/MemManage)
 * ---------------------------------------------------------------- */
extern "C" {

__attribute__((weak)) void BusFault_Handler(void)
{
    rt_kprintf("\n*** BusFault ***\n");
    __disable_irq();
    while (1) {}
}

__attribute__((weak)) void UsageFault_Handler(void)
{
    rt_kprintf("\n*** UsageFault ***\n");
    __disable_irq();
    while (1) {}
}

__attribute__((weak)) void MemManage_Handler(void)
{
    rt_kprintf("\n*** MemManage ***\n");
    __disable_irq();
    while (1) {}
}

} // extern "C"

/* Lightweight IWDG reload — no LSI/PR/RLR reconfig. Safe to call from
 * rt_components_init before slow INIT_* handlers. */
extern "C" void ap_rtt_iwdg_kick(void)
{
    IWDG->KR = 0xAAAA;
}

/* ----------------------------------------------------------------
 *  IWDG initialization — STM32F767 Independent Watchdog
 *  Uses LSI (~32 kHz). Prescaler /256, reload 1250 → ~10s timeout.
 *  Once started, IWDG cannot be stopped until next reset.
 *  Called from Scheduler::init() after threads are created.
 * ---------------------------------------------------------------- */
extern "C" void ap_rtt_iwdg_init(void)
{
#define IWDG_KR    (*(volatile uint32_t *)0x40003000)
#define IWDG_PR    (*(volatile uint32_t *)0x40003004)
#define IWDG_RLR   (*(volatile uint32_t *)0x40003008)
#define IWDG_SR    (*(volatile uint32_t *)0x4000300C)

    /* 🚨 FEED IMMEDIATELY — hardware IWDG starts from reset with default
     * ~512ms timeout (PR=0=div4, RLR=4095).  The bootloader may take
     * 100-300ms to validate the app, so only ~200-400ms remain by the
     * time we reach here.  LSI enable can take up to ~100ms, and PR/RLR
     * sync adds more delay.  Feed first to extend the counter NOW, then
     * reconfigure to a longer timeout. */
    IWDG_KR = 0xAAAA;  /* Feed to prevent imminent reset */

    /* Enable LSI */
    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) {}

    /* Enable write access to IWDG_PR and IWDG_RLR */
    IWDG_KR = 0x5555;

    /* Prescaler: /256 (PR=6, bits 110) */
    IWDG_PR = 6;

    /* Reload value: 1250 → timeout = (256 * 1250) / 32000 ≈ 10s */
    IWDG_RLR = 1250;

    /* Wait for register update with timeout (~10ms at 32kHz LSI) */
    {
        volatile uint32_t iwdg_timeout = 1000000;
        while (IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {
            if (--iwdg_timeout == 0) {
                rt_kprintf("IWDG: SR sync timeout (SR=0x%08lx), continuing\n",
                           (unsigned long)IWDG_SR);
                break;
            }
            __NOP();
        }
    }

    /* Final feed with the new timeout configuration active */
    IWDG_KR = 0xAAAA;

    /* Start the watchdog (no-op if already running from hardware IWDG) */
    IWDG_KR = 0xCCCC;
}
