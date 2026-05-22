/*
 * AP_HAL_RTT — DeviceBus: per-bus callback threads with heap-allocated stacks
 *
 * One thread per physical bus (SPI1, SPI2, SPI4, I2C…).  Each thread uses
 * a static rt_thread object with a heap-allocated stack (rt_malloc) to avoid
 * BSS footprint from static stack arrays.  Lazy allocation on first use.
 *
 * API aligned with AP_HAL_ChibiOS/Device.h: bouncebuffer support,
 * hal_device tracking, thread-context guard on adjust_timer().
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

/*
 * RTT bounce buffer for DMA-safe transfers.
 * Simple structure — always allocates via rt_malloc_align,
 * copies data in/out.  No DMA-safe-region checks needed
 * because RT-Thread uses unified memory on STM32F7/H7.
 */
struct rtt_bouncebuffer_t {
    uint8_t *dma_buf;
    uint8_t *orig_buf;      // original buffer pointer (for finish)
    uint32_t size;
    bool busy;
};

class DeviceBus
{
public:
    DeviceBus(uint8_t thread_priority);

    struct DeviceBus *next;          /* linked list of buses (ChibiOS compat) */
    Semaphore semaphore;

    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb, AP_HAL::Device *hal_device);
    bool adjust_timer(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec);

    static DeviceBus *get_bus(uint8_t bus_num, uint8_t thread_priority);

    /*
     * DMA-safe bounce buffer support (API compatible with ChibiOS DeviceBus).
     * On RTT these always allocate a 32-byte-aligned bounce buffer
     * since we have no mem_is_dma_safe() equivalent.
     */
    bool bouncebuffer_setup(const uint8_t *&buf_tx, uint16_t tx_len,
                            uint8_t *&buf_rx, uint16_t rx_len) WARN_IF_UNUSED;
    void bouncebuffer_finish(const uint8_t *buf_tx, uint8_t *buf_rx, uint16_t rx_len);

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
    AP_HAL::Device *_hal_device = nullptr;

    // bounce buffer state (lazily allocated on first use)
    rtt_bouncebuffer_t *_bounce_buffer_tx = nullptr;
    rtt_bouncebuffer_t *_bounce_buffer_rx = nullptr;

    static void _bus_thread_entry(void *arg);
    static DeviceBus *_buses[MAX_BUSES];

    // internal bounce buffer helpers
    static bool _bouncebuffer_ensure(rtt_bouncebuffer_t *&bb, uint32_t size);
};

} // namespace RTT
