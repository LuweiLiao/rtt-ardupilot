/*
 * AP_HAL_RTT — DeviceBus: per-bus callback threads with static allocation
 *
 * One thread per physical bus (SPI1, SPI2, SPI4, I2C…).  Each thread uses
 * a static stack buffer (rt_thread_init, no heap) to avoid heap exhaustion
 * from rt_thread_create.
 */

#pragma once

#include <stdint.h>
#include <AP_HAL/HAL.h>
#include <AP_HAL/Device.h>
#include "Semaphores.h"
#include "HAL_RTT_Namespace.h"
#include <rtthread.h>

namespace RTT
{

class DeviceBus
{
public:
    DeviceBus(uint8_t thread_priority);

    Semaphore semaphore;

    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb, AP_HAL::Device *hal_device);
    bool adjust_timer(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec);

    static DeviceBus *get_bus(uint8_t bus_num, uint8_t thread_priority);

    struct callback_info {
        AP_HAL::Device::PeriodicCb cb;
        uint32_t period_usec;
        uint64_t next_usec;
        callback_info *next;
    };

    static constexpr uint8_t MAX_BUSES = 8;

private:
    uint8_t _thread_priority;
    bool _thread_started = false;
    callback_info *_callbacks = nullptr;
    rt_thread_t _thread = nullptr;

    static void _bus_thread_entry(void *arg);
    static DeviceBus *_buses[MAX_BUSES];
};

} // namespace RTT
