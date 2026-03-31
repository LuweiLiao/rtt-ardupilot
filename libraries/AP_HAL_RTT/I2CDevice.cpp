/*
 * AP_HAL_RTT — I2C device driver implementation
 * Uses rt_i2c_bus_device for I2C bus access.
 * Periodic callbacks delegated to DeviceBus (same pattern as SPI).
 */

#include "I2CDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#ifdef RT_USING_I2C
#include <drivers/dev_i2c.h>
#endif

using namespace RTT;

#ifndef HAL_RTT_I2C_BUS_NAMES
#define HAL_RTT_I2C_BUS_NAMES "i2c1", "i2c2", "i2c3", "i2c4"
#endif

static const char *const _i2c_bus_names[] = { HAL_RTT_I2C_BUS_NAMES };
#define HAL_RTT_I2C_BUS_COUNT (sizeof(_i2c_bus_names) / sizeof(_i2c_bus_names[0]))

I2CDevice::I2CDevice(uint8_t bus, uint8_t address, uint32_t bus_clock,
                   bool use_smbus, uint32_t timeout_ms)
    : AP_HAL::I2CDevice()
    , _bus(nullptr)
    , _address(address)
    , _bus_clock(bus_clock)
    , _timeout_ms(timeout_ms)
    , _split(false)
    , _bus_dev(DeviceBus::get_bus(bus, 0))
{
    (void)use_smbus;
    set_device_bus(bus);
    set_device_address(address);
#ifdef RT_USING_I2C
    if (bus < HAL_RTT_I2C_BUS_COUNT) {
        _bus = rt_i2c_bus_device_find(_i2c_bus_names[bus]);
    }
#endif
}

I2CDevice::~I2CDevice()
{
}

bool I2CDevice::set_speed(AP_HAL::Device::Speed)
{
    return _bus != nullptr;
}

bool I2CDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
    if (_bus == nullptr) return false;
    if (!_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;

#ifdef RT_USING_I2C
    bool ok = false;
    for (uint8_t attempt = 0; attempt <= _retries; attempt++) {
        rt_tick_t tick = rt_tick_from_millisecond(_timeout_ms > 0 ? _timeout_ms : 4);
        if (rt_i2c_bus_lock(_bus, tick) != RT_EOK) {
            continue;
        }

        bool xfer_ok = true;
        if (send_len > 0 && send != nullptr) {
            rt_ssize_t n = rt_i2c_master_send(_bus, _address, RT_I2C_WR, send, send_len);
            if (n != (rt_ssize_t)send_len) xfer_ok = false;
        }
        if (xfer_ok && recv_len > 0 && recv != nullptr) {
            rt_ssize_t n = rt_i2c_master_recv(_bus, _address, RT_I2C_RD, recv, recv_len);
            if (n != (rt_ssize_t)recv_len) xfer_ok = false;
        }
        rt_i2c_bus_unlock(_bus);

        if (xfer_ok) {
            ok = true;
            break;
        }
    }
#else
    bool ok = false;
#endif

    _sem.give();
    return ok;
}

bool I2CDevice::read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                        uint32_t recv_len, uint8_t times)
{
    for (uint8_t i = 0; i < times; i++) {
        if (!read_registers(first_reg, recv + i * recv_len, recv_len)) {
            return false;
        }
    }
    return true;
}

AP_HAL::Semaphore *I2CDevice::get_semaphore()
{
    return &_sem;
}

AP_HAL::Device::PeriodicHandle I2CDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    if (_bus_dev == nullptr) {
        return nullptr;
    }
    return _bus_dev->register_periodic_callback(period_usec, cb, this);
}

bool I2CDevice::adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (_bus_dev == nullptr) {
        return false;
    }
    return _bus_dev->adjust_timer(h, period_usec);
}

/*
 * clear_bus — toggle SCL to recover a stuck I2C bus.
 * ChibiOS reads SDA then clocks SCL up to 9 times.
 * For software I2C in RT-Thread, the bit-bang driver handles this
 * internally, so this is a no-op for now.
 */
void I2CDevice::clear_bus(uint8_t busidx)
{
    (void)busidx;
}

void I2CDevice::clear_all_buses(void)
{
    for (uint8_t i = 0; i < HAL_RTT_I2C_BUS_COUNT; i++) {
        clear_bus(i);
    }
}
