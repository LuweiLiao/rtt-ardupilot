/*
 * AP_HAL_RTT — SPI device driver
 * Board-independent: uses RTT_SPIDesc from hwdef-generated HAL_SPI_DEVICE_LIST.
 */

#include "SPIDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <rtthread.h>
#include <drivers/dev_spi.h>

#ifdef SOC_SERIES_STM32F7
#include <stm32f7xx.h>

/* STM32F7 SPI1 GPIO pin configuration (register-level).
 * Called once, then guarded by _spi1_gpio_init_done.  The GPIO MODER/AFR
 * for MISO/MOSI may be clobbered by other peripheral init (e.g. USART6 on PA6
 * on some boards) so we restore on first transfer only — repeated init
 * creates glitches that confuse IMU slaves during CS-held burst reads.
 *
 * Pinout (CUAV V5, from hwdef.dat):
 *   PG11=SCK(AF5), PA6=MISO(AF5), PD7=MOSI(AF5)
 *   PF2=ICM20689_CS, PF3=ICM20602_CS, PF4=BMI055_GYRO_CS */
static void _spi1_gpio_init(void)
{
    /* Enable GPIO clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN |
                    RCC_AHB1ENR_GPIOFEN | RCC_AHB1ENR_GPIOGEN;
    (void)RCC->AHB1ENR;
    /* Ensure SPI1 peripheral clock is enabled */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    /* PG11 SCK: MODE=AF(10), AF=AF5(0101) */
    GPIOG->MODER = (GPIOG->MODER & ~(3U << 22)) | (2U << 22);
    GPIOG->AFR[1] = (GPIOG->AFR[1] & ~(0xFU << 12)) | (5U << 12);

    /* PA6 MISO: MODE=AF(10), AF=AF5(0101) */
    GPIOA->MODER = (GPIOA->MODER & ~(3U << 12)) | (2U << 12);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFU << 24)) | (5U << 24);

    /* PD7 MOSI: MODE=AF(10), AF=AF5(0101) */
    GPIOD->MODER = (GPIOD->MODER & ~(3U << 14)) | (2U << 14);
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~(0xFU << 28)) | (5U << 28);

    /* CS pins: OUTPUT, INITIAL STATE HIGH */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 4)) | (1U << 4);  /* PF2 OUT */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 6)) | (1U << 6);  /* PF3 OUT */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 8)) | (1U << 8);  /* PF4 OUT */
    GPIOF->BSRR = (1U << 2) | (1U << 3) | (1U << 4);         /* set HIGH */
}

/* STM32F7 SPI4 GPIO pin configuration (register-level).
 * Used for MS5611 barometer (and optionally external SPI devices).
 * Called once, then guarded by _spi4_gpio_init_done.
 *
 * Pinout (CUAV V5, from hwdef.dat):
 *   PE2=SCK(AF5), PE13=MISO(AF5), PE6=MOSI(AF5)
 *   PF10=MS5611_CS */
static bool _spi4_gpio_init_done = false;
static void _spi4_gpio_init(void)
{
    if (_spi4_gpio_init_done) return;

    /* Enable GPIO clocks for PORTE and PORTF */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;
    /* Ensure SPI4 peripheral clock is enabled (APB2) */
    RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
    (void)RCC->APB2ENR;

    /* PE2 SPI4_SCK: MODE=AF(10), AF=AF5(0101) */
    GPIOE->MODER = (GPIOE->MODER & ~(3U << 4)) | (2U << 4);
    GPIOE->AFR[0] = (GPIOE->AFR[0] & ~(0xFU << 8)) | (5U << 8);

    /* PE13 SPI4_MISO: MODE=AF(10), AF=AF5(0101) */
    GPIOE->MODER = (GPIOE->MODER & ~(3U << 26)) | (2U << 26);
    GPIOE->AFR[1] = (GPIOE->AFR[1] & ~(0xFU << 20)) | (5U << 20);

    /* PE6 SPI4_MOSI: MODE=AF(10), AF=AF5(0101) */
    GPIOE->MODER = (GPIOE->MODER & ~(3U << 12)) | (2U << 12);
    GPIOE->AFR[0] = (GPIOE->AFR[0] & ~(0xFU << 24)) | (5U << 24);

    /* PF10 MS5611_CS: OUTPUT, INITIAL STATE HIGH */
    GPIOF->MODER = (GPIOF->MODER & ~(3U << 20)) | (1U << 20);
    GPIOF->BSRR = (1U << 10);  /* set PF10 HIGH */

    _spi4_gpio_init_done = true;
}
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
 * CS pin lookup table — matches rtt_devname to GPIO pin number.
 * Values correspond to HAL_RTT_SPI_ATTACH_LIST CS pins.
 */
struct spi_cs_entry {
    const char *rtt_devname;
    rt_base_t cs_pin;
};
static const struct spi_cs_entry _spi_cs_table[] = {
    {"spi11", 82},
    {"spi12", 83},
    {"spi13", 84},
    {"spi14", 106},
    {"spi21", 85},
    {"spi41", 90},
};

static rt_base_t _lookup_cs_pin(const char *rtt_devname)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(_spi_cs_table); i++) {
        if (strcmp(_spi_cs_table[i].rtt_devname, rtt_devname) == 0) {
            return _spi_cs_table[i].cs_pin;
        }
    }
    return 0;
}

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
/*
 * Map AP bus number to STM32F7 SPI peripheral.
 */
static SPI_TypeDef *bus_to_spi(uint8_t bus)
{
    switch (bus) {
    case 1: return SPI1;
    case 2: return SPI2;
#ifdef SPI3
    case 3: return SPI3;
#endif
#ifdef SPI4
    case 4: return SPI4;
#endif
#ifdef SPI5
    case 5: return SPI5;
#endif
#ifdef SPI6
    case 6: return SPI6;
#endif
    default: return SPI1;
    }
}

static bool spi1_poll_transfer(struct rt_spi_device *dev,
                                const uint8_t *send, uint32_t send_len,
                                uint8_t *recv, uint32_t recv_len,
                                bool cs_take, bool cs_release,
                                SPI_TypeDef *spi,
                                rt_base_t cs_pin)
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
        rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
        uint32_t port_idx = cs >> 4;
        uint32_t pin = cs & 0xF;
        volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
        *bsrr = 1U << (pin + 16);
    }

    /*
     * Only re-initialize SPI on standalone transactions (cs_take == true).
     * When CS is held across calls (ICM20689 multi-part read: send addr
     * then read data), mid-transaction SPE toggle creates a clock glitch
     * that confuses the IMU slave, causing RXNE never to set.
     * Ref: ICM20689 112-byte full-duplex read hang.
     */
    if (cs_take) {
        CLEAR_BIT(spi->CR1, SPI_CR1_SPE);

        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                   SPI_CR1_CPOL | SPI_CR1_CPHA |
                   SPI_CR1_BR_0 | SPI_CR1_BR_1;  /* /16 */
        spi->CR2 = SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2 | SPI_CR2_FRXTH;
        SET_BIT(spi->CR1, SPI_CR1_SPE);

        /* Flush stale FIFO */
        while (spi->SR & SPI_SR_RXNE) { (void)*((__IO uint8_t *)&spi->DR); }
        (void)spi->SR;
    }

    for (uint32_t i = 0; i < total_len; i++) {
        uint32_t timeout = 100000;
        while (!(spi->SR & SPI_SR_TXE) && --timeout) { __NOP(); }
        if (timeout == 0) {
            if (cs_release) {
                rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                *(volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U) = 1U << pin;
            }
            CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
            while (spi->SR & SPI_SR_RXNE) { (void)(*(__IO uint8_t *)&spi->DR); }
            if (heap) { rt_free_align(buf); }
            return false;
        }
        *((__IO uint8_t *)&spi->DR) = buf[i];
        timeout = 100000;
        while (!(spi->SR & SPI_SR_RXNE) && --timeout) { __NOP(); }
        if (timeout == 0) {
            (void)*((__IO uint8_t *)&spi->DR); // drain DR
            if (cs_release) {
                rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                *(volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U) = 1U << pin;
            }
            CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
            while (spi->SR & SPI_SR_RXNE) { (void)(*(__IO uint8_t *)&spi->DR); }
            if (heap) { rt_free_align(buf); }
            return false;
        }
        buf[i] = *((__IO uint8_t *)&spi->DR);
    }

    uint32_t timeout = 10000;
    while ((spi->SR & SPI_SR_BSY) && --timeout) { __NOP(); }

    /* Release CS via GPIO BSRR */
    if (cs_release) {
        rt_base_t cs = (cs_pin != 0) ? cs_pin : dev->cs_pin;
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
    , _cs_pin(0)
{
    set_device_bus(desc.bus);
    _cs_pin = _lookup_cs_pin(desc.rtt_devname);
    /* For STM32F7 SPI1 devices (IMUs), use register-level polling directly,
     * bypassing RT-Thread's SPI framework which has DMA and GPIO config issues.
     * See _spi1_gpio_init() and spi1_poll_transfer() for the polling path. */
#ifndef FORCE_RTT_SPI_FRAMEWORK
    if (_desc.bus == 1 || _desc.bus == 4) {
        _dev = nullptr;
        return;
    }
#endif
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
#ifdef SOC_SERIES_STM32F7
    if (_dev == nullptr) return true; /* register-level polling, speed configured per-transfer */
#endif
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
 */

bool SPIDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
#ifdef SOC_SERIES_STM32F7
    if (_dev == nullptr) {
        if (_desc.bus == 4) {
            _spi4_gpio_init();
        } else {
            _spi1_gpio_init();
        }
        if (send_len > 0 || recv_len > 0) {
            bool need_sem = !_cs_held;
            if (need_sem && !_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;
            bool ok = spi1_poll_transfer(nullptr, send, send_len, recv, recv_len,
                                         !_cs_held, !_cs_held, bus_to_spi(_desc.bus), _cs_pin);
            if (!_cs_held && need_sem) _sem.give();
            return ok;
        }
        return true;
    }
#endif
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
#ifdef SOC_SERIES_STM32F7
    if (_dev == nullptr) {
        if (set && !_cs_held) {
            /* Actually assert CS via GPIO BSRR — callers (Invensense IMU
             * driver) expect the pin to be driven LOW (active) after
             * set_chip_select(true) so that subsequent transfer() /
             * transfer_fullduplex() calls with cs_take=false happen while
             * CS is asserted, enabling multi-byte burst reads (e.g.
             * ICM20689 112-byte FIFO read). */
            if (_desc.bus == 4) {
                _spi4_gpio_init();
            } else {
                _spi1_gpio_init();
            }
            rt_base_t cs = (_cs_pin != 0) ? _cs_pin : 0;
            if (cs != 0) {
                uint32_t port_idx = cs >> 4;
                uint32_t pin = cs & 0xF;
                volatile uint32_t *bsrr = (volatile uint32_t *)(0x40020000U + port_idx * 0x400U + 0x18U);
                *bsrr = 1U << (pin + 16);  /* BR = drive LOW */
            }
        }
        _cs_held = set;
        return true;
    }
#endif
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
        _cs_held = true;
        return true;
    }

    if (_cs_held) {
        _cs_held = false;
        _sem.give();
    }
    return true;
}

/*
 * transfer_fullduplex — send and receive simultaneously.
 */
bool SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv, uint32_t len)
{
    if (_dev == nullptr) {
#ifdef SOC_SERIES_STM32F7
        if (_desc.bus == 4) {
            _spi4_gpio_init();
        } else {
            _spi1_gpio_init();
        }
        if (len > 0) {
            return spi1_poll_transfer(nullptr, send, len, recv, len,
                                      !_cs_held, !_cs_held, bus_to_spi(_desc.bus), _cs_pin);
        }
#endif
        return false;
    }

    bool need_sem = !_cs_held;
    if (need_sem && !_sem.take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
        return false;
    }
    if (!_cs_held && !_lock_bus()) {
        if (need_sem) { _sem.give(); }
        return false;
    }

    const bool cs_take = !_cs_held;
    const bool cs_release = !_cs_held;

    uint8_t _bounce[64];
    uint8_t *txbuf = (uint8_t *)send;
    uint8_t *rxbuf = recv;
    bool ok = false;
    bool heap = false;

    if (send == recv) {
        if (len <= sizeof(_bounce)) {
            txbuf = _bounce;
            rxbuf = _bounce;
        } else {
            txbuf = (uint8_t *)rt_malloc_align(len, 32);
            if (txbuf == nullptr) {
                if (!_cs_held) { _unlock_bus(); }
                if (need_sem) { _sem.give(); }
                return false;
            }
            rxbuf = txbuf;
            heap = true;
        }
        memcpy(txbuf, send, len);
    }

    struct rt_spi_message msg = {};
    msg.send_buf = txbuf;
    msg.recv_buf = rxbuf;
    msg.length = len;
    msg.cs_take = cs_take ? 1U : 0U;
    msg.cs_release = cs_release ? 1U : 0U;
    msg.next = RT_NULL;

    rtt_dbg_spi_xfer_count += 2;
    struct rt_spi_message *ret = rt_spi_transfer_message(_dev, &msg);
    ok = (ret == RT_NULL);
    if (ok && send == recv) {
        memcpy(recv, rxbuf, len);
    }
    rtt_dbg_spi_xfer_count++;

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
    return _bus->register_periodic_callback(period_usec, cb, this);
}

bool SPIDevice::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    return _bus->adjust_timer(h, period_usec);
}
