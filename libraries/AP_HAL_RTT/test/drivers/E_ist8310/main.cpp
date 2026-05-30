/**
 * test_E_ist8310 — External module: IST8310 compass chip smoke (CUAV V5)
 *
 * Layer: E* (chip-level I2C probe, not AP_Compass full stack).
 * Reuses D_i2c_hal link/stub pattern; adds IST8310 setup + single measurement
 * and raw XYZ register dump (same register map as AP_Compass_IST8310).
 *
 * Runtime PASS requires on-board IST8310 @ bus0/0x0E; 0xFF / wrong WAI / no
 * field response must TEST_FAIL.
 *
 * Build: scons --target=cuav_v5 --test=E_ist8310 -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/I2CDevice.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

/* AP_Compass_IST8310.cpp */
static const uint8_t IST8310_WAI_REG = 0x00;
static const uint8_t IST8310_WAI_VAL = 0x10;
static const uint8_t IST8310_CNTL1_REG = 0x0A;
static const uint8_t IST8310_CNTL1_SINGLE = 0x01;
static const uint8_t IST8310_CNTL2_REG = 0x0B;
static const uint8_t IST8310_CNTL2_SRST = 0x01;
static const uint8_t IST8310_AVGCNTL_REG = 0x41;
static const uint8_t IST8310_AVGCNTL_VAL = (4U | (4U << 3)); /* XZ_16 | Y_16 */
static const uint8_t IST8310_PDCNTL_REG = 0x42;
static const uint8_t IST8310_PDCNTL_VAL = 0xC0;
static const uint8_t IST8310_OUT_XL_REG = 0x03;

static const uint8_t CUAV_V5_COMPASS_BUS = 0;
static const uint8_t CUAV_V5_IST8310_ADDR = 0x0E;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static int16_t le16_from_buf(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void step_ist8310_chip_smoke(void)
{
    TEST_STEP("IST8310 external chip smoke (not AP_Compass)");

    test_printf("    scope=E_ist8310 chip; gate=D_i2c_hal HAL API\r\n");
    TEST_ASSERT(hal.i2c_mgr != nullptr, "hal.i2c_mgr");

    hal.scheduler->delay(200);

    AP_HAL::OwnPtr<AP_HAL::I2CDevice> dev =
        hal.i2c_mgr->get_device(CUAV_V5_COMPASS_BUS, CUAV_V5_IST8310_ADDR);
    TEST_ASSERT(dev, "i2c_mgr->get_device(bus=0, addr=0x0E)");

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "I2CDevice semaphore");
    TEST_ASSERT(sem->take(100), "semaphore take");

    dev->set_retries(10);
    TEST_ASSERT(dev->set_speed(AP_HAL::Device::SPEED_HIGH), "set_speed(HIGH)");

    TEST_ASSERT(dev->write_register(IST8310_CNTL2_REG, IST8310_CNTL2_SRST),
                "CNTL2 soft reset");
    sem->give();
    hal.scheduler->delay(10);
    TEST_ASSERT(sem->take(100), "semaphore take after SRST");

    uint8_t wai = 0;
    TEST_ASSERT(dev->read_registers(IST8310_WAI_REG, &wai, 1), "read WAI");
    test_printf("    WAI reg 0x%02x = 0x%02x (expect 0x%02x)\r\n",
                IST8310_WAI_REG, wai, IST8310_WAI_VAL);
    TEST_ASSERT(wai != 0xFF, "WAI not 0xFF");
    TEST_ASSERT(wai == IST8310_WAI_VAL, "IST8310 WAI match");

    TEST_ASSERT(dev->write_register(IST8310_AVGCNTL_REG, IST8310_AVGCNTL_VAL),
                "AVGCNTL setup");
    TEST_ASSERT(dev->write_register(IST8310_PDCNTL_REG, IST8310_PDCNTL_VAL),
                "PDCNTL setup");

    TEST_ASSERT(dev->write_register(IST8310_CNTL1_REG, IST8310_CNTL1_SINGLE),
                "CNTL1 single measurement");
    sem->give();
    hal.scheduler->delay(15);
    TEST_ASSERT(sem->take(100), "semaphore take for XYZ");

    uint8_t raw[6];
    TEST_ASSERT(dev->read_registers(IST8310_OUT_XL_REG, raw, sizeof(raw)),
                "read XYZ raw bytes");
    sem->give();

    const int16_t rx = le16_from_buf(&raw[0]);
    const int16_t ry = le16_from_buf(&raw[2]);
    const int16_t rz = le16_from_buf(&raw[4]);
    test_printf("    raw XYZ: x=%d y=%d z=%d (reg 0x03..0x08)\r\n", rx, ry, rz);
    test_printf("    raw bytes: %02x %02x %02x %02x %02x %02x\r\n",
                raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);

    TEST_ASSERT(rx != 0 || ry != 0 || rz != 0, "field not all zero");
    TEST_ASSERT(rx != -1 && ry != -1 && rz != -1, "field not 0xFFFF pattern");

    test_printf("    note: chip-level only; NOT AP_Compass / calibration\r\n");
    test_printf("    note: scheduler->init() not called\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("E_IST8310");

    step_ist8310_chip_smoke();

    TEST_DONE();
    return 0;
}
