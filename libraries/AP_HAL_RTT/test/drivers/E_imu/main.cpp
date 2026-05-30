/**
 * test_E_imu — External module: ICM20689 chip smoke (CUAV V5)
 *
 * Layer: E* (chip-level SPI probe, not AP_InertialSensor full stack).
 * Reads WHO_AM_I (0x75 → 0x98) and PWR_MGMT_1 (0x6B, expect not 0xFF) via
 * hal.spi->get_device("icm20689") — same hwdef SPIDEV as D_spi_hal.
 *
 * Does NOT exercise AP_InertialSensor, INS calibration, FIFO, or sensor fusion.
 * HAL SPIDevice gate: --test=D_spi_hal. Register-level: --test=L4_spi.
 *
 * Build: scons --target=cuav_v5 --test=E_imu -j$(nproc)
 * Runtime PASS requires on-board ICM20689; missing/wrong HW must TEST_FAIL.
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

static const uint8_t ICM20689_WHOAMI_REG = 0x75;
static const uint8_t ICM20689_WHOAMI_VAL = 0x98;
static const uint8_t ICM20689_PWR_MGMT_1 = 0x6B;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static bool read_reg_u8(AP_HAL::SPIDevice &dev, AP_HAL::Semaphore &sem,
                        uint8_t reg, uint8_t &out)
{
    dev.set_read_flag(0x80);
    if (!dev.set_speed(AP_HAL::Device::SPEED_LOW)) {
        return false;
    }
    if (!sem.take(100)) {
        return false;
    }
    const bool ok = dev.read_registers(reg, &out, 1);
    sem.give();
    return ok;
}

static void step_imu_chip_smoke(void)
{
    TEST_STEP("ICM20689 external chip smoke (not AP_InertialSensor)");

    test_printf("    scope=E_imu chip WHO_AM_I; NOT full INS stack\r\n");
    test_printf("    hal.scheduler->delay(200) before SPI probe\r\n");
    hal.scheduler->delay(200);

    AP_HAL::OwnPtr<AP_HAL::SPIDevice> dev = hal.spi->get_device("icm20689");
    TEST_ASSERT(dev, "hal.spi->get_device(icm20689) — check hwdef SPIDEV");

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "SPIDevice semaphore");

    uint8_t whoami = 0;
    TEST_ASSERT(read_reg_u8(*dev, *sem, ICM20689_WHOAMI_REG, whoami),
                "read_registers WHO_AM_I");
    test_printf("    WHO_AM_I reg 0x%02x = 0x%02x (expect 0x%02x)\r\n",
                ICM20689_WHOAMI_REG, whoami, ICM20689_WHOAMI_VAL);
    TEST_ASSERT(whoami != 0xFF, "WHO_AM_I not 0xFF (bus fault)");
    TEST_ASSERT(whoami == ICM20689_WHOAMI_VAL, "ICM20689 WHO_AM_I match");

    uint8_t pwr_mgmt = 0;
    TEST_ASSERT(read_reg_u8(*dev, *sem, ICM20689_PWR_MGMT_1, pwr_mgmt),
                "read_registers PWR_MGMT_1");
    test_printf("    PWR_MGMT_1 reg 0x%02x = 0x%02x (expect != 0xFF)\r\n",
                ICM20689_PWR_MGMT_1, pwr_mgmt);
    TEST_ASSERT(pwr_mgmt != 0xFF, "PWR_MGMT_1 not 0xFF");

    test_printf("    note: overlap with D_spi_hal HAL API gate; E_imu = chip label\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("E_IMU");

    step_imu_chip_smoke();

    TEST_DONE();
    return 0;
}
