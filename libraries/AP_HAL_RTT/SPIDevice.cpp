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
    , _bus(DeviceBus::get_bus(desc.bus, 0))
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
         * Merged full-duplex SPI transfer: send command bytes then receive
         * data in a single CS-held transaction.  Use SEPARATE send and recv
         * buffers to avoid DMA aliasing issues on STM32F7 (some drivers
         * overwrite the send buffer with received data when send_buf ==
         * recv_buf, causing the command byte to be corrupted before it is
         * shifted out).
         */
        uint8_t _bounce_tx[32];
        uint8_t _bounce_rx[32];
        uint8_t *txbuf;
        uint8_t *rxbuf;
        bool heap = false;

        if (send_len <= sizeof(_bounce_tx) && recv_len <= sizeof(_bounce_rx)) {
            txbuf = _bounce_tx;
            rxbuf = _bounce_rx;
        } else {
            txbuf = (uint8_t *)rt_malloc_align(send_len, 32);
            rxbuf = (uint8_t *)rt_malloc_align(recv_len, 32);
            if (txbuf == nullptr || rxbuf == nullptr) {
                if (txbuf) rt_free_align(txbuf);
                if (rxbuf) rt_free_align(rxbuf);
                if (need_sem) _sem.give();
                return false;
            }
            heap = true;
        }

        memcpy(txbuf, send, send_len);
        memset(rxbuf, 0, recv_len);

        /*
         * Chain two SPI messages: first send_len bytes (TX only, clock
         * out command), then recv_len bytes (RX, shift in response).
         * Both share the same CS session (cs_take on first, cs_release
         * on last).
         */
        struct rt_spi_message msg_tx = {};
        msg_tx.send_buf   = txbuf;
        msg_tx.recv_buf   = RT_NULL;
        msg_tx.length     = send_len;
        msg_tx.cs_take    = 1;
        msg_tx.cs_release = 0;
        msg_tx.next       = RT_NULL;

        struct rt_spi_message msg_rx = {};
        msg_rx.send_buf   = RT_NULL;
        msg_rx.recv_buf   = rxbuf;
        msg_rx.length     = recv_len;
        msg_rx.cs_take    = 0;
        msg_rx.cs_release = 1;
        msg_rx.next       = RT_NULL;

        msg_tx.next = &msg_rx;

        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg_tx);
        if (ret == RT_NULL) {
            memcpy(recv, rxbuf, recv_len);
            ok = true;
        }
        if (heap) {
            rt_free_align(txbuf);
            rt_free_align(rxbuf);
        }
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

    bool ok = false;
    if (rt_spi_take_bus(_dev) == RT_EOK && rt_spi_take(_dev) == RT_EOK) {
        /*
         * Use separate TX/RX buffers when caller provides the same
         * pointer for both (common in ArduPilot register read patterns).
         * STM32F7 DMA may corrupt the send data if send_buf == recv_buf.
         */
        uint8_t _bounce[64];
        uint8_t *txbuf = (uint8_t *)send;
        uint8_t *rxbuf = recv;
        bool heap = false;
        if (send == recv) {
            if (len <= sizeof(_bounce)) {
                txbuf = _bounce;
                rxbuf = _bounce;
                memcpy(txbuf, send, len);
            } else {
                txbuf = (uint8_t *)rt_malloc_align(len, 32);
                if (txbuf == nullptr) {
                    rt_spi_release(_dev);
                    rt_spi_release_bus(_dev);
                    _sem.give();
                    return false;
                }
                rxbuf = txbuf;
                heap = true;
                memcpy(txbuf, send, len);
            }
        }

        struct rt_spi_message msg;
        msg.send_buf = txbuf;
        msg.recv_buf = rxbuf;
        msg.length = len;
        msg.cs_take = 1;
        msg.cs_release = 1;
        msg.next = RT_NULL;

        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        rt_spi_release(_dev);
        rt_spi_release_bus(_dev);
        ok = (ret == RT_NULL);

        if (send == recv && ok) {
            memcpy(recv, rxbuf, len);
        }
        if (heap) rt_free_align(txbuf);
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
    if (_bus == nullptr) {
        return nullptr;
    }
    return _bus->register_periodic_callback(period_usec, cb, this);
}

bool SPIDevice::adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (_bus == nullptr) {
        return false;
    }
    return _bus->adjust_timer(h, period_usec);
}
