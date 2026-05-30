/**
 * test_D_spi_hal — AP_HAL SPIDevice HAL smoke (Batch A2)
 *
 * Exercises real AP_HAL::SPIDevice via hal.spi->get_device() on the hwdef
 * device table (CUAV V5: "icm20689" on SPI1). Reads ICM20689 WHO_AM_I (reg
 * 0x75, expect 0x98) using semaphore + set_speed + read_registers — same
 * semantics as AP_InertialSensor_Invensense and bringup L4_spi.
 *
 * Does not start hal.run() / full scheduler threads. Runtime PASS requires
 * on-board ICM20689; wrong/missing hardware must TEST_FAIL (no fake PASS).
 *
 * Build: scons --target=cuav_v5 --test=D_spi_hal -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

/* MPUREG_WHOAMI / MPU_WHOAMI_ICM20689 — AP_InertialSensor_Invensense_registers.h */
static const uint8_t ICM20689_WHOAMI_REG = 0x75;
static const uint8_t ICM20689_WHOAMI_VAL = 0x98;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static void step_spi_hal_smoke(void)
{
    TEST_STEP("SPIDevice HAL smoke (AP_HAL API)");

    test_printf("    hal.scheduler->delay(200) before SPI probe\r\n");
    hal.scheduler->delay(200);

    AP_HAL::OwnPtr<AP_HAL::SPIDevice> dev = hal.spi->get_device("icm20689");
    TEST_ASSERT(dev, "hal.spi->get_device(icm20689)");

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "SPIDevice semaphore");
    TEST_ASSERT(sem->take(100), "semaphore take(100ms)");

    dev->set_read_flag(0x80);
    TEST_ASSERT(dev->set_speed(AP_HAL::Device::SPEED_LOW), "set_speed(SPEED_LOW)");

    uint8_t whoami = 0;
    const bool rd_ok = dev->read_registers(ICM20689_WHOAMI_REG, &whoami, 1);
    sem->give();

    test_printf("    read_registers(0x%02x) -> %s, whoami=0x%02x (expect 0x%02x)\r\n",
                ICM20689_WHOAMI_REG,
                rd_ok ? "ok" : "FAIL",
                whoami,
                ICM20689_WHOAMI_VAL);

    TEST_ASSERT(rd_ok, "read_registers WHO_AM_I transfer");
    TEST_ASSERT(whoami != 0xFF, "WHO_AM_I not 0xFF (bus fault)");
    TEST_ASSERT(whoami == ICM20689_WHOAMI_VAL, "ICM20689 WHO_AM_I match");

    test_printf("    note: device name from hwdef HAL_SPI_DEVICE_LIST\r\n");
    test_printf("    note: register gate for raw SPI1: --test=L4_spi\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_SPI_HAL");

    step_spi_hal_smoke();

    TEST_DONE();
    return 0;
}
