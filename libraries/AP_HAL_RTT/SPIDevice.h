/*
 * AP_HAL_RTT — SPI device driver
 * Board-independent: device table driven by HAL_SPI_DEVICE_LIST from hwdef.h.
 */

#pragma once

#include <AP_HAL/SPIDevice.h>
#include "Semaphores.h"
#include "DeviceBus.h"
#include "HAL_RTT_Namespace.h"

struct rt_spi_device;

namespace RTT
{

struct RTT_SPIDesc {
    const char *name;
    const char *rtt_devname;
    uint8_t bus;
    uint8_t devid;
    uint8_t mode;
    uint32_t lowspeed;
    uint32_t highspeed;
};

class SPIDevice : public AP_HAL::SPIDevice
{
public:
    SPIDevice(RTT_SPIDesc &desc);
    ~SPIDevice();

    bool set_speed(AP_HAL::Device::Speed speed) override;
    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;
    bool transfer_fullduplex(const uint8_t *send, uint8_t *recv, uint32_t len) override;
    AP_HAL::Semaphore *get_semaphore() override;
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb) override;
    bool adjust_periodic_callback(
        AP_HAL::Device::PeriodicHandle h, uint32_t period_usec) override;
    bool set_chip_select(bool set) override;

private:
    bool _lock_bus();
    void _unlock_bus();

    RTT_SPIDesc &_desc;
    struct rt_spi_device *_dev;
    DeviceBus *_bus;
    bool _config_dirty = true;
    bool _bus_locked = false;
    bool _cs_held = false;
    rt_base_t _cs_pin;
    uint32_t _br;           /* SPI baud rate divider (BR field in CR1) */
};

} // namespace RTT
