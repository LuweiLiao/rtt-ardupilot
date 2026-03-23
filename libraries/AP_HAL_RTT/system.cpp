/*
 * AP_HAL_RTT — system functions
 * Provides AP_HAL::millis / micros64 / panic.
 * millis/micros64 delegate to Util which uses DWT CYCCNT for sub-tick precision.
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/system.h>
#include "AP_HAL_RTT/Util.h"
#include <rtthread.h>
#include <stdarg.h>
#include <stdio.h>

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
    while (1) {
        rt_thread_mdelay(1000);
    }
}

uint32_t millis()
{
    return ((const RTT::Util*)hal.util)->get_millis();
}

uint64_t micros64()
{
    return ((const RTT::Util*)hal.util)->get_micros64();
}

uint64_t millis64()
{
    return (uint64_t)millis();
}

uint32_t micros()
{
    return (uint32_t)(micros64() & 0xFFFFFFFFU);
}

}  // namespace AP_HAL
