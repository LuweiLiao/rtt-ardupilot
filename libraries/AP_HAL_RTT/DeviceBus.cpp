/*
 * AP_HAL_RTT — DeviceBus: per-bus callback thread (ChibiOS-aligned)
 *
 * Single thread per physical bus iterates a linked list of callbacks,
 * dispatching each when micros64() >= next_usec.  Sleep is computed as
 * the time until the earliest pending callback, clamped to [100 µs, 50 ms].
 * This mirrors ChibiOS Device.cpp::bus_thread().
 */

#include "DeviceBus.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <string.h>

extern const AP_HAL::HAL &hal;

namespace RTT
{

DeviceBus *DeviceBus::_buses[MAX_BUSES] = {};

DeviceBus::DeviceBus(uint8_t thread_priority)
    : _thread_priority(thread_priority)
{
}

DeviceBus *DeviceBus::get_bus(uint8_t bus_num, uint8_t thread_priority)
{
    if (bus_num >= MAX_BUSES) {
        return nullptr;
    }
    if (_buses[bus_num] == nullptr) {
        _buses[bus_num] = new DeviceBus(thread_priority);
    }
    return _buses[bus_num];
}

void DeviceBus::_bus_thread_entry(void *arg)
{
    DeviceBus *binfo = (DeviceBus *)arg;

    while (true) {
        uint64_t now = AP_HAL::micros64();
        callback_info *callback;

        for (callback = binfo->_callbacks; callback; callback = callback->next) {
            if (now >= callback->next_usec) {
                while (now >= callback->next_usec) {
                    callback->next_usec += callback->period_usec;
                }
                binfo->semaphore.take_blocking();
                callback->cb();
                binfo->semaphore.give();
            }
        }

        uint64_t next_needed = 0;
        now = AP_HAL::micros64();

        for (callback = binfo->_callbacks; callback; callback = callback->next) {
            if (next_needed == 0 || callback->next_usec < next_needed) {
                next_needed = callback->next_usec;
                if (next_needed < now) {
                    next_needed = now;
                }
            }
        }

        uint32_t delay_us = 50000;
        if (next_needed >= now && next_needed - now < delay_us) {
            delay_us = next_needed - now;
        }
        if (delay_us < 100) {
            delay_us = 100;
        }

        rt_tick_t ticks = (rt_tick_t)((uint32_t)delay_us * RT_TICK_PER_SECOND / 1000000U);
        if (ticks == 0) {
            ticks = 1;
        }
        rt_thread_delay(ticks);
    }
}

AP_HAL::Device::PeriodicHandle DeviceBus::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb, AP_HAL::Device *hal_device)
{
    if (!_thread_started) {
        _thread_started = true;

        char name[RT_NAME_MAX];
        if (hal_device != nullptr) {
            switch (hal_device->bus_type()) {
            case AP_HAL::Device::BUS_TYPE_SPI:
                rt_snprintf(name, sizeof(name), "SPI%u", (unsigned)hal_device->bus_num());
                break;
            case AP_HAL::Device::BUS_TYPE_I2C:
                rt_snprintf(name, sizeof(name), "I2C%u", (unsigned)hal_device->bus_num());
                break;
            default:
                rt_snprintf(name, sizeof(name), "DEV%u", (unsigned)hal_device->bus_num());
                break;
            }
        } else {
            static uint8_t anon_cnt = 0;
            rt_snprintf(name, sizeof(name), "dcb%u", (unsigned)anon_cnt++);
        }

        uint8_t prio = _thread_priority;
        if (prio == 0 || prio >= RT_THREAD_PRIORITY_MAX) {
            /* Must be higher priority than boosted main (MAX/4=8) so that
             * bus callbacks preempt wait_for_sample() DWT loops.
             * Use MAX/6 ≈ 5 — below timer (MAX/8=4), above boosted main. */
            prio = RT_THREAD_PRIORITY_MAX / 6;
        }

        _thread = rt_thread_create(name, _bus_thread_entry,
                                   this, 8192, prio, 20);
        if (_thread == nullptr) {
            return nullptr;
        }
        rt_thread_startup(_thread);
    }

    auto *ci = new callback_info;
    if (ci == nullptr) {
        return nullptr;
    }
    ci->cb = cb;
    ci->period_usec = period_usec;
    ci->next_usec = AP_HAL::micros64() + period_usec;
    ci->next = _callbacks;
    _callbacks = ci;

    return (AP_HAL::Device::PeriodicHandle)ci;
}

bool DeviceBus::adjust_timer(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (h == nullptr) {
        return false;
    }
    auto *ci = (callback_info *)h;
    ci->period_usec = period_usec;
    ci->next_usec = AP_HAL::micros64() + period_usec;
    return true;
}

} // namespace RTT
