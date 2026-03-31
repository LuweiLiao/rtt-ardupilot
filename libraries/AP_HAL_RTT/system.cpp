/*
 * AP_HAL_RTT — system functions (aligned with ChibiOS)
 * Provides AP_HAL::millis / micros64 / panic / millis16 / micros16.
 * millis/micros64 delegate to Util which uses DWT CYCCNT for sub-tick precision.
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/system.h>
#include "AP_HAL_RTT/Util.h"
#include <rtthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stm32f7xx.h>

extern const AP_HAL::HAL& hal;

namespace AP_HAL {

void panic(const char *errormsg, ...)
{
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

uint32_t millis()
{
    return ((const RTT::Util*)hal.util)->get_millis();
}

uint32_t micros()
{
    return (uint32_t)(micros64() & 0xFFFFFFFFU);
}

uint64_t millis64()
{
    return (uint64_t)millis();
}

uint64_t micros64()
{
    return ((const RTT::Util*)hal.util)->get_micros64();
}

uint16_t millis16()
{
    return (uint16_t)(millis() & 0xFFFF);
}

uint16_t micros16()
{
    return (uint16_t)(micros() & 0xFFFF);
}

}  // namespace AP_HAL

/* ----------------------------------------------------------------
 *  Fault handlers — RT-Thread context_gcc.S owns HardFault_Handler
 *  (it saves context for rt_hw_hard_fault_exception).
 *  We provide weak handlers for the remaining faults.
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

    /* Enable LSI */
    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) {}

    /* Enable write access to IWDG_PR and IWDG_RLR */
    IWDG_KR = 0x5555;

    /* Prescaler: /256 (PR=6, bits 110) */
    IWDG_PR = 6;

    /* Reload value: 1250 → timeout = (256 * 1250) / 32000 ≈ 10s */
    IWDG_RLR = 1250;

    /* Wait for register update */
    while (IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {}

    /* Start the watchdog */
    IWDG_KR = 0xCCCC;
}
