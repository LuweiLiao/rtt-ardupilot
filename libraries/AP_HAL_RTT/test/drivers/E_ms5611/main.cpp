/**
 * test_E_ms5611 — External module: MS5611 baro PROM + CRC smoke
 *
 * Layer: E* (chip-level SPI, not AP_Baro / AP_Baro_MS5611 full backend).
 * When hwdef registers SPIDEV "ms5611", reads factory PROM and checks CRC-4
 * (MS5611 datasheet). When "ms5611" is absent from HAL_SPI_DEVICE_LIST, fails
 * at runtime with an explicit hwdef message (cuav_v5 RTT currently: PROM N/A).
 *
 * Next step to enable HW path: restore SPIDEV ms5611 (+ MS5611_CS) in
 * libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat without touching unrelated lines.
 *
 * Build: scons --target=cuav_v5 --test=E_ms5611 -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

static const uint8_t CMD_MS56XX_RESET = 0x1E;
static const uint8_t CMD_MS56XX_PROM = 0xA0;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

/* MS5611 PROM CRC-4 — AP_Baro_MS5611 / AP_Math crc_crc4 (inlined for minimal link) */
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

static bool read_prom_word(AP_HAL::SPIDevice &dev, uint8_t word, uint16_t &out)
{
    const uint8_t reg = (uint8_t)(CMD_MS56XX_PROM + (word << 1));
    uint8_t val[2] = {0, 0};
    if (!dev.transfer(&reg, 1, val, sizeof(val))) {
        return false;
    }
    out = (uint16_t)((val[0] << 8) | val[1]);
    return true;
}

static bool prom_crc_ok(uint16_t prom[8])
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

static void step_ms5611_smoke(void)
{
    TEST_STEP("MS5611 external chip smoke (not AP_Baro backend)");

    test_printf("    scope=E_ms5611 PROM/CRC; NOT AP_Baro_MS5611 init path\r\n");

    AP_HAL::OwnPtr<AP_HAL::SPIDevice> dev = hal.spi->get_device("ms5611");
    if (!dev) {
        test_printf("    FAIL: hal.spi->get_device(ms5611) returned null\r\n");
        test_printf("    reason=cuav_v5 hwdef missing SPIDEV ms5611 in HAL_SPI_DEVICE_LIST\r\n");
        test_printf("    action=add SPIDEV ms5611 (+ BARO line) then reflash; see open-issues\r\n");
        TEST_FAIL("ms5611 device not registered in hwdef");
    }

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "SPIDevice semaphore");
    TEST_ASSERT(sem->take(200), "semaphore take(200ms)");

    dev->set_speed(AP_HAL::Device::SPEED_LOW);
    TEST_ASSERT(dev->transfer(&CMD_MS56XX_RESET, 1, nullptr, 0), "MS5611 reset");
    sem->give();
    hal.scheduler->delay(4);

    TEST_ASSERT(sem->take(200), "semaphore take(200ms) for PROM");

    uint16_t prom[8];
    for (uint8_t i = 0; i < 8; i++) {
        TEST_ASSERT(read_prom_word(*dev, i, prom[i]), "PROM word read");
        test_printf("    PROM[%u]=0x%04x\r\n", (unsigned)i, prom[i]);
    }
    sem->give();

    TEST_ASSERT(prom_crc_ok(prom), "MS5611 PROM CRC-4");

    test_printf("    note: HAL BARO probe macros may still be disabled in hwdef\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("E_MS5611");

    step_ms5611_smoke();

    TEST_DONE();
    return 0;
}
