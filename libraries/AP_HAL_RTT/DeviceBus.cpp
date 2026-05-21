/*
 * AP_HAL_RTT — DeviceBus: per-bus callback threads with heap-allocated stacks
 *
 * Per-bus threads (one per physical SPI/I2C bus) dispatch callbacks by
 * micros64() timestamps.  Each thread stack is lazily heap-allocated via
 * rt_malloc on first use, reducing BSS footprint vs. static arrays.
 *
 * Bus-level exclusive access: each DeviceBus has its own Semaphore,
 * taken before each callback dispatch.
 *
 * API aligned with AP_HAL_ChibiOS/Device.cpp:
 *  - bouncebuffer_setup / bouncebuffer_finish (DMA-safe buffer abstraction)
 *  - hal_device tracking on the bus
 *  - thread-context guard on adjust_timer()
 *
 * Max 8 buses supported (MAX_BUSES).  Stacks allocated on demand.
 */

#include "DeviceBus.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <string.h>

extern const AP_HAL::HAL &hal;

namespace RTT
{

DeviceBus *DeviceBus::_buses[MAX_BUSES] = {};

/* ------------------------------------------------------------------
 *  Per-bus thread: static thread objects, heap-allocated stacks
 *  Threads are started lazily on first register_periodic_callback.
 *  Each stack is rt_malloc'd on demand (1 KB, matching ChibiOS
 *  HAL_DEVICE_THREAD_STACK) and never freed.
 * ------------------------------------------------------------------ */
static struct rt_thread _bus_thread_objs[DeviceBus::MAX_BUSES];
static char *_bus_thread_stacks[DeviceBus::MAX_BUSES] = {nullptr};
static const unsigned BUS_STACK_SIZE = 8192;
static bool _bus_thread_inited[DeviceBus::MAX_BUSES] = {false};

void DeviceBus::_bus_thread_entry(void *arg)
{
    DeviceBus *binfo = (DeviceBus *)arg;

    while (true) {
        uint64_t now = AP_HAL::micros64();
        callback_info *callback;

        for (callback = binfo->_callbacks; callback; callback = callback->next) {
            if (now >= callback->next_usec) {
                while (now >= callback->next_usec) {
                    callback->next_usec += callback->period_usec;
                }
                if (!binfo->semaphore.take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
                    continue;
                }
                callback->cb();
                binfo->semaphore.give();
            }
        }

        uint64_t next_needed = 0;
        now = AP_HAL::micros64();

        for (callback = binfo->_callbacks; callback; callback = callback->next) {
            if (next_needed == 0 || callback->next_usec < next_needed) {
                next_needed = callback->next_usec;
                if (next_needed < now) {
                    next_needed = now;
                }
            }
        }

        uint32_t delay_us = 50000;
        if (next_needed >= now && next_needed - now < delay_us) {
            delay_us = next_needed - now;
        }
        if (delay_us < 100) {
            delay_us = 100;
        }

        /* Yield CPU */
        {
            rt_tick_t ticks = (delay_us * RT_TICK_PER_SECOND) / 1000000U;
            if (ticks < 1) ticks = 1;
            rt_thread_delay(ticks);
        }
    }
}

/* ------------------------------------------------------------------
 *  Find or create a DeviceBus instance
 * ------------------------------------------------------------------ */

DeviceBus::DeviceBus(uint8_t thread_priority)
    : _thread_priority(thread_priority)
{
}

DeviceBus *DeviceBus::get_bus(uint8_t bus_num, uint8_t thread_priority)
{
    if (bus_num >= MAX_BUSES) {
        return nullptr;
    }
    if (_buses[bus_num] == nullptr) {
        _buses[bus_num] = new DeviceBus(thread_priority);
    }
    return _buses[bus_num];
}

AP_HAL::Device::PeriodicHandle DeviceBus::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb, AP_HAL::Device *hal_device)
{
    if (!_thread_started) {
        _thread_started = true;

        /* Store the hal_device for future reference (ChibiOS API compat) */
        _hal_device = hal_device;

        char name[RT_NAME_MAX];
        if (hal_device != nullptr) {
            switch (hal_device->bus_type()) {
            case AP_HAL::Device::BUS_TYPE_SPI:
                rt_snprintf(name, sizeof(name), "SPI%u", (unsigned)hal_device->bus_num());
                break;
            case AP_HAL::Device::BUS_TYPE_I2C:
                rt_snprintf(name, sizeof(name), "I2C%u", (unsigned)hal_device->bus_num());
                break;
            default:
                rt_snprintf(name, sizeof(name), "DEV%u", (unsigned)hal_device->bus_num());
                break;
            }
        } else {
            static uint8_t anon_cnt = 0;
            rt_snprintf(name, sizeof(name), "dcb%u", (unsigned)anon_cnt++);
        }

        uint8_t prio = _thread_priority;
        if (prio == 0 || prio >= RT_THREAD_PRIORITY_MAX) {
            prio = RT_THREAD_PRIORITY_MAX / 6;
        }

        /* Find a free slot in the static thread pool */
        int slot = -1;
        for (int i = 0; i < MAX_BUSES; i++) {
            if (!_bus_thread_inited[i]) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            // No free slot — all 8 static threads used
            return nullptr;
        }

        /* Allocate stack for this slot on first use */
        if (!_bus_thread_stacks[slot]) {
            _bus_thread_stacks[slot] = (char *)rt_malloc(BUS_STACK_SIZE);
            if (!_bus_thread_stacks[slot]) {
                rt_kprintf("DeviceBus: failed to allocate %u-byte stack for slot %d\n",
                           BUS_STACK_SIZE, slot);
                return nullptr;
            }
        }

        rt_thread_init(&_bus_thread_objs[slot], name,
                       _bus_thread_entry, this,
                       _bus_thread_stacks[slot], BUS_STACK_SIZE,
                       prio, 20);

        /* Store the thread handle for adjust_timer() ownership check */
        _thread = &_bus_thread_objs[slot];

        rt_thread_startup(&_bus_thread_objs[slot]);
        _bus_thread_inited[slot] = true;
    }

    auto *ci = new callback_info;
    if (ci == nullptr) {
        return nullptr;
    }
    ci->cb = cb;
    ci->period_usec = period_usec;
    ci->next_usec = AP_HAL::micros64() + period_usec;
    ci->next = _callbacks;
    _callbacks = ci;

    return (AP_HAL::Device::PeriodicHandle)ci;
}

bool DeviceBus::adjust_timer(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (h == nullptr) {
        return false;
    }

    /*
     * Thread-context guard: only allow adjustment from within the bus
     * thread itself, to prevent races with the callback dispatch loop.
     * Matches ChibiOS semantics (chThdGetSelfX() != thread_ctx).
     */
    if (rt_thread_self() != _thread) {
        return false;
    }

    auto *ci = (callback_info *)h;
    ci->period_usec = period_usec;
    ci->next_usec = AP_HAL::micros64() + period_usec;
    return true;
}

/* ------------------------------------------------------------------
 *  Bounce buffer support (DMA-safe transfers)
 *
 *  RTT implementation: always allocate a 32-byte-aligned buffer
 *  via rt_malloc_align, copy data in/out.  This is less sophisticated
 *  than ChibiOS (which checks mem_is_dma_safe and uses pre-allocated
 *  pools), but functionally correct for STM32F7/H7 unified memory.
 *
 *  API matches AP_HAL_ChibiOS DeviceBus::bouncebuffer_setup / finish.
 * ------------------------------------------------------------------ */

/*
 * Ensure a bounce buffer of at least 'size' bytes exists.
 * Allocates via rt_malloc_align(32) for DMA-safe alignment.
 * Returns true on success.
 */
bool DeviceBus::_bouncebuffer_ensure(rtt_bouncebuffer_t *&bb, uint32_t size)
{
    if (bb == nullptr) {
        bb = (rtt_bouncebuffer_t *)rt_malloc(sizeof(rtt_bouncebuffer_t));
        if (bb == nullptr) {
            return false;
        }
        memset(bb, 0, sizeof(*bb));
    }

    if (bb->size < size || bb->dma_buf == nullptr) {
        if (bb->dma_buf != nullptr) {
            rt_free_align(bb->dma_buf);
        }
        bb->dma_buf = (uint8_t *)rt_malloc_align(size, 32);
        if (bb->dma_buf == nullptr) {
            bb->size = 0;
            return false;
        }
        bb->size = size;
    }

    return true;
}

void DeviceBus::_bouncebuffer_release(rtt_bouncebuffer_t *bb)
{
    if (bb != nullptr) {
        bb->busy = false;
    }
}

bool DeviceBus::bouncebuffer_setup(const uint8_t *&buf_tx, uint16_t tx_len,
                                   uint8_t *&buf_rx, uint16_t rx_len)
{
    if (buf_rx != nullptr && rx_len > 0) {
        if (!_bouncebuffer_ensure(_bounce_buffer_rx, rx_len)) {
            return false;
        }
        _bounce_buffer_rx->orig_buf = buf_rx;
        _bounce_buffer_rx->busy = true;
        buf_rx = _bounce_buffer_rx->dma_buf;
    }

    if (buf_tx != nullptr && tx_len > 0) {
        if (!_bouncebuffer_ensure(_bounce_buffer_tx, tx_len)) {
            if (buf_rx != nullptr) {
                _bounce_buffer_rx->busy = false;
            }
            return false;
        }
        _bounce_buffer_tx->orig_buf = const_cast<uint8_t *>(buf_tx);
        _bounce_buffer_tx->busy = true;
        memcpy(_bounce_buffer_tx->dma_buf, buf_tx, tx_len);
        buf_tx = _bounce_buffer_tx->dma_buf;
    }

    return true;
}

void DeviceBus::bouncebuffer_finish(const uint8_t *buf_tx, uint8_t *buf_rx, uint16_t rx_len)
{
    if (buf_rx != nullptr && _bounce_buffer_rx != nullptr && _bounce_buffer_rx->busy) {
        if (_bounce_buffer_rx->orig_buf != nullptr && rx_len > 0) {
            memcpy(_bounce_buffer_rx->orig_buf, _bounce_buffer_rx->dma_buf, rx_len);
        }
        _bounce_buffer_rx->busy = false;
        _bounce_buffer_rx->orig_buf = nullptr;
    }

    if (buf_tx != nullptr && _bounce_buffer_tx != nullptr && _bounce_buffer_tx->busy) {
        _bounce_buffer_tx->busy = false;
        _bounce_buffer_tx->orig_buf = nullptr;
    }
}

} // namespace RTT
