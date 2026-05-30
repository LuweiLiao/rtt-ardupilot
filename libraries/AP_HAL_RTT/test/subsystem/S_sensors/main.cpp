/**
 * test_S_sensors — Subsystem: thin IMU + Baro chip combo (no full INS/Baro stack)
 *
 * Layer: S* — reuses E_imu / E_ms5611 chip-level probes in one image.
 * Does NOT link AP_InertialSensor, AP_Baro, sensor fusion, or calibration.
 *
 * MS5611: if hwdef lacks SPIDEV "ms5611", step 2 TEST_FAIL (explicit hwdef msg).
 * IMU: always attempted via "icm20689" (same as D_spi_hal / E_imu).
 *
 * Build: scons --target=cuav_v5 --test=S_sensors -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

static const uint8_t ICM20689_WHOAMI_REG = 0x75;
static const uint8_t ICM20689_WHOAMI_VAL = 0x98;
static const uint8_t ICM20689_PWR_MGMT_1 = 0x6B;

static const uint8_t CMD_MS56XX_RESET = 0x1E;
static const uint8_t CMD_MS56XX_PROM = 0xA0;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static bool imu_read_reg_u8(AP_HAL::SPIDevice &dev, AP_HAL::Semaphore &sem,
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

static uint16_t ms5611_crc4(uint16_t *data)
{
    uint16_t n_rem = 0;

    for (uint8_t cnt = 0; cnt < 16; cnt++) {
        if (cnt & 1) {
            n_rem ^= (uint8_t)(data[cnt >> 1] & 0x00FF);
        } else {
            n_rem ^= (uint8_t)(data[cnt >> 1] >> 8);
        }
        for (uint8_t n_bit = 8; n_bit > 0; n_bit--) {
            if (n_rem & 0x8000) {
                n_rem = (uint16_t)((n_rem << 1) ^ 0x3000);
            } else {
                n_rem = (uint16_t)(n_rem << 1);
            }
        }
    }
    return (n_rem >> 12) & 0xF;
}

static bool ms5611_read_prom_word(AP_HAL::SPIDevice &dev, uint8_t word, uint16_t &out)
{
    const uint8_t reg = (uint8_t)(CMD_MS56XX_PROM + (word << 1));
    uint8_t val[2] = {0, 0};
    if (!dev.transfer(&reg, 1, val, sizeof(val))) {
        return false;
    }
    out = (uint16_t)((val[0] << 8) | val[1]);
    return true;
}

static bool ms5611_prom_crc_ok(uint16_t prom[8])
{
    bool all_zero = true;
    for (uint8_t i = 0; i < 8; i++) {
        if (prom[i] != 0) {
            all_zero = false;
        }
    }
    if (all_zero) {
        return false;
    }

    const uint16_t crc_read = prom[7] & 0xf;
    prom[7] &= 0xff00;
    return crc_read == ms5611_crc4(prom);
}

static void step_imu_subsystem(void)
{
    TEST_STEP("IMU chip (E_imu boundary in S_sensors)");

    test_printf("    scope=ICM20689 WHO_AM_I; NOT AP_InertialSensor\r\n");
    hal.scheduler->delay(200);

    AP_HAL::OwnPtr<AP_HAL::SPIDevice> dev = hal.spi->get_device("icm20689");
    TEST_ASSERT(dev, "hal.spi->get_device(icm20689)");

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "IMU SPIDevice semaphore");

    uint8_t whoami = 0;
    TEST_ASSERT(imu_read_reg_u8(*dev, *sem, ICM20689_WHOAMI_REG, whoami),
                "IMU WHO_AM_I read");
    test_printf("    WHO_AM_I=0x%02x (expect 0x%02x)\r\n", whoami, ICM20689_WHOAMI_VAL);
    TEST_ASSERT(whoami != 0xFF, "IMU WHO_AM_I not 0xFF");
    TEST_ASSERT(whoami == ICM20689_WHOAMI_VAL, "ICM20689 WHO_AM_I match");

    uint8_t pwr_mgmt = 0;
    TEST_ASSERT(imu_read_reg_u8(*dev, *sem, ICM20689_PWR_MGMT_1, pwr_mgmt),
                "IMU PWR_MGMT_1 read");
    TEST_ASSERT(pwr_mgmt != 0xFF, "IMU PWR_MGMT_1 not 0xFF");

    TEST_PASS();
}

static void step_baro_subsystem(void)
{
    TEST_STEP("Baro chip (E_ms5611 boundary in S_sensors)");

    test_printf("    scope=MS5611 PROM/CRC; NOT AP_Baro backend\r\n");

    AP_HAL::OwnPtr<AP_HAL::SPIDevice> dev = hal.spi->get_device("ms5611");
    if (!dev) {
        test_printf("    FAIL: hal.spi->get_device(ms5611) null\r\n");
        test_printf("    reason=hwdef missing SPIDEV ms5611 (cuav_v5 RTT)\r\n");
        test_printf("    action=restore SPIDEV ms5611 then reflash\r\n");
        test_printf("    isolated gates: --test=E_ms5611 same boundary\r\n");
        TEST_FAIL("ms5611 not registered — subsystem baro skip/fail");
    }

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "Baro SPIDevice semaphore");
    TEST_ASSERT(sem->take(200), "Baro semaphore take");

    dev->set_speed(AP_HAL::Device::SPEED_LOW);
    TEST_ASSERT(dev->transfer(&CMD_MS56XX_RESET, 1, nullptr, 0), "MS5611 reset");
    sem->give();
    hal.scheduler->delay(4);

    TEST_ASSERT(sem->take(200), "Baro semaphore take for PROM");

    uint16_t prom[8];
    for (uint8_t i = 0; i < 8; i++) {
        TEST_ASSERT(ms5611_read_prom_word(*dev, i, prom[i]), "PROM word read");
        test_printf("    PROM[%u]=0x%04x\r\n", (unsigned)i, prom[i]);
    }
    sem->give();

    TEST_ASSERT(ms5611_prom_crc_ok(prom), "MS5611 PROM CRC-4");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("S_SENSORS");

    step_imu_subsystem();
    step_baro_subsystem();

    test_printf("    note: NOT full sensor subsystem (no INS/Baro init)\r\n");
    test_printf("    note: scheduler->init() not called\r\n");

    TEST_DONE();
    return 0;
}
