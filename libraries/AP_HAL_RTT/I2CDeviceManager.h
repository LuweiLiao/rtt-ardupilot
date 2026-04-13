/*
 * ArduPilot + RT-Thread HAL - I2CDeviceManager stub (Phase 0)
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

    // CUAV V5 I2C bus layout: only I2C3 (PH7/PH8) has physical pins.
    //   bus 0 = i2c3 (internal: IST8310 compass)
    // i2c1/i2c2/i2c4 pins not defined in hwdef.dat — no external compass support.
    uint32_t get_bus_mask(void) const override { return 0x01; }
    uint32_t get_bus_mask_external(void) const override { return 0x00; }
    uint32_t get_bus_mask_internal(void) const override { return 0x01; }
};

} // namespace RTT
