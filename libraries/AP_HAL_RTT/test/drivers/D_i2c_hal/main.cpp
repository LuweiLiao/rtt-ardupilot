/**
 * test_D_i2c_hal — AP_HAL I2CDevice HAL smoke (Batch A3)
 *
 * Exercises hal.i2c_mgr->get_device() on CUAV V5 bus 0 (I2C3) / IST8310 @ 0x0E.
 * Reads WAI register (reg 0x00, expect 0x10) with semaphore + read_registers,
 * matching AP_Compass_IST8310 probe semantics. Runtime PASS requires on-board
 * compass; 0xFF or wrong ID must TEST_FAIL (no fake PASS).
 *
 * Build: scons --target=cuav_v5 --test=D_i2c_hal -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/I2CDevice.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

/* AP_Compass_IST8310.cpp — WAI_REG / DEVICE_ID */
static const uint8_t IST8310_WAI_REG = 0x00;
static const uint8_t IST8310_WAI_VAL = 0x10;
static const uint8_t IST8310_CNTL2_REG = 0x0B;
static const uint8_t IST8310_CNTL2_SRST = 0x01;

/* hwdef.dat: I2C3 = bus 0, PROBE_MAG_I2C(IST8310, 0, 0x0E, ...) */
static const uint8_t CUAV_V5_COMPASS_BUS = 0;
static const uint8_t CUAV_V5_IST8310_ADDR = 0x0E;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static void step_i2c_hal_smoke(void)
{
    TEST_STEP("I2CDevice HAL smoke (AP_HAL API)");

    TEST_ASSERT(hal.i2c_mgr != nullptr, "hal.i2c_mgr");

    test_printf("    hal.scheduler->delay(200) before I2C probe\r\n");
    hal.scheduler->delay(200);

    AP_HAL::OwnPtr<AP_HAL::I2CDevice> dev =
        hal.i2c_mgr->get_device(CUAV_V5_COMPASS_BUS, CUAV_V5_IST8310_ADDR);
    TEST_ASSERT(dev, "i2c_mgr->get_device(bus=0, addr=0x0E)");

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "I2CDevice semaphore");
    TEST_ASSERT(sem->take(100), "semaphore take(100ms)");

    dev->set_retries(10);
    TEST_ASSERT(dev->set_speed(AP_HAL::Device::SPEED_HIGH), "set_speed(SPEED_HIGH)");

    /* IST8310: soft-reset before WAI (see AP_Compass_IST8310::init) */
    const bool srst_ok = dev->write_register(IST8310_CNTL2_REG, IST8310_CNTL2_SRST);
    test_printf("    write_register(CNTL2, SRST) -> %s\r\n", srst_ok ? "ok" : "FAIL");
    sem->give();
    hal.scheduler->delay(10);
    TEST_ASSERT(sem->take(100), "semaphore take after SRST delay");

    uint8_t wai = 0;
    const bool rd_ok = dev->read_registers(IST8310_WAI_REG, &wai, 1);
    sem->give();

    test_printf("    read_registers(0x%02x) -> %s, wai=0x%02x (expect 0x%02x)\r\n",
                IST8310_WAI_REG,
                rd_ok ? "ok" : "FAIL",
                wai,
                IST8310_WAI_VAL);

    TEST_ASSERT(rd_ok, "read_registers WAI transfer");
    TEST_ASSERT(wai != 0xFF, "WAI not 0xFF (bus fault / NACK)");
    TEST_ASSERT(wai == IST8310_WAI_VAL, "IST8310 WAI match");

    test_printf("    target: I2C3 bus index 0, addr 0x0E (hwdef HAL_MAG_PROBE_LIST)\r\n");
    test_printf("    note: register gate for raw I2C3: L5_i2c (when manifest)\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_I2C_HAL");

    step_i2c_hal_smoke();

    TEST_DONE();
    return 0;
}
