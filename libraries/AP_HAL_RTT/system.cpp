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
