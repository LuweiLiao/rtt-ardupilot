/**
 * test_D_storage — AP_HAL Storage HAL smoke (Batch B1)
 *
 * Exercises real AP_HAL::Storage via hal.storage: init(), read_block(),
 * write_block() on a small scratch region at the tail of HAL_STORAGE_SIZE.
 * Backs up original bytes, writes a test pattern, verifies read-back, then
 * restores the original content.
 *
 * Data safety: uses the last 8 bytes only (offset HAL_STORAGE_SIZE - 8).
 * Do not run on a board with live parameter storage you cannot afford to
 * touch at that offset; when RTT uses RAM stub backend, scratch is volatile
 * (0xFF after open) and does not touch FRAM/flash.
 *
 * Does not mount SD or exercise filesystem (see E_sdcard).
 * Does not start hal.run() / full scheduler threads.
 *
 * Build: scons --target=cuav_v5 --test=D_storage -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "test_runner.h"
}

#ifndef HAL_STORAGE_SIZE
#define HAL_STORAGE_SIZE 16384
#endif

/* Scratch at end of storage — avoid low offsets used by AP_Param layout */
static const uint16_t TEST_LEN = 8;
static const uint16_t TEST_OFFSET = (uint16_t)(HAL_STORAGE_SIZE - TEST_LEN);

static const uint8_t TEST_PATTERN[TEST_LEN] = {
    0xA5, 0x5A, 0xC3, 0x3C, 0x96, 0x69, 0x0F, 0xF0,
};

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static void step_storage_hal_smoke(void)
{
    TEST_STEP("Storage HAL smoke (AP_HAL API)");

    AP_HAL::Storage *st = hal.storage;
    TEST_ASSERT(st != nullptr, "hal.storage non-null");

    test_printf("    HAL_STORAGE_SIZE=%u scratch offset=%u len=%u\r\n",
                (unsigned)HAL_STORAGE_SIZE,
                (unsigned)TEST_OFFSET,
                (unsigned)TEST_LEN);

    st->init();

    hal.scheduler->delay(50);

    uint8_t backup[TEST_LEN];
    memset(backup, 0, sizeof(backup));
    st->read_block(backup, TEST_OFFSET, TEST_LEN);
    test_printf("    backup bytes:");
    for (unsigned i = 0; i < TEST_LEN; i++) {
        test_printf(" %02x", backup[i]);
    }
    test_printf("\r\n");

    st->write_block(TEST_OFFSET, TEST_PATTERN, TEST_LEN);

    uint8_t readback[TEST_LEN];
    memset(readback, 0, sizeof(readback));
    st->read_block(readback, TEST_OFFSET, TEST_LEN);

    test_printf("    readback bytes:");
    for (unsigned i = 0; i < TEST_LEN; i++) {
        test_printf(" %02x", readback[i]);
    }
    test_printf("\r\n");

    for (unsigned i = 0; i < TEST_LEN; i++) {
        if (readback[i] != TEST_PATTERN[i]) {
            test_printf("    mismatch at %u: got 0x%02x expect 0x%02x\r\n",
                        i, readback[i], TEST_PATTERN[i]);
            TEST_FAIL("read_block pattern mismatch");
        }
    }

    st->write_block(TEST_OFFSET, backup, TEST_LEN);

    uint8_t restored[TEST_LEN];
    memset(restored, 0, sizeof(restored));
    st->read_block(restored, TEST_OFFSET, TEST_LEN);

    for (unsigned i = 0; i < TEST_LEN; i++) {
        if (restored[i] != backup[i]) {
            test_printf("    restore mismatch at %u: got 0x%02x expect 0x%02x\r\n",
                        i, restored[i], backup[i]);
            TEST_FAIL("restore read_block mismatch");
        }
    }

    test_printf("    note: cuav_v5 RTT Storage may use RAM stub (see Storage.cpp)\r\n");
    test_printf("    note: FRAM persistence gate: E_fram / on-board second boot\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_STORAGE");

    step_storage_hal_smoke();

    TEST_DONE();
    return 0;
}
