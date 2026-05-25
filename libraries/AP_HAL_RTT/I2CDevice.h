/*
 * AP_HAL_RTT — I2C device driver
 * Direct CMSIS register-level I2C transfers (no RT-Thread I2C framework).
 * Supports periodic callbacks via DeviceBus (same as SPI).
 */

#pragma once

#include <AP_HAL/I2CDevice.h>
#include "Semaphores.h"
#include "DeviceBus.h"
#include "HAL_RTT_Namespace.h"

namespace RTT
{

class I2CDevice : public AP_HAL::I2CDevice
{
public:
    I2CDevice(uint8_t bus, uint8_t address, uint32_t bus_clock,
              bool use_smbus, uint32_t timeout_ms);
    ~I2CDevice();

    bool set_speed(AP_HAL::Device::Speed speed) override;
    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;
    bool read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                uint32_t recv_len, uint8_t times) override;
    AP_HAL::Semaphore *get_semaphore() override;
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb) override;
    bool adjust_periodic_callback(
        AP_HAL::Device::PeriodicHandle h, uint32_t period_usec) override;
    void set_address(uint8_t address) override { _address = address; }
    void set_split_transfers(bool set) override { _split = set; }
    void set_retries(uint8_t retries) override { _retries = retries; }

    static void clear_all_buses(void);
    static void clear_bus(uint8_t busidx);

private:
    bool _do_transfer(const uint8_t *send, uint32_t send_len,
                     uint8_t *recv, uint32_t recv_len);

    uint8_t _address;
    uint8_t _busnum;
    uint8_t _retries = 2;
    uint32_t _bus_clock;
    uint32_t _timeout_ms;
    bool _split;
    DeviceBus *_bus_dev;
};

} // namespace RTT
