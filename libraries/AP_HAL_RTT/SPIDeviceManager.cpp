/*
 * AP_HAL_RTT — SPI device manager
 * Board-independent: device table driven by HAL_SPI_DEVICE_LIST from hwdef.h.
 */

#include "SPIDeviceManager.h"
#include "SPIDevice.h"
#include <cstring>

namespace RTT
{

#ifdef HAL_SPI_DEVICE_LIST
static RTT_SPIDesc _device_table[] = { HAL_SPI_DEVICE_LIST };
#define _DEVICE_TABLE_COUNT ARRAY_SIZE(_device_table)
#else
static RTT_SPIDesc *_device_table = nullptr;
#define _DEVICE_TABLE_COUNT 0
#endif

AP_HAL::SPIDevice *SPIDeviceManager::get_device_ptr(const char *name)
{
    for (uint8_t i = 0; i < _DEVICE_TABLE_COUNT; i++) {
        if (strcmp(name, _device_table[i].name) == 0) {
            return NEW_NOTHROW SPIDevice(_device_table[i]);
        }
    }
    return nullptr;
}

uint8_t SPIDeviceManager::get_count()
{
    return _DEVICE_TABLE_COUNT;
}

const char *SPIDeviceManager::get_device_name(uint8_t idx)
{
    if (idx >= _DEVICE_TABLE_COUNT) return nullptr;
    return _device_table[idx].name;
}

} // namespace RTT
