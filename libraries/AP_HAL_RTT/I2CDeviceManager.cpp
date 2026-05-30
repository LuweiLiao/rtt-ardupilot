/*
 * ArduPilot + RT-Thread HAL - I2CDeviceManager implementation
 */

#include "I2CDeviceManager.h"
#include "I2CDevice.h"
#include <AP_HAL/board/rtt.h>

namespace RTT
{

#ifndef RTT_I2C_BUS_COUNT
#define RTT_I2C_BUS_COUNT 4
#endif

AP_HAL::I2CDevice *I2CDeviceManager::get_device_ptr(uint8_t bus, uint8_t address,
                                                   uint32_t bus_clock,
                                                   bool use_smbus,
                                                   uint32_t timeout_ms)
{
    if (bus >= RTT_I2C_BUS_COUNT) {
        return nullptr;
    }
    return NEW_NOTHROW I2CDevice(bus, address, bus_clock, use_smbus, timeout_ms);
}

uint32_t I2CDeviceManager::get_bus_mask(void) const
{
    if (RTT_I2C_BUS_COUNT >= 32) {
        return 0xFFFFFFFFU;
    }
    return (1U << RTT_I2C_BUS_COUNT) - 1U;
}

uint32_t I2CDeviceManager::get_bus_mask_internal(void) const
{
    return get_bus_mask() & HAL_I2C_INTERNAL_MASK;
}

uint32_t I2CDeviceManager::get_bus_mask_external(void) const
{
    return get_bus_mask() & ~HAL_I2C_INTERNAL_MASK;
}

} // namespace RTT
