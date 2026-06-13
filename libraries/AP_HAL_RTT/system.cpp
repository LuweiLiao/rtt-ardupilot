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
#include "rtt_dbg_bkp.h"

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

    rtt_dbg_bkp_fault_save(RTT_DBG_BKP_FAULT_PANIC, 0U, 0U, 0U);
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

static void rtt_fault_handler_bkp(uint32_t fault_type)
{
    uint32_t lr;
    __asm volatile ("mov %0, lr" : "=r" (lr));
    const uint32_t use_psp = lr & 0x4U;
    uint32_t sp;
    if (use_psp) {
        __asm volatile ("mrs %0, psp" : "=r" (sp));
    } else {
        __asm volatile ("mrs %0, msp" : "=r" (sp));
    }
    /* basic exception frame: R0,R1,R2,R3,R12,LR,PC,xPSR */
    const uint32_t pc = *(volatile uint32_t *)(sp + 24U);
    const uint32_t stk_lr = *(volatile uint32_t *)(sp + 20U);
    const uint32_t xpsr = *(volatile uint32_t *)(sp + 28U);
    rtt_dbg_bkp_fault_save(fault_type, pc, stk_lr, xpsr);
}

__attribute__((weak)) void BusFault_Handler(void)
{
    rt_kprintf("\n*** BusFault ***\n");
    rtt_fault_handler_bkp(RTT_DBG_BKP_FAULT_BUS);
    __disable_irq();
    while (1) {}
}

__attribute__((weak)) void UsageFault_Handler(void)
{
    rt_kprintf("\n*** UsageFault ***\n");
    rtt_fault_handler_bkp(RTT_DBG_BKP_FAULT_USAGE);
    __disable_irq();
    while (1) {}
}

__attribute__((weak)) void MemManage_Handler(void)
{
    rt_kprintf("\n*** MemManage ***\n");
    rtt_fault_handler_bkp(RTT_DBG_BKP_FAULT_MEM);
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
 *  IWDG initialization — STM32F767 Independent Watchdog.
 *  CUAV V5 enables IWDG in hardware option-byte mode.  The watchdog starts
 *  from reset with PR=0/RLR=0xFFF (~512ms at 32kHz LSI) and PR/RLR writes do
 *  not take effect.  This function only refreshes the counter and ensures LSI
 *  is ready; periodic feeding is handled by SysTick and Scheduler paths.
 * ---------------------------------------------------------------- */
extern "C" void ap_rtt_iwdg_init(void)
{
#define IWDG_KR    (*(volatile uint32_t *)0x40003000)

    /* Feed immediately; the bootloader and early RT-Thread init have already
     * spent part of the fixed hardware-IWDG window. */
    IWDG_KR = 0xAAAA;  /* Feed to prevent imminent reset */

    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) {
        IWDG_KR = 0xAAAA;
    }

    IWDG_KR = 0xAAAA;
}
