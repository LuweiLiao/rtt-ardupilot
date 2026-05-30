/*
 * ArduPilot + RT-Thread HAL - I2CDeviceManager
 */

#pragma once

#include <AP_HAL/I2CDevice.h>
#include "HAL_RTT_Namespace.h"

namespace RTT
{

class I2CDeviceManager : public AP_HAL::I2CDeviceManager
{
public:
    AP_HAL::I2CDevice *get_device_ptr(uint8_t bus, uint8_t address,
                                     uint32_t bus_clock = 400000,
                                     bool use_smbus = false,
                                     uint32_t timeout_ms = 4) override;

    uint32_t get_bus_mask(void) const override;
    uint32_t get_bus_mask_external(void) const override;
    uint32_t get_bus_mask_internal(void) const override;
};

} // namespace RTT
