/*
 * AP_HAL_RTT — SPI device driver
 * Board-independent: uses RTT_SPIDesc from hwdef-generated HAL_SPI_DEVICE_LIST.
 * Uses RT-Thread SPI framework API (rt_spi_transfer_message) to ensure proper
 * SPI configuration and DMA/polling path selection.
 *
 * SPI1 uses direct register-level polling on STM32F7/CUAV-V5: the RTT HAL
 * polling path returns incorrect data for multi-byte reads.  Root cause
 * suspected in HAL_SPI_TransmitReceive interaction with the STM32F7 SPI FIFO.
 */

#include "SPIDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <rtthread.h>
#include <drivers/dev_spi.h>

#ifdef SOC_SERIES_STM32F7
#include <stm32f7xx.h>
#endif

using namespace RTT;

/* SPI transfer debug counter */
static volatile uint32_t rtt_dbg_spi_xfer_count = 0;

/* SPI1 runtime diagnostic — read via GDB */
volatile struct {
    uint32_t spi1_xfer_calls;
    uint32_t spi1_tx_bytes;
    uint32_t spi1_rx_bytes;
    uint32_t last_recv_0;
    uint32_t last_recv_1;
} rtt_spi1_rt = {};

/*
 * SPI1 register-level polling transfer on STM32F7.
 * The RTT HAL polling path (HAL_SPI_TransmitReceive) returns incorrect data
 * for multi-byte reads on SPI1.  Direct register-level polling bypasses this.
 * CS pins must be initialised HIGH (inactive) before calling this.
 */
/*
 * Detect full-duplex: transfer_fullduplex(buf, len) calls
 * transfer(buf, len, buf, len), so send==recv and send_len==recv_len.
 * In full-duplex mode we exchange max(send_len, recv_len) bytes
 * simultaneously.  In half-duplex (write-then-read) mode we exchange
 * send_len + recv_len bytes sequentially.
 */
static bool spi1_poll_transfer(struct rt_spi_device *dev,
                                const uint8_t *send, uint32_t send_len,
                                uint8_t *recv, uint32_t recv_len,
                                bool cs_take, bool cs_release)
{
    const bool fullduplex = (send_len > 0 && recv_len > 0 &&
                             send == recv && send_len == recv_len);
    const uint32_t total_len = fullduplex ? send_len : (send_len + recv_len);
    uint8_t _bounce[64];
    uint8_t *buf;
    bool heap = false;

    if (total_len == 0) return true;

    if (total_len <= sizeof(_bounce)) {
        buf = _bounce;
    } else {
        buf = (uint8_t *)rt_malloc_align(total_len, 32);
        if (buf == nullptr) return false;
        heap = true;
    }

    if (send_len > 0) memcpy(buf, send, send_len);
    if (!fullduplex && recv_len > 0) memset(buf + send_len, 0, recv_len);

    /* Assert CS via GPIO BSRR */
    if (cs_take) {
        rt_base_t cs = dev->cs_pin;
        uint32_t port_idx = cs >> 4;
        uint32_t pin = cs & 0xF;
        volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
        *bsrr = 1U << (pin + 16);
    }

    SPI_TypeDef *spi = SPI1;
    CLEAR_BIT(spi->CR1, SPI_CR1_SPE);

    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
               SPI_CR1_CPOL | SPI_CR1_CPHA |
               SPI_CR1_BR_0 | SPI_CR1_BR_1;  /* /16 */
    spi->CR2 = SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2 | SPI_CR2_FRXTH;
    SET_BIT(spi->CR1, SPI_CR1_SPE);

    /* Flush stale FIFO */
    while (spi->SR & SPI_SR_RXNE) { (void)*((__IO uint8_t *)&spi->DR); }
    (void)spi->SR;

    for (uint32_t i = 0; i < total_len; i++) {
        uint32_t timeout = 10000;
        while (!(spi->SR & SPI_SR_TXE) && --timeout) { __NOP(); }
        *((__IO uint8_t *)&spi->DR) = buf[i];
        timeout = 10000;
        while (!(spi->SR & SPI_SR_RXNE) && --timeout) { __NOP(); }
        buf[i] = *((__IO uint8_t *)&spi->DR);
    }

    uint32_t timeout = 10000;
    while ((spi->SR & SPI_SR_BSY) && --timeout) { __NOP(); }

    /* Release CS via GPIO BSRR */
    if (cs_release) {
        rt_base_t cs = dev->cs_pin;
        uint32_t port_idx = cs >> 4;
        uint32_t pin = cs & 0xF;
        volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
        *bsrr = 1U << pin;
    }

    if (fullduplex) {
        memcpy(recv, buf, recv_len);
    } else {
        memcpy(recv, buf + send_len, recv_len);
    }

    /* Runtime diagnostic */
    rtt_spi1_rt.spi1_xfer_calls++;
    rtt_spi1_rt.spi1_tx_bytes += send_len;
    rtt_spi1_rt.spi1_rx_bytes += recv_len;
    if (recv_len > 0) rtt_spi1_rt.last_recv_0 = fullduplex ? buf[0] : buf[send_len];
    if (recv_len > 1) rtt_spi1_rt.last_recv_1 = fullduplex ? buf[1] : buf[send_len + 1];

    if (heap) { rt_free_align(buf); }
    return true;
}

static uint32_t _spi_mode_to_rtt(uint8_t mode)
{
    switch (mode) {
    case 0: return RT_SPI_MODE_0;
    case 1: return RT_SPI_MODE_1;
    case 2: return RT_SPI_MODE_2;
    default: return RT_SPI_MODE_3;
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

bool SPIDevice::_lock_bus()
{
    if (_dev == nullptr || _dev->bus == nullptr || _dev->bus->ops == nullptr) {
        return false;
    }
    if (_bus_locked) {
        return true;
    }
    if (rt_mutex_take(&(_dev->bus->lock), RT_WAITING_FOREVER) != RT_EOK) {
        return false;
    }
    if (_config_dirty || _dev->bus->owner != _dev) {
        if (_dev->bus->ops->configure(_dev, &_dev->config) != RT_EOK) {
            rt_mutex_release(&(_dev->bus->lock));
            return false;
        }
        _dev->bus->owner = _dev;
        _config_dirty = false;
    }
    return true;
}

void SPIDevice::_unlock_bus()
{
    if (_dev != nullptr && _dev->bus != nullptr && !_bus_locked) {
        rt_mutex_release(&(_dev->bus->lock));
    }
}

bool SPIDevice::set_speed(AP_HAL::Device::Speed speed)
{
    if (_dev == nullptr) return false;
    const uint32_t target_hz =
        (speed == AP_HAL::Device::SPEED_HIGH) ? _desc.highspeed : _desc.lowspeed;

    if (_dev->config.mode == _spi_mode_to_rtt(_desc.mode) &&
        _dev->config.data_width == 8 &&
        _dev->config.max_hz == target_hz) {
        return true;
    }

    _dev->config.mode = _spi_mode_to_rtt(_desc.mode) | RT_SPI_MSB;
    _dev->config.data_width = 8;
    _dev->config.max_hz = target_hz;
    _config_dirty = true;
    return true;
}

/*
 * SPI1 register-level polling transfer on STM32F7.
 * Direct register polling bypasses broken HAL_SPI_TransmitReceive path.
 * TODO: investigate LLD DMA path once SOC_SERIES_STM32F7 build issue is fixed.
 */

bool SPIDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
    if (_dev == nullptr) return false;

    bool need_sem = !_cs_held;
    if (need_sem && !_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;
    if (!_cs_held && !_lock_bus()) {
        if (need_sem) { _sem.give(); }
        return false;
    }

    bool ok = false;
    const bool cs_take = !_cs_held;
    const bool cs_release = !_cs_held;

#ifdef SOC_SERIES_STM32F7
    /* STM32F7: ALL transfers use direct register-level polling.
     * The RTT HAL polling path (HAL_SPI_TransmitReceive) returns incorrect
     * data for multi-byte reads on all SPI buses on STM32F7/CUAV-V5. */
    if (true) {
        if (send_len > 0 || recv_len > 0) {
            ok = spi1_poll_transfer(_dev, send, send_len, recv, recv_len,
                                    cs_take, cs_release);
        } else {
            ok = true; /* no-op */
        }
        rtt_dbg_spi_xfer_count += 2;
        if (!_cs_held) { _unlock_bus(); }
        if (need_sem) { _sem.give(); }
        return ok;
    }
#endif

    if (send_len > 0 && recv_len > 0) {
        uint8_t _bounce[64];
        const uint32_t total_len = send_len + recv_len;
        uint8_t *buf;
        bool heap = false;

        if (total_len <= sizeof(_bounce)) {
            buf = _bounce;
        } else {
            buf = (uint8_t *)rt_malloc_align(total_len, 32);
            if (buf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                if (need_sem) _sem.give();
                return false;
            }
            heap = true;
        }

        memcpy(buf, send, send_len);
        memset(buf + send_len, 0, recv_len);

        struct rt_spi_message msg = {};
        msg.send_buf = buf;
        msg.recv_buf = buf;
        msg.length = total_len;
        msg.cs_take = cs_take ? 1U : 0U;
        msg.cs_release = cs_release ? 1U : 0U;
        msg.next = RT_NULL;

        rtt_dbg_spi_xfer_count++;
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        if (ret == RT_NULL) {
            memcpy(recv, buf + send_len, recv_len);
            ok = true;
        }
        rtt_dbg_spi_xfer_count++;

        if (heap) { rt_free_align(buf); }
    } else if (send_len > 0) {
        uint8_t _bounce_rx[64];
        uint8_t *rxbuf = _bounce_rx;
        bool heap = false;

        if (send_len > sizeof(_bounce_rx)) {
            rxbuf = (uint8_t *)rt_malloc_align(send_len, 32);
            if (rxbuf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                if (need_sem) { _sem.give(); }
                return false;
            }
            heap = true;
        }

        memset(rxbuf, 0, send_len);

        struct rt_spi_message msg = {};
        msg.send_buf = send;
        msg.recv_buf = rxbuf;
        msg.length = send_len;
        msg.cs_take = cs_take ? 1U : 0U;
        msg.cs_release = cs_release ? 1U : 0U;
        msg.next = RT_NULL;

        rtt_dbg_spi_xfer_count++;
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        ok = (ret == RT_NULL);
        rtt_dbg_spi_xfer_count++;

        if (heap) { rt_free_align(rxbuf); }
    } else if (recv_len > 0) {
        struct rt_spi_message msg = {};
        msg.send_buf = RT_NULL;
        msg.recv_buf = recv;
        msg.length = recv_len;
        msg.cs_take = cs_take ? 1U : 0U;
        msg.cs_release = cs_release ? 1U : 0U;
        msg.next = RT_NULL;

        rtt_dbg_spi_xfer_count++;
        struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
        ok = (ret == RT_NULL);
        rtt_dbg_spi_xfer_count++;
    }

    if (!_cs_held) { _unlock_bus(); }
    if (need_sem) { _sem.give(); }
    return ok;
}

bool SPIDevice::set_chip_select(bool set)
{
    if (_dev == nullptr) {
        return false;
    }

    if (set) {
        if (_cs_held) {
            return true;
        }
        if (!_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
            return false;
        }
        if (!_lock_bus()) {
            _sem.give();
            return false;
        }
        if (rt_spi_take(_dev) != RT_EOK) {
            _unlock_bus();
            _sem.give();
            return false;
        }
        _bus_locked = true;
        _cs_held = true;
        return true;
    }

    if (!_cs_held) {
        return true;
    }

    const bool ok = (rt_spi_release(_dev) == RT_EOK);
    _bus_locked = false;
    _cs_held = false;
    _unlock_bus();
    _sem.give();
    return ok;
}

bool SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv, uint32_t len)
{
    if (_dev == nullptr) return false;
    const bool need_sem = !_cs_held;
    if (need_sem && !_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
        return false;
    }
    if (!_cs_held && !_lock_bus()) {
        if (need_sem) { _sem.give(); }
        return false;
    }

    const bool cs_take = !_cs_held;
    const bool cs_release = !_cs_held;

#ifdef SOC_SERIES_STM32F7
    /* STM32F7: use direct register-level polling (same as transfer()).
     * The RTT HAL polling path returns incorrect data for multi-byte
     * reads on all SPI buses on STM32F7/CUAV-V5. */
    if (true) {
        bool ok = (len > 0)
            ? spi1_poll_transfer(_dev, send, len, recv, len, cs_take, cs_release)
            : true;
        rtt_dbg_spi_xfer_count += 2;
        if (!_cs_held) { _unlock_bus(); }
        if (need_sem) { _sem.give(); }
        return ok;
    }
#endif

    uint8_t _bounce[64];
    uint8_t *txbuf = (uint8_t *)send;
    uint8_t *rxbuf = recv;
    bool heap = false;
    bool ok = false;
    if (send == recv) {
        if (len <= sizeof(_bounce)) {
            txbuf = _bounce;
            rxbuf = _bounce;
            memcpy(txbuf, send, len);
        } else {
            txbuf = (uint8_t *)rt_malloc_align(len, 32);
            if (txbuf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                if (need_sem) { _sem.give(); }
                return false;
            }
            rxbuf = txbuf;
            heap = true;
            memcpy(txbuf, send, len);
        }
    }

    struct rt_spi_message msg = {};
    msg.send_buf = txbuf;
    msg.recv_buf = rxbuf;
    msg.length = len;
    msg.cs_take = !_cs_held ? 1U : 0U;
    msg.cs_release = !_cs_held ? 1U : 0U;
    msg.next = RT_NULL;

    rtt_dbg_spi_xfer_count++;
    struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
    ok = (ret == RT_NULL);
    rtt_dbg_spi_xfer_count++;

    if (send == recv && ok) {
        memcpy(recv, rxbuf, len);
    }
    if (heap) { rt_free_align(txbuf); }
    if (!_cs_held) { _unlock_bus(); }
    if (need_sem) { _sem.give(); }
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
