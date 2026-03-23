/*
 * ArduPilot + RT-Thread HAL - DeviceBus periodic callback implementation
 */

#include "DeviceBus.h"
#include <rtthread.h>
#include <string.h>

namespace RTT
{

DeviceBus::DeviceBus(uint8_t thread_priority)
    : next(nullptr), _thread_priority(thread_priority)
{
}

struct periodic_cb_context {
    AP_HAL::Device::PeriodicCb cb;
    uint32_t period_usec;
    rt_thread_t thread;
};

static uint8_t _cb_thread_count = 0;

static void _periodic_thread_entry(void *arg)
{
    auto *ctx = (periodic_cb_context *)arg;
    uint32_t ms = ctx->period_usec / 1000;
    if (ms == 0) {
        ms = 1;
    }
    while (true) {
        ctx->cb();
        rt_thread_mdelay(ms);
    }
}

AP_HAL::Device::PeriodicHandle DeviceBus::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb, AP_HAL::Device *hal_device)
{
    (void)hal_device;

    auto *ctx = new periodic_cb_context{cb, period_usec, nullptr};
    if (ctx == nullptr) {
        return nullptr;
    }

    char name[RT_NAME_MAX];
    rt_snprintf(name, sizeof(name), "dcb%u", (unsigned)_cb_thread_count++);

    uint8_t prio = _thread_priority;
    if (prio == 0 || prio >= RT_THREAD_PRIORITY_MAX) {
        prio = RT_THREAD_PRIORITY_MAX / 3;
    }

    ctx->thread = rt_thread_create(name, _periodic_thread_entry,
                                   ctx, 2048, prio, 20);
    if (ctx->thread) {
        rt_thread_startup(ctx->thread);
        return (AP_HAL::Device::PeriodicHandle)ctx;
    }
    delete ctx;
    return nullptr;
}

bool DeviceBus::adjust_timer(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (h == nullptr) {
        return false;
    }
    auto *ctx = (periodic_cb_context *)h;
    ctx->period_usec = period_usec;
    return true;
}

} // namespace RTT
