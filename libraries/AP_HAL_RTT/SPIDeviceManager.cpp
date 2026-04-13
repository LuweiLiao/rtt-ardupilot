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
    // rt_kprintf("[SPI-MGR] Looking for device '%s' in table of %u devices\n", name, _DEVICE_TABLE_COUNT);
    for (uint8_t i = 0; i < _DEVICE_TABLE_COUNT; i++) {
        // rt_kprintf("[SPI-MGR]   [%u] '%s' (rtt='%s')\n", i, _device_table[i].name, _device_table[i].rtt_devname);
        if (strcmp(name, _device_table[i].name) == 0) {
            // rt_kprintf("[SPI-MGR] Found '%s', creating SPIDevice\n", name);
            return NEW_NOTHROW SPIDevice(_device_table[i]);
        }
    }
    // rt_kprintf("[SPI-MGR] Device '%s' NOT FOUND in table\n", name);
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
