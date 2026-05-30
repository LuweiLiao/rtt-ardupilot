/**
 * test_E_fram — External module: Ramtron FRAM RDID + scratch RW smoke
 *
 * Layer: E* (chip-level SPI FRAM, not AP_Param / full Storage backend).
 * When hwdef registers SPIDEV "ramtron", reads JEDEC-style RDID (cmd 0x9F)
 * and does a 4-byte write/read at offset 0 (restored after test).
 * When "ramtron" is absent from HAL_SPI_DEVICE_LIST, runtime TEST_FAIL with
 * explicit hwdef message (cuav_v5 RTT currently: FRAM N/A).
 *
 * Next step: restore SPIDEV ramtron (+ FRAM_CS) in cuav_v5 hwdef.dat.
 * D_storage HAL smoke uses RAM stub until FRAM is wired.
 *
 * Build: scons --target=cuav_v5 --test=E_fram -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "test_runner.h"
}

static const uint8_t RAMTRON_RDID = 0x9f;
static const uint8_t RAMTRON_RDSR = 0x05;
static const uint8_t RAMTRON_WREN = 0x06;
static const uint8_t RAMTRON_WRITE = 0x02;
static const uint8_t RAMTRON_READ = 0x03;

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static void step_fram_smoke(void)
{
    TEST_STEP("Ramtron FRAM external chip smoke (not AP_Param storage)");

    test_printf("    scope=E_fram RDID+scratch RW; NOT full Storage/AP_RAMTRON\r\n");

    AP_HAL::OwnPtr<AP_HAL::SPIDevice> dev = hal.spi->get_device("ramtron");
    if (!dev) {
        test_printf("    FAIL: hal.spi->get_device(ramtron) returned null\r\n");
        test_printf("    reason=cuav_v5 hwdef missing SPIDEV ramtron in HAL_SPI_DEVICE_LIST\r\n");
        test_printf("    action=add SPIDEV ramtron (+ FRAM_CS) then reflash; see open-issues\r\n");
        TEST_FAIL("ramtron device not registered in hwdef");
    }

    AP_HAL::Semaphore *sem = dev->get_semaphore();
    TEST_ASSERT(sem != nullptr, "SPIDevice semaphore");
    TEST_ASSERT(sem->take(200), "semaphore take(200ms)");

    dev->set_speed(AP_HAL::Device::SPEED_LOW);

    /* Match AP_RAMTRON::init() Cypress layout (9 bytes after cmd 0x9F). */
    uint8_t rdid[9];
    memset(rdid, 0, sizeof(rdid));
    TEST_ASSERT(dev->read_registers(RAMTRON_RDID, rdid, sizeof(rdid)), "RDID read");
    test_printf("    RDID bytes:");
    for (unsigned i = 0; i < sizeof(rdid); i++) {
        test_printf(" %02x", rdid[i]);
    }
    test_printf("\r\n");
    test_printf("    Cypress id1=0x%02x id2=0x%02x (FM25V02A expect 0x22 0x08)\r\n",
                rdid[7], rdid[8]);
    TEST_ASSERT(rdid[7] == 0x22 && rdid[8] == 0x08, "FM25V02A RDID id bytes");

    bool all_ff = true;
    bool all_00 = true;
    for (unsigned i = 0; i < sizeof(rdid); i++) {
        if (rdid[i] != 0xff) {
            all_ff = false;
        }
        if (rdid[i] != 0x00) {
            all_00 = false;
        }
    }
    TEST_ASSERT(!all_ff, "RDID not all 0xFF");
    TEST_ASSERT(!all_00, "RDID not all 0x00");

    uint8_t rdsr = 0;
    TEST_ASSERT(dev->read_registers(RAMTRON_RDSR, &rdsr, 1), "RDSR read");
    test_printf("    RDSR=0x%02x (expect WEL clear, typically 0x00)\r\n", rdsr);

    const uint32_t addr = 0;
    uint8_t backup[4];
    memset(backup, 0, sizeof(backup));
    {
        uint8_t cmd[4] = {RAMTRON_READ,
                          (uint8_t)((addr >> 16) & 0xff),
                          (uint8_t)((addr >> 8) & 0xff),
                          (uint8_t)(addr & 0xff)};
        TEST_ASSERT(dev->transfer(cmd, sizeof(cmd), backup, sizeof(backup)),
                    "read backup @0");
    }

    const uint8_t pattern[4] = {0x5a, 0xa5, 0xc3, 0x3c};
    {
        TEST_ASSERT(dev->transfer(&RAMTRON_WREN, 1, nullptr, 0), "WREN");
        uint8_t cmd[8] = {RAMTRON_WRITE,
                          (uint8_t)((addr >> 16) & 0xff),
                          (uint8_t)((addr >> 8) & 0xff),
                          (uint8_t)(addr & 0xff),
                          pattern[0], pattern[1], pattern[2], pattern[3]};
        TEST_ASSERT(dev->transfer(cmd, sizeof(cmd), nullptr, 0), "write pattern @0");
    }

    uint8_t readback[4];
    memset(readback, 0, sizeof(readback));
    {
        uint8_t cmd[4] = {RAMTRON_READ,
                          (uint8_t)((addr >> 16) & 0xff),
                          (uint8_t)((addr >> 8) & 0xff),
                          (uint8_t)(addr & 0xff)};
        TEST_ASSERT(dev->transfer(cmd, sizeof(cmd), readback, sizeof(readback)),
                    "readback @0");
    }

    for (unsigned i = 0; i < 4; i++) {
        if (readback[i] != pattern[i]) {
            test_printf("    mismatch @%u: got 0x%02x expect 0x%02x\r\n",
                        i, readback[i], pattern[i]);
            sem->give();
            TEST_FAIL("FRAM scratch RW mismatch");
        }
    }

    {
        TEST_ASSERT(dev->transfer(&RAMTRON_WREN, 1, nullptr, 0), "WREN restore");
        uint8_t cmd[8] = {RAMTRON_WRITE,
                          (uint8_t)((addr >> 16) & 0xff),
                          (uint8_t)((addr >> 8) & 0xff),
                          (uint8_t)(addr & 0xff),
                          backup[0], backup[1], backup[2], backup[3]};
        TEST_ASSERT(dev->transfer(cmd, sizeof(cmd), nullptr, 0), "restore @0");
    }

    sem->give();

    test_printf("    note: D_storage uses RAM stub until ramtron in hwdef\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("E_FRAM");

    step_fram_smoke();

    TEST_DONE();
    return 0;
}
