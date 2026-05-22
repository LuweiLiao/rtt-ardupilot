/*
 * AP_HAL_RTT — I2C device driver implementation
 * CMSIS register-level I2C3 driver for CUAV V5 (STM32F767).
 * Registered with RT-Thread I2C framework so existing rt_i2c_master_send/recv work.
 *
 * Pinout (hwdef.dat): PH7=I2C3_SCL(AF4), PH8=I2C3_SDA(AF4)
 * Periodic callbacks delegated to DeviceBus.
 */

#include "I2CDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <drivers/dev_i2c.h>

#include <stm32f7xx.h>

using namespace RTT;

#ifndef HAL_RTT_I2C_BUS_NAMES
/* I2C_ORDER from hwdef.dat: I2C3 I2C1 I2C2 I2C4
 * Bus 0 = I2C3 (IST8310 internal compass, PH7/PH8)
 * Bus 1 = I2C1, Bus 2 = I2C2, Bus 3 = I2C4
 */
#define HAL_RTT_I2C_BUS_NAMES "i2c3", "i2c1", "i2c2", "i2c4"
#endif

static const char *const _i2c_bus_names[] = { HAL_RTT_I2C_BUS_NAMES };
#define HAL_RTT_I2C_BUS_COUNT (sizeof(_i2c_bus_names) / sizeof(_i2c_bus_names[0]))

/* ------------------------------------------------------------------ */
/*  CMSIS register-level I2C3 driver                                  */
/* ------------------------------------------------------------------ */
#if defined(HAL_MCU_STM32F7XX) || defined(SOC_SERIES_STM32F7)

#ifndef I2C_TIMEOUT_MAX
#define I2C_TIMEOUT_MAX    50000U
#endif

static bool _i2c3_bus_registered = false;

/*
 * I2C TIMINGR for PCLK1=54MHz, 100kHz Standard Mode (RM0410 §30.4.2):
 *   PRESC=3 → tI2CCLK = 4 * 18.5ns ≈ 74ns
 *   SCLL=67 → 68 * 74ns ≈ 5.03us, SCLH=66 → 67 * 74ns ≈ 4.96us
 *   SDADEL=2, SCLDEL=3
 */
#define I2C3_TIMINGR_100KHZ  0x30812E3E

/*
 * Initialize I2C3 hardware — clocks, GPIO AF4, timing, enable.
 */
static void _i2c3_hw_init(void)
{
    if (_i2c3_bus_registered) return;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN;
    (void)RCC->AHB1ENR;
    RCC->APB1ENR |= RCC_APB1ENR_I2C3EN;
    (void)RCC->APB1ENR;

    RCC->APB1RSTR |= RCC_APB1RSTR_I2C3RST;
    __NOP(); __NOP(); __NOP();
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C3RST;

    GPIOH->MODER = (GPIOH->MODER & ~((3U << 14) | (3U << 16))) |
                    ((2U << 14) | (2U << 16));
    GPIOH->AFR[0] = (GPIOH->AFR[0] & ~(0xFU << 28)) | (4U << 28);
    GPIOH->AFR[1] = (GPIOH->AFR[1] & ~(0xFU << 0))  | (4U << 0);
    GPIOH->OTYPER |= (1U << 7) | (1U << 8);
    GPIOH->PUPDR   = (GPIOH->PUPDR & ~((3U << 14) | (3U << 16))) |
                     ((1U << 14) | (1U << 16));

    I2C3->CR1 &= ~I2C_CR1_PE;
    (void)I2C3->CR1;
    I2C3->TIMINGR = I2C3_TIMINGR_100KHZ;
    I2C3->OAR1 = (1U << 15);
    I2C3->CR1 |= I2C_CR1_PE;
    (void)I2C3->CR1;
}

/* Forward declaration for ops struct */
static rt_ssize_t _i2c3_master_xfer(struct rt_i2c_bus_device *bus,
                                     struct rt_i2c_msg msgs[], rt_uint32_t num);

/* RT-Thread I2C bus operations */
static const struct rt_i2c_bus_device_ops _i2c3_ops = {
    .master_xfer  = _i2c3_master_xfer,
    .slave_xfer   = nullptr,
    .i2c_bus_control = nullptr,
};

static struct rt_i2c_bus_device _i2c3_bus_dev = {
    .ops = &_i2c3_ops,
};

/*
 * Register I2C3 bus with RT-Thread framework.
 */
static void _i2c3_register(void)
{
    if (_i2c3_bus_registered) return;
    _i2c3_hw_init();
    /* rt_i2c_bus_device_register() initialises the device, mutex and linked list.
     * Must be called after RT-Thread kernel is ready (after scheduler start). */
    if (rt_i2c_bus_device_register(&_i2c3_bus_dev, "i2c3") == RT_EOK) {
        _i2c3_bus_registered = true;
    }
}

/*
 * I2C3 master transfer — CMSIS register-level per RM0410 §30.4.3.
 * Called by RT-Thread I2C framework via ops->master_xfer.
 * Supports combined transfers (multi-message write+read with repeated start).
 */
static rt_ssize_t _i2c3_master_xfer(struct rt_i2c_bus_device *bus,
                                     struct rt_i2c_msg msgs[], rt_uint32_t num)
{
    (void)bus;

    /* Track I2C transactions (ChibiOS compat for persistent_data) */
    extern const AP_HAL::HAL& hal;
    hal.util->persistent_data.i2c_count++;

    /* Wait for bus not busy before first transaction */
    uint32_t timeout = I2C_TIMEOUT_MAX;
    while ((I2C3->ISR & I2C_ISR_BUSY) && --timeout) { __NOP(); }
    if (timeout == 0) {
        rt_kprintf("I2CX: BUSY timeout!\n");
        return -1;
    }

    /* Clear all sticky error flags once at the start */
    I2C3->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF |
                I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;

    for (rt_uint32_t m = 0; m < num; m++) {
        struct rt_i2c_msg *msg = &msgs[m];
        uint8_t *buf = msg->buf;
        uint32_t remaining = msg->len;
        bool last_msg = (m == num - 1);

        /* Build CR2: SADD[7:1] at bits 7:1, direction, NBYTES, START */
        uint32_t cr2 = (((uint32_t)(msg->addr) << 1) & 0x000000FEU); /* SADD[7:1] */
        if (msg->flags & RT_I2C_RD) {
            cr2 |= I2C_CR2_RD_WRN;
        }
        cr2 |= ((remaining & 0xFF) << I2C_CR2_NBYTES_Pos);
        cr2 |= I2C_CR2_START;
        if (last_msg) {
            cr2 |= I2C_CR2_AUTOEND;      /* last message: auto STOP */
        }
        /* RELOAD must be 0 for NBYTES <= 255 single-shot */

        I2C3->CR2 = cr2;

        if (msg->flags & RT_I2C_RD) {
            /* Receive — poll RXNE, NACKF, BERR, OVR */
            while (remaining > 0) {
                timeout = I2C_TIMEOUT_MAX;
                while (!(I2C3->ISR & (I2C_ISR_RXNE |
                                      I2C_ISR_NACKF |
                                      I2C_ISR_BERR  |
                                      I2C_ISR_OVR)) && --timeout) { __NOP(); }
                if (timeout == 0) {
                    I2C3->CR2 |= I2C_CR2_STOP;
                    return -1;
                }
                if (I2C3->ISR & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_OVR)) {
                    I2C3->ICR = I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_OVRCF;
                    I2C3->CR2 |= I2C_CR2_STOP;
                    return -1;
                }
                *buf++ = (uint8_t)I2C3->RXDR;
                remaining--;
            }
        } else {
            /* Transmit — poll TXIS, NACKF, BERR, OVR */
            while (remaining > 0) {
                timeout = I2C_TIMEOUT_MAX;
                while (!(I2C3->ISR & (I2C_ISR_TXIS |
                                      I2C_ISR_NACKF |
                                      I2C_ISR_BERR  |
                                      I2C_ISR_OVR)) && --timeout) { __NOP(); }
                if (timeout == 0) {
                    I2C3->CR2 |= I2C_CR2_STOP;
                    return -1;
                }
                if (I2C3->ISR & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_OVR)) {
                    I2C3->ICR = I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_OVRCF;
                    I2C3->CR2 |= I2C_CR2_STOP;
                    return -1;
                }
                I2C3->TXDR = *buf++;
                remaining--;
            }
        }

        if (last_msg) {
            /* Last message with AUTOEND: wait for STOPF */
            timeout = I2C_TIMEOUT_MAX;
            while (!(I2C3->ISR & I2C_ISR_STOPF) && --timeout) { __NOP(); }
            if (timeout == 0) return -1;
            I2C3->ICR = I2C_ICR_STOPCF;
        } else {
            /* Intermediate message without AUTOEND: wait for TC */
            timeout = I2C_TIMEOUT_MAX;
            while (!(I2C3->ISR & I2C_ISR_TC) && --timeout) { __NOP(); }
            if (timeout == 0) return -1;
            /* TC is cleared by writing CR2 with START on next iteration */
        }
    }
    return (rt_ssize_t)num;
}
#endif /* HAL_MCU_STM32F7XX || SOC_SERIES_STM32F7 */

/* ------------------------------------------------------------------ */
/*  I2CDevice class                                                    */
/* ------------------------------------------------------------------ */

I2CDevice::I2CDevice(uint8_t bus, uint8_t address, uint32_t bus_clock,
                   bool use_smbus, uint32_t timeout_ms)
    : AP_HAL::I2CDevice()
    , _bus(nullptr)
    , _address(address)
    , _busnum(bus)
    , _bus_clock(bus_clock)
    , _timeout_ms(timeout_ms)
    , _split(false)
    , _bus_dev(DeviceBus::get_bus(bus, 0))
{
    set_device_bus(bus);
    set_device_address(address);

#if defined(HAL_MCU_STM32F7XX) || defined(SOC_SERIES_STM32F7)
    /* Bus 0 = I2C3 on CUAV V5. Register hardware I2C3 if not yet done. */
    if (bus == 0) {
        _i2c3_register();

        /*
         * Apply bus_clock to I2C3 TIMINGR.
         * ChibiOS reference selects between 100kHz/400kHz TIMINGR
         * based on requested clock (HAL_I2C_F7_100_TIMINGR / _400_TIMINGR).
         */
        if (_bus_clock > 100000) {
            I2C3->TIMINGR = 0x6000030D;  /* 400 kHz (STMCubeMX, PCLK1=54 MHz) */
        } else {
            I2C3->TIMINGR = 0x30812E3E;  /* 100 kHz (default, already set by _i2c3_hw_init) */
        }

        /* Apply SMBus host enable if requested (ChibiOS compat: I2C_CR1_SMBHEN) */
        if (use_smbus) {
            I2C3->CR1 |= I2C_CR1_SMBHEN;
        } else {
            I2C3->CR1 &= ~I2C_CR1_SMBHEN;
        }
    }
#endif

    if (bus < HAL_RTT_I2C_BUS_COUNT) {
        _bus = rt_i2c_bus_device_find(_i2c_bus_names[bus]);
    }

    (void)use_smbus;
}

I2CDevice::~I2CDevice()
{
}

bool I2CDevice::set_speed(AP_HAL::Device::Speed speed)
{
    if (_bus == nullptr) {
        return false;
    }

#if defined(HAL_MCU_STM32F7XX) || defined(SOC_SERIES_STM32F7)
    /* Reconfigure I2C3 TIMINGR based on requested speed */
    if (_busnum == 0 && (I2C3->CR1 & I2C_CR1_PE)) {
        if (speed == AP_HAL::Device::SPEED_HIGH) {
            I2C3->TIMINGR = 0x6000030D;  /* 400 kHz */
        } else {
            I2C3->TIMINGR = 0x30812E3E;  /* 100 kHz */
        }
    }
#else
    (void)speed;
#endif

    return true;
}

bool I2CDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
    if (_bus == nullptr) return false;
    if (_bus_dev == nullptr) return false;
    if (!_bus_dev->semaphore.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;

    bool ok = false;
    for (uint8_t attempt = 0; attempt <= _retries; attempt++) {
        rt_tick_t tick = rt_tick_from_millisecond(_timeout_ms > 0 ? _timeout_ms : 4);
        if (rt_i2c_bus_lock(_bus, tick) != RT_EOK) {
            continue;
        }

        bool xfer_ok = true;

        if (send_len > 0 && recv_len > 0 && !_split) {
            /* Combined transfer: send register addr then read data
             * with repeated start (no STOP between messages).
             * Aligned with ChibiOS I2CDevice::_transfer() which uses
             * i2cMasterTransmitTimeout() for combined SENDRECV. */
            struct rt_i2c_msg msgs[2];
            msgs[0].addr = _address;
            msgs[0].flags = RT_I2C_WR;
            msgs[0].buf  = const_cast<uint8_t *>(send);
            msgs[0].len  = send_len;
            msgs[1].addr = _address;
            msgs[1].flags = RT_I2C_RD;
            msgs[1].buf  = recv;
            msgs[1].len  = recv_len;
            if (rt_i2c_transfer(_bus, msgs, 2) != 2) {
                xfer_ok = false;
            }
        } else {
            if (send_len > 0 && send != nullptr) {
                if (rt_i2c_master_send(_bus, _address, RT_I2C_WR, send, send_len) != (rt_ssize_t)send_len) {
                    xfer_ok = false;
                }
            }
            if (xfer_ok && recv_len > 0 && recv != nullptr) {
                if (rt_i2c_master_recv(_bus, _address, RT_I2C_RD, recv, recv_len) != (rt_ssize_t)recv_len) {
                    xfer_ok = false;
                }
            }
        }

        rt_i2c_bus_unlock(_bus);

        if (xfer_ok) {
            ok = true;
            break;
        }
    }

    /* If bus 0 and no bus found, try registering (STM32F7 only) */
    if (!ok) {
#if defined(HAL_MCU_STM32F7XX) || defined(SOC_SERIES_STM32F7)
        _i2c3_register();
#endif
        _bus = rt_i2c_bus_device_find("i2c3");
        if (_bus != nullptr) {
            return transfer(send, send_len, recv, recv_len);
        }
    }
    if (_bus_dev != nullptr) {
        _bus_dev->semaphore.give();
    }
    return ok;
}

bool I2CDevice::read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                        uint32_t recv_len, uint8_t times)
{
    for (uint8_t i = 0; i < times; i++) {
        if (!read_registers(first_reg, recv + i * recv_len, recv_len)) {
            return false;
        }
    }
    return true;
}

AP_HAL::Semaphore *I2CDevice::get_semaphore()
{
    // Align with ChibiOS: return bus-level semaphore (I2CBus::get_semaphore → &bus.semaphore)
    return &_bus_dev->semaphore;
}

AP_HAL::Device::PeriodicHandle I2CDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    if (_bus_dev == nullptr) {
        return nullptr;
    }
    return _bus_dev->register_periodic_callback(period_usec, cb, this);
}

bool I2CDevice::adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (_bus_dev == nullptr) {
        return false;
    }
    return _bus_dev->adjust_timer(h, period_usec);
}

/*
 * clear_bus — toggle SCL up to 9 times to recover a stuck I2C bus.
 * CMSIS register-level implementation for I2C3 (bus 0, PH7/PH8).
 * Only bus 0 is supported because CUAV V5 only has I2C3 physically wired;
 * buses 1-3 (i2c1/i2c2/i2c4) are registered with RT-Thread but have no
 * physical pins defined in hwdef.dat.
 *
 * D-Cache note: this function touches GPIO registers directly (non-cached
 * peripheral memory on STM32F7). No D-Cache maintenance needed for PIO
 * register access. If DMA is added in the future, bounce buffers or
 * cache clean/invalidate will be required (see DeviceBus::bouncebuffer_*).
 */
void I2CDevice::clear_bus(uint8_t busidx)
{
    if (busidx != 0) return;  /* only I2C3 (bus 0) is hardware-registered */

    uint32_t scl_pin = 7;   /* PH7 */
    uint32_t sda_pin = 8;   /* PH8 */
    volatile uint32_t *moder = &GPIOH->MODER;
    volatile uint32_t *bsrr  = &GPIOH->BSRR;
    volatile uint32_t *idr   = &GPIOH->IDR;

    /* Temporarily switch PH7 and PH8 to GPIO output, open-drain */
    *moder = (*moder & ~((3U << 14) | (3U << 16))) |
             ((1U << 14) | (1U << 16));          /* PH7,PH8 output */
    GPIOH->OTYPER |= (1U << 7) | (1U << 8);      /* open-drain */
    GPIOH->PUPDR   = (GPIOH->PUPDR & ~((3U << 14) | (3U << 16))) |
                     ((1U << 14) | (1U << 16));  /* pull-up */

    /* Check SDA — if low, clock SCL up to 9 times to free bus */
    if (!(*idr & (1U << sda_pin))) {
        for (uint8_t i = 0; i < 9; i++) {
            *bsrr = 1U << (scl_pin + 16);          /* SCL LOW */
            __NOP(); __NOP(); __NOP(); __NOP();
            *bsrr = 1U << scl_pin;                   /* SCL HIGH (released) */
            __NOP(); __NOP(); __NOP(); __NOP();
            if (*idr & (1U << sda_pin)) break;       /* SDA released */
        }
    }

    /* Generate STOP condition */
    *bsrr = 1U << (scl_pin + 16);
    *bsrr = 1U << (sda_pin + 16);         /* SDA LOW */
    __NOP();
    *bsrr = 1U << scl_pin;                  /* SCL HIGH */
    __NOP(); __NOP();
    *bsrr = 1U << sda_pin;                  /* SDA HIGH → STOP */

    /* Restore I2C3 AF mode */
    *moder = (*moder & ~((3U << 14) | (3U << 16))) |
             ((2U << 14) | (2U << 16));
    GPIOH->AFR[0] = (GPIOH->AFR[0] & ~(0xFU << 28)) | (4U << 28);
    GPIOH->AFR[1] = (GPIOH->AFR[1] & ~(0xFU << 0))  | (4U << 0);
}

void I2CDevice::clear_all_buses(void)
{
    clear_bus(0);  /* only bus 0 (I2C3) supported */
}
