/*
 * AP_HAL_RTT — SPI device driver
 * Board-independent: uses RTT_SPIDesc from hwdef-generated HAL_SPI_DEVICE_LIST.
 * SPI bus locking is handled internally by rt_spi_send_then_recv / rt_spi_send / rt_spi_recv.
 */

#include "SPIDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <rtthread.h>
#include <drivers/dev_spi.h>

using namespace RTT;

static uint32_t _spi_mode_to_rtt(uint8_t mode)
{
    switch (mode) {
    case 0: return RT_SPI_MODE_0 | RT_SPI_MSB;
    case 1: return RT_SPI_MODE_1 | RT_SPI_MSB;
    case 2: return RT_SPI_MODE_2 | RT_SPI_MSB;
    default: return RT_SPI_MODE_3 | RT_SPI_MSB;
    }
}

SPIDevice::SPIDevice(RTT_SPIDesc &desc)
    : AP_HAL::SPIDevice()
    , _desc(desc)
    , _dev(nullptr)
{
    set_device_bus(desc.bus);
    _dev = (struct rt_spi_device *)rt_device_find(desc.rtt_devname);
    if (_dev != nullptr) {
        set_speed(AP_HAL::Device::SPEED_LOW);
    }
}

SPIDevice::~SPIDevice()
{
}

bool SPIDevice::set_speed(AP_HAL::Device::Speed speed)
{
    if (_dev == nullptr) return false;
    struct rt_spi_configuration cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = _spi_mode_to_rtt(_desc.mode);
    cfg.data_width = 8;
    cfg.max_hz = (speed == AP_HAL::Device::SPEED_HIGH) ? _desc.highspeed : _desc.lowspeed;
    return rt_spi_configure(_dev, &cfg) == RT_EOK;
}

bool SPIDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
    if (_dev == nullptr) return false;
    bool need_sem = !_cs_held;
    if (need_sem && !_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;

    bool ok = false;

    if (send_len > 0 && recv_len > 0) {
        /*
         * ChibiOS-style merged transfer: combine send+recv into a single
         * full-duplex SPI transaction. This halves the number of SPI events
         * vs rt_spi_send_then_recv() which splits into two xfer() calls.
         */
        const uint32_t total = send_len + recv_len;
        uint8_t _bounce[32];
        uint8_t *buf;
        bool heap = false;

        if (total <= sizeof(_bounce)) {
            buf = _bounce;
        } else {
            buf = (uint8_t *)rt_malloc_align(total, 32);
            if (buf == nullptr) {
                if (need_sem) _sem.give();
                return false;
            }
            heap = true;
        }

        memcpy(buf, send, send_len);
        memset(&buf[send_len], 0, recv_len);

        struct rt_spi_message msg = {};
        msg.send_buf   = buf;
        msg.recv_buf   = buf;
        msg.length     = total;
        msg.cs_take    = 1;
        msg.cs_release = 1;
        msg.next       = RT_NULL;

        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        if (ret == RT_NULL) {
            memcpy(recv, &buf[send_len], recv_len);
            ok = true;
        }
        if (heap) rt_free_align(buf);
    } else if (send_len > 0) {
        rt_size_t ret = rt_spi_send(_dev, send, send_len);
        ok = (ret == send_len);
    } else if (recv_len > 0) {
        rt_size_t ret = rt_spi_recv(_dev, recv, recv_len);
        ok = (ret == recv_len);
    }

    if (need_sem) _sem.give();
    return ok;
}

bool SPIDevice::set_chip_select(bool set)
{
    _cs_held = set;
    return true;
}

bool SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv, uint32_t len)
{
    if (_dev == nullptr) return false;
    if (!_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;

    struct rt_spi_message msg;
    msg.send_buf = send;
    msg.recv_buf = recv;
    msg.length = len;
    msg.cs_take = 1;
    msg.cs_release = 1;
    msg.next = RT_NULL;

    bool ok = false;
    if (rt_spi_take_bus(_dev) == RT_EOK && rt_spi_take(_dev) == RT_EOK) {
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        rt_spi_release(_dev);
        rt_spi_release_bus(_dev);
        ok = (ret == RT_NULL);
    }
    _sem.give();
    return ok;
}

AP_HAL::Semaphore *SPIDevice::get_semaphore()
{
    return &_sem;
}

AP_HAL::Device::PeriodicHandle SPIDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return _bus.register_periodic_callback(period_usec, cb, this);
}

bool SPIDevice::adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    return _bus.adjust_timer(h, period_usec);
}
