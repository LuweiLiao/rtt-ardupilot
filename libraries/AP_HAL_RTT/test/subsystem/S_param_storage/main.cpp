/**
 * test_S_param_storage — Subsystem: storage-backed param path smoke
 *
 * Layer: S* (subsystem) — exercises HAL Storage as the backend used by
 * AP_Param / StorageManager in full ArduPilot, without linking vehicle
 * param tables or AP_Param::load_all().
 *
 * Scope (this image):
 *   - hal.storage init + scratch read/write/restore at tail-16B
 *   - HAL_STORAGE_SIZE sanity check
 *   - Documents NOT full AP_Param round-trip (no var_info / GCS download)
 *
 * HAL-only equivalent: --test=D_storage (tail-8B scratch).
 * Full param gate: ArduCopter + pymavlink param download (L0 / status.md).
 *
 * Build: scons --target=cuav_v5 --test=S_param_storage -j$(nproc)
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

static const uint16_t SUBSYS_SCRATCH_LEN = 16;
static const uint16_t SUBSYS_SCRATCH_OFF =
    (uint16_t)(HAL_STORAGE_SIZE - SUBSYS_SCRATCH_LEN);

static const uint8_t SUBSYS_PATTERN[SUBSYS_SCRATCH_LEN] = {
    0x53, 0x50, 0x41, 0x52, 0x41, 0x4D, 0x53, 0x4D,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
};

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static void step_storage_subsystem_smoke(void)
{
    TEST_STEP("Storage subsystem smoke (NOT full AP_Param)");

    test_printf("    boundary=HAL Storage only; NO AP_Param::load_all/save\r\n");
    test_printf("    HAL layer gate=D_storage; full params=vehicle build\r\n");

    TEST_ASSERT(HAL_STORAGE_SIZE >= 4096U, "HAL_STORAGE_SIZE >= 4KiB");

    AP_HAL::Storage *st = hal.storage;
    TEST_ASSERT(st != nullptr, "hal.storage non-null");

    test_printf("    HAL_STORAGE_SIZE=%u scratch off=%u len=%u\r\n",
                (unsigned)HAL_STORAGE_SIZE,
                (unsigned)SUBSYS_SCRATCH_OFF,
                (unsigned)SUBSYS_SCRATCH_LEN);

    st->init();
    hal.scheduler->delay(50);

    uint8_t backup[SUBSYS_SCRATCH_LEN];
    memset(backup, 0, sizeof(backup));
    st->read_block(backup, SUBSYS_SCRATCH_OFF, SUBSYS_SCRATCH_LEN);

    st->write_block(SUBSYS_SCRATCH_OFF, SUBSYS_PATTERN, SUBSYS_SCRATCH_LEN);

    uint8_t readback[SUBSYS_SCRATCH_LEN];
    memset(readback, 0, sizeof(readback));
    st->read_block(readback, SUBSYS_SCRATCH_OFF, SUBSYS_SCRATCH_LEN);

    for (unsigned i = 0; i < SUBSYS_SCRATCH_LEN; i++) {
        if (readback[i] != SUBSYS_PATTERN[i]) {
            test_printf("    mismatch at %u: got 0x%02x expect 0x%02x\r\n",
                        i, readback[i], SUBSYS_PATTERN[i]);
            TEST_FAIL("subsystem storage readback mismatch");
        }
    }

    st->write_block(SUBSYS_SCRATCH_OFF, backup, SUBSYS_SCRATCH_LEN);

    uint8_t restored[SUBSYS_SCRATCH_LEN];
    memset(restored, 0, sizeof(restored));
    st->read_block(restored, SUBSYS_SCRATCH_OFF, SUBSYS_SCRATCH_LEN);

    for (unsigned i = 0; i < SUBSYS_SCRATCH_LEN; i++) {
        if (restored[i] != backup[i]) {
            TEST_FAIL("subsystem storage restore mismatch");
        }
    }

    test_printf("    note: cuav_v5 may use RAM stub (volatile across reset)\r\n");
    test_printf("    note: FRAM persistence -> E_fram / second-boot check\r\n");
    test_printf("    note: AP_Param needs vehicle var_info — out of scope\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("S_PARAM_STORAGE");

    step_storage_subsystem_smoke();

    TEST_DONE();
    return 0;
}
