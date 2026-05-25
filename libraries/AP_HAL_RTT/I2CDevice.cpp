/*
 * AP_HAL_RTT — I2C device driver implementation
 * Direct CMSIS register-level I2C transfers (no RT-Thread I2C framework).
 * For CUAV V5 (STM32F767): bus 0 = I2C3 (PH7=SCL AF4, PH8=SDA AF4).
 *
 * All four STM32F7 I2C peripherals are on APB1 (PCLK1=54 MHz).
 * TIMINGR values computed with STM32CubeMX for 54 MHz PCLK1.
 */

#include "I2CDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>

#include <stm32f7xx.h>

using namespace RTT;

extern const AP_HAL::HAL& hal;

#ifndef I2C_TIMEOUT_MAX
#define I2C_TIMEOUT_MAX    50000U
#endif

/* ------------------------------------------------------------------ */
/*  TIMINGR values — STM32F7, PCLK1=54 MHz                            */
/* ------------------------------------------------------------------ */
#define I2C_TIMINGR_100KHZ  0x30812E3E
#define I2C_TIMINGR_400KHZ  0x6000030D

/* ------------------------------------------------------------------ */
/*  I2C bus descriptor                                                 */
/* ------------------------------------------------------------------ */
struct I2CBusDescr {
    I2C_TypeDef *regs;              /* CMSIS register base              */
    uint32_t     rcc_enr_bit;       /* RCC_APB1ENR enable bit          */
    uint32_t     rcc_rst_bit;       /* RCC_APB1RSTR reset bit          */
    GPIO_TypeDef *scl_port;         /* SCL GPIO port (NULL = skip GPIO) */
    uint16_t     scl_pin;           /* SCL pin number                   */
    uint8_t      scl_af;            /* SCL alternate function           */
    GPIO_TypeDef *sda_port;         /* SDA GPIO port (NULL = skip GPIO) */
    uint16_t     sda_pin;           /* SDA pin number                   */
    uint8_t      sda_af;            /* SDA alternate function           */
    uint32_t     timingr_100k;      /* TIMINGR for 100 kHz              */
    uint32_t     timingr_400k;      /* TIMINGR for 400 kHz              */
    bool         hw_inited;         /* has hardware been initialised?   */
};

/*
 * Bus table — one entry per supported I2C bus.
 * Order must match HAL_RTT_I2C_BUS_NAMES.
 * GPIO ports filled only for physically-wired buses (CUAV V5: bus 0 only).
 * Bus 1/2/3 (I2C1/2/4) have no physical pins on CUAV V5 per hwdef.dat,
 * but the register definitions are present for code reuse.
 */
static I2CBusDescr _i2c_buses[] = {
    /* Bus 0 — I2C3 (PH7=SCL AF4, PH8=SDA AF4) */
    { I2C3, RCC_APB1ENR_I2C3EN, RCC_APB1RSTR_I2C3RST,
      GPIOH, 7, 4, GPIOH, 8, 4,
      I2C_TIMINGR_100KHZ, I2C_TIMINGR_400KHZ, false },

    /* Bus 1 — I2C1 (PB8=SCL AF4, PB9=SDA AF4) — no physical pins on CUAV V5 */
    { I2C1, RCC_APB1ENR_I2C1EN, RCC_APB1RSTR_I2C1RST,
      GPIOB, 8, 4, GPIOB, 9, 4,
      I2C_TIMINGR_100KHZ, I2C_TIMINGR_400KHZ, false },

    /* Bus 2 — I2C2 (PF1=SCL AF4, PF0=SDA AF4) — no physical pins on CUAV V5 */
    { I2C2, RCC_APB1ENR_I2C2EN, RCC_APB1RSTR_I2C2RST,
      GPIOF, 1, 4, GPIOF, 0, 4,
      I2C_TIMINGR_100KHZ, I2C_TIMINGR_400KHZ, false },

    /* Bus 3 — I2C4 (PF14=SCL AF4, PF15=SDA AF4) — no physical pins on CUAV V5 */
    { I2C4, RCC_APB1ENR_I2C4EN, RCC_APB1RSTR_I2C4RST,
      GPIOF, 14, 4, GPIOF, 15, 4,
      I2C_TIMINGR_100KHZ, I2C_TIMINGR_400KHZ, false },
};
#define I2C_BUS_COUNT (sizeof(_i2c_buses) / sizeof(_i2c_buses[0]))

/* ------------------------------------------------------------------ */
/*  GPIO helper — set pin to AF mode with open-drain + pull-up         */
/* ------------------------------------------------------------------ */
static void _gpio_set_af_od(GPIO_TypeDef *port, uint16_t pin, uint8_t af)
{
    uint32_t moder_shift = pin * 2;
    uint32_t afr_index   = pin / 8;
    uint32_t afr_shift   = (pin % 8) * 4;

    port->MODER   = (port->MODER & ~(3U << moder_shift)) | (2U << moder_shift);
    port->AFR[afr_index]  = (port->AFR[afr_index] & ~(0xFU << afr_shift)) | ((uint32_t)af << afr_shift);
    port->OTYPER  |= (1U << pin);
    port->PUPDR   = (port->PUPDR & ~(3U << moder_shift)) | (1U << moder_shift);
}

/* ------------------------------------------------------------------ */
/*  Hardware initialisation — clock, reset, GPIO, TIMINGR, enable     */
/* ------------------------------------------------------------------ */
static void _i2c_hw_init(uint8_t bus)
{
    if (bus >= I2C_BUS_COUNT) return;
    I2CBusDescr *bd = &_i2c_buses[bus];
    if (bd->hw_inited) return;
    if (bd->regs == NULL) return;

    /* Enable peripheral clock */
    RCC->APB1ENR |= bd->rcc_enr_bit;
    (void)RCC->APB1ENR;

    /* Reset peripheral */
    RCC->APB1RSTR |= bd->rcc_rst_bit;
    __NOP(); __NOP(); __NOP();
    RCC->APB1RSTR &= ~bd->rcc_rst_bit;

    /* Enable GPIO port clock if GPIO pins are defined */
    if (bd->scl_port != NULL && bd->sda_port != NULL) {
        GPIO_TypeDef *ports[] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH };
        uint32_t enr_bits[]  = {
            RCC_AHB1ENR_GPIOAEN, RCC_AHB1ENR_GPIOBEN,
            RCC_AHB1ENR_GPIOCEN, RCC_AHB1ENR_GPIODEN,
            RCC_AHB1ENR_GPIOEEN, RCC_AHB1ENR_GPIOFEN,
            RCC_AHB1ENR_GPIOGEN, RCC_AHB1ENR_GPIOHEN
        };
        for (uint8_t i = 0; i < 8; i++) {
            if (bd->scl_port == ports[i] || bd->sda_port == ports[i]) {
                RCC->AHB1ENR |= enr_bits[i];
            }
        }
        (void)RCC->AHB1ENR;

        _gpio_set_af_od(bd->scl_port, bd->scl_pin, bd->scl_af);
        _gpio_set_af_od(bd->sda_port, bd->sda_pin, bd->sda_af);
    }

    /* Disable peripheral before configuring */
    bd->regs->CR1 &= ~I2C_CR1_PE;
    (void)bd->regs->CR1;

    /* Set timing and own address */
    bd->regs->TIMINGR = bd->timingr_100k;
    bd->regs->OAR1 = (1U << 15);    /* OA1EN — enable own address */

    /* Enable peripheral */
    bd->regs->CR1 |= I2C_CR1_PE;
    (void)bd->regs->CR1;

    bd->hw_inited = true;
}

/* ------------------------------------------------------------------ */
/*  Low-level I2C transfer — polling, register-only                    */
/*  Returns true on success.                                           */
/* ------------------------------------------------------------------ */
static bool _i2c_master_xfer_ll(I2C_TypeDef *i2c, uint8_t addr,
                                const uint8_t *send, uint32_t send_len,
                                uint8_t *recv, uint32_t recv_len)
{
    uint32_t timeout;

    /* Clear sticky error flags */
    i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF |
               I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;

    /* Wait for bus not busy */
    timeout = I2C_TIMEOUT_MAX;
    while ((i2c->ISR & I2C_ISR_BUSY) && --timeout) { __NOP(); }
    if (timeout == 0) {
        i2c->CR2 |= I2C_CR2_STOP;
        return false;
    }

    /* ================================================================== */
    /*  Phase 1 — Transmit (if send_len > 0)                               */
    /* ================================================================== */
    if (send_len > 0 && send != NULL) {
        uint32_t remaining = send_len;
        bool has_recv = (recv_len > 0 && recv != NULL);

        /* CR2: SADD = addr, NBYTES = send_len, START, WRITE (default),
         * AUTOEND only if no receive phase follows (otherwise TC signals
         * end-of-write and we set up the read phase from TC). */
        uint32_t cr2 = ((uint32_t)addr << 1) & 0x000000FEU;
        cr2 |= (remaining << 16) & I2C_CR2_NBYTES;
        cr2 |= I2C_CR2_START;
        if (!has_recv) {
            cr2 |= I2C_CR2_AUTOEND;
        }
        /* RD_WRN = 0 for transmit */
        i2c->CR2 = cr2;

        /* Poll TXIS for each byte */
        while (remaining > 0) {
            timeout = I2C_TIMEOUT_MAX;
            while (!(i2c->ISR & (I2C_ISR_TXIS | I2C_ISR_NACKF |
                                 I2C_ISR_BERR | I2C_ISR_OVR |
                                 I2C_ISR_ARLO)) && --timeout) { __NOP(); }
            if (timeout == 0) {
                i2c->CR2 |= I2C_CR2_STOP;
                return false;
            }
            if (i2c->ISR & (I2C_ISR_NACKF | I2C_ISR_BERR |
                            I2C_ISR_OVR | I2C_ISR_ARLO)) {
                i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_BERRCF |
                           I2C_ICR_OVRCF | I2C_ICR_ARLOCF;
                i2c->CR2 |= I2C_CR2_STOP;
                return false;
            }
            i2c->TXDR = *send++;
            remaining--;
        }

        if (has_recv) {
            /* Wait for TC (transfer complete) before starting receive.
             * TC is cleared by writing CR2 for the next transfer. */
            timeout = I2C_TIMEOUT_MAX;
            while (!(i2c->ISR & (I2C_ISR_TC | I2C_ISR_NACKF |
                                 I2C_ISR_BERR | I2C_ISR_OVR |
                                 I2C_ISR_ARLO)) && --timeout) { __NOP(); }
            if (timeout == 0) {
                i2c->CR2 |= I2C_CR2_STOP;
                return false;
            }
            if (i2c->ISR & (I2C_ISR_NACKF | I2C_ISR_BERR |
                            I2C_ISR_OVR | I2C_ISR_ARLO)) {
                i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_BERRCF |
                           I2C_ICR_OVRCF | I2C_ICR_ARLOCF;
                i2c->CR2 |= I2C_CR2_STOP;
                return false;
            }
        }
    }

    /* ================================================================== */
    /*  Phase 2 — Receive (if recv_len > 0)                                */
    /* ================================================================== */
    if (recv_len > 0 && recv != NULL) {
        uint32_t remaining = recv_len;

        /* CR2: SADD = addr, NBYTES = recv_len, RD_WRN, START, AUTOEND */
        uint32_t cr2 = ((uint32_t)addr << 1) & 0x000000FEU;
        cr2 |= I2C_CR2_RD_WRN;
        cr2 |= (remaining << 16) & I2C_CR2_NBYTES;
        cr2 |= I2C_CR2_START | I2C_CR2_AUTOEND;
        i2c->CR2 = cr2;

        /* Poll RXNE for each byte */
        while (remaining > 0) {
            timeout = I2C_TIMEOUT_MAX;
            while (!(i2c->ISR & (I2C_ISR_RXNE | I2C_ISR_NACKF |
                                 I2C_ISR_BERR | I2C_ISR_OVR |
                                 I2C_ISR_ARLO)) && --timeout) { __NOP(); }
            if (timeout == 0) {
                i2c->CR2 |= I2C_CR2_STOP;
                return false;
            }
            if (i2c->ISR & (I2C_ISR_NACKF | I2C_ISR_BERR |
                            I2C_ISR_OVR | I2C_ISR_ARLO)) {
                i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_BERRCF |
                           I2C_ICR_OVRCF | I2C_ICR_ARLOCF;
                i2c->CR2 |= I2C_CR2_STOP;
                return false;
            }
            *recv++ = (uint8_t)i2c->RXDR;
            remaining--;
        }
    }

    /* Wait for STOPF if AUTOEND was used */
    if ((send_len > 0 && send != NULL && recv_len == 0) ||
        (recv_len > 0 && recv != NULL)) {
        timeout = I2C_TIMEOUT_MAX;
        while (!(i2c->ISR & I2C_ISR_STOPF) && --timeout) { __NOP(); }
        if (timeout == 0) {
            return false;
        }
        i2c->ICR = I2C_ICR_STOPCF;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  I2CDevice methods                                                  */
/* ------------------------------------------------------------------ */

I2CDevice::I2CDevice(uint8_t bus, uint8_t address, uint32_t bus_clock,
                   bool use_smbus, uint32_t timeout_ms)
    : AP_HAL::I2CDevice()
    , _address(address)
    , _busnum(bus)
    , _bus_clock(bus_clock)
    , _timeout_ms(timeout_ms)
    , _split(false)
    , _bus_dev(DeviceBus::get_bus(bus, 0))
{
    set_device_bus(bus);
    set_device_address(address);

    if (bus < I2C_BUS_COUNT) {
        _i2c_hw_init(bus);

        /* Apply bus_clock to TIMINGR */
        I2CBusDescr *bd = &_i2c_buses[bus];
        if (bd->regs != NULL && (bd->regs->CR1 & I2C_CR1_PE)) {
            if (_bus_clock > 100000) {
                bd->regs->TIMINGR = bd->timingr_400k;
            } else {
                bd->regs->TIMINGR = bd->timingr_100k;
            }
        }

        /* Apply SMBus host enable if requested */
        if (use_smbus && bd->regs != NULL) {
            bd->regs->CR1 |= I2C_CR1_SMBHEN;
        }
    }

    (void)use_smbus;
}

I2CDevice::~I2CDevice()
{
}

bool I2CDevice::set_speed(AP_HAL::Device::Speed speed)
{
    if (_busnum >= I2C_BUS_COUNT) {
        return false;
    }

    I2CBusDescr *bd = &_i2c_buses[_busnum];
    if (bd->regs == NULL || !(bd->regs->CR1 & I2C_CR1_PE)) {
        return false;
    }

    /* Disable PE before changing TIMINGR per RM0410 §30.4.2 */
    bd->regs->CR1 &= ~I2C_CR1_PE;
    (void)bd->regs->CR1;
    if (speed == AP_HAL::Device::SPEED_HIGH) {
        bd->regs->TIMINGR = bd->timingr_400k;
    } else {
        bd->regs->TIMINGR = bd->timingr_100k;
    }
    bd->regs->CR1 |= I2C_CR1_PE;
    (void)bd->regs->CR1;

    return true;
}

/*
 * Single-shot I2C transfer: (send) → device → (recv).
 * Combined transfer when both send and recv are present (write then
 * read with repeated start).  Retry on failure.
 */
bool I2CDevice::_do_transfer(const uint8_t *send, uint32_t send_len,
                            uint8_t *recv, uint32_t recv_len)
{
    if (_busnum >= I2C_BUS_COUNT) {
        return false;
    }

    I2CBusDescr *bd = &_i2c_buses[_busnum];
    if (bd->regs == NULL) {
        return false;
    }

    /* Ensure hardware is initialised */
    if (!bd->hw_inited) {
        _i2c_hw_init(_busnum);
    }

    return _i2c_master_xfer_ll(bd->regs, _address,
                               send, send_len, recv, recv_len);
}

bool I2CDevice::transfer(const uint8_t *send, uint32_t send_len,
                        uint8_t *recv, uint32_t recv_len)
{
    if (_bus_dev == nullptr) return false;
    if (!_bus_dev->semaphore.take(HAL_SEMAPHORE_BLOCK_FOREVER)) return false;

    bool ok = false;
    for (uint8_t attempt = 0; attempt <= _retries; attempt++) {
        if (_split) {
            /* Split transfer: separate write and read transactions.
             * Avoids a stop condition with SCL low which some devices
             * (e.g. LidarLite blue label) do not support. */
            bool split_ok = true;
            if (send_len > 0 && send != NULL) {
                if (!_do_transfer(send, send_len, nullptr, 0)) {
                    split_ok = false;
                }
            }
            if (split_ok && recv_len > 0 && recv != NULL) {
                if (!_do_transfer(nullptr, 0, recv, recv_len)) {
                    split_ok = false;
                }
            }
            if (split_ok) {
                ok = true;
                break;
            }
        } else {
            if (_do_transfer(send, send_len, recv, recv_len)) {
                ok = true;
                break;
            }
        }
    }

    _bus_dev->semaphore.give();
    return ok;
}

bool I2CDevice::read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                        uint32_t recv_len, uint8_t times)
{
    for (uint8_t i = 0; i < times; i++) {
        if (!transfer(&first_reg, 1, recv + i * recv_len, recv_len)) {
            return false;
        }
    }
    return true;
}

AP_HAL::Semaphore *I2CDevice::get_semaphore()
{
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
 * CMSIS register-level implementation for all supported buses.
 * Temporarily switches SCL/SDA to GPIO output, open-drain, clocks SCL
 * until SDA is released, then generates STOP and restores AF mode.
 */
void I2CDevice::clear_bus(uint8_t busidx)
{
    if (busidx >= I2C_BUS_COUNT) return;
    I2CBusDescr *bd = &_i2c_buses[busidx];
    if (bd->scl_port == NULL || bd->sda_port == NULL) return;

    GPIO_TypeDef *scl_port = bd->scl_port;
    GPIO_TypeDef *sda_port = bd->sda_port;
    uint16_t scl_pin = bd->scl_pin;
    uint16_t sda_pin = bd->sda_pin;
    uint32_t scl_moder_shift = scl_pin * 2;
    uint32_t sda_moder_shift = sda_pin * 2;

    /* Save MODER and switch to GPIO output, open-drain */
    uint32_t moder_saved = scl_port->MODER;
    uint32_t pupdr_saved = scl_port->PUPDR;

    scl_port->MODER = (scl_port->MODER & ~((3U << scl_moder_shift) | (3U << sda_moder_shift))) |
                       ((1U << scl_moder_shift) | (1U << sda_moder_shift));
    scl_port->OTYPER |= (1U << scl_pin) | (1U << sda_pin);
    scl_port->PUPDR   = (scl_port->PUPDR & ~((3U << scl_moder_shift) | (3U << sda_moder_shift))) |
                        ((1U << scl_moder_shift) | (1U << sda_moder_shift));

    /* Check SDA — if low, clock SCL up to 9 times to free bus */
    if (!(sda_port->IDR & (1U << sda_pin))) {
        for (uint8_t i = 0; i < 9; i++) {
            scl_port->BSRR = 1U << (scl_pin + 16);   /* SCL LOW */
            __NOP(); __NOP(); __NOP(); __NOP();
            scl_port->BSRR = 1U << scl_pin;            /* SCL HIGH (released) */
            __NOP(); __NOP(); __NOP(); __NOP();
            if (sda_port->IDR & (1U << sda_pin)) break;
        }
    }

    /* Generate STOP condition */
    scl_port->BSRR = 1U << (scl_pin + 16);           /* SCL LOW */
    sda_port->BSRR = 1U << (sda_pin + 16);            /* SDA LOW */
    __NOP();
    scl_port->BSRR = 1U << scl_pin;                    /* SCL HIGH */
    __NOP(); __NOP();
    sda_port->BSRR = 1U << sda_pin;                    /* SDA HIGH → STOP */

    /* Restore AF mode */
    scl_port->MODER = moder_saved;
    scl_port->PUPDR = pupdr_saved;
    _gpio_set_af_od(bd->scl_port, bd->scl_pin, bd->scl_af);
    _gpio_set_af_od(bd->sda_port, bd->sda_pin, bd->sda_af);
}

void I2CDevice::clear_all_buses(void)
{
    for (uint8_t i = 0; i < I2C_BUS_COUNT; i++) {
        clear_bus(i);
    }
}
