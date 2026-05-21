/*
 * AP_HAL_RTT — WSPI (QuadSPI/OctoSPI) device driver
 * Register-level polling, aligned with ChibiOS WSPIDevice semantics.
 *
 * Supports:
 *   - STM32F7 QUADSPIv1 (CUAV V5)
 *   - STM32H7 OCTOSPI   (Pixhawk6C Mini) — TODO
 *
 * Device table driven by HAL_WSPI_DEVICE_LIST from hwdef.h (if generated)
 * or from manual board-level definitions.
 */

#pragma once

#include <AP_HAL/WSPIDevice.h>
#include "Semaphores.h"
#include "DeviceBus.h"
#include "HAL_RTT_Namespace.h"

#ifdef STM32F767xx
#include <stm32f7xx.h>
#elif defined(STM32H743xx)
#include <stm32h7xx.h>
#endif

namespace RTT
{

/* Device descriptor — matched from HAL_WSPI_DEVICE_LIST entries */
struct WSPIDesc {
    const char *name;          // logical device name
    uint8_t     bus;           // bus index (0-based)
    uint32_t    mode;          // CK_MODE bit (for DCR)
    uint32_t    speed;         // max clock speed in Hz
    uint8_t     size_pow2;     // flash size as power of 2 (for FSIZE)
    uint8_t     ncs_clk_delay; // CS hold delay in clock cycles (CSHT)
};

/* Per-bus state (one per physical QuadSPI/OctoSPI peripheral) */
class WSPIBus : public DeviceBus
{
public:
    WSPIBus(uint8_t _bus);
    uint8_t bus;

    // DCR value computed once from device_desc
    uint32_t dcr;
    // Has the peripheral been started (clock + CR.EN set)
    bool started;
    // Prescaler value (computed from speed)
    uint8_t prescaler;
};

class WSPIDevice : public AP_HAL::WSPIDevice
{
public:
    static WSPIDevice *from(AP_HAL::WSPIDevice *dev)
    {
        return static_cast<WSPIDevice*>(dev);
    }

    WSPIDevice(WSPIBus &_bus, WSPIDesc &_device_desc);

    bool set_speed(Speed speed) override { return true; }

    AP_HAL::Semaphore *get_semaphore() override
    {
        return &bus.semaphore;
    }

    PeriodicHandle register_periodic_callback(uint32_t period_usec, PeriodicCb cb) override
    {
        return nullptr;
    }
    bool adjust_periodic_callback(PeriodicHandle h, uint32_t period_usec) override
    {
        return false;
    }

    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;

    void set_cmd_header(const CommandHeader& cmd_hdr) override;

    bool is_busy() override;
    bool acquire_bus(bool acquire);

    // Memory-mapped / XIP mode
    bool enter_xip_mode(void **map_ptr) override;
    bool exit_xip_mode() override;

private:
    WSPIBus &bus;
    WSPIDesc &device_desc;
    AP_HAL::Device::CommandHeader mode;  // populated by set_cmd_header

    // STM32F7 QuadSPI register base
    QUADSPI_TypeDef *_qspi_regs();
};

class WSPIDeviceManager : public AP_HAL::WSPIDeviceManager
{
public:
    friend class WSPIDevice;

    static WSPIDeviceManager *from(AP_HAL::WSPIDeviceManager *wspi_mgr)
    {
        return static_cast<WSPIDeviceManager*>(wspi_mgr);
    }

    AP_HAL::OwnPtr<AP_HAL::WSPIDevice> get_device(const char *name) override;

    uint8_t get_count() const override;
    const char *get_device_name(uint8_t idx) const override;

private:
    static WSPIDesc device_table[];
    WSPIBus *buses;
};

} // namespace RTT
