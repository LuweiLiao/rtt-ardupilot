/**
 * test_E_wspi_flash — External module: WSPI / QUADSPI NOR flash
 *
 * Layer: E* (JEDEC ID, page program — board dependent)
 * Status: BUILD_ONLY — cuav_v5 hwdef has no QUADSPI/WSPI; runtime documents N/A.
 *
 * Reference: AP_FlashIface jedec_test examples (H7 boards).
 * Hardware: N/A on CUAV V5; use pixhawk6c_mini or other H7 target when added.
 *
 * Build: scons --target=cuav_v5 --test=E_wspi_flash -j$(nproc)
 *         (must link; on-board test is intentionally skipped)
 */

#include "test_runner.h"
#include <rtthread.h>

static void step_board_capability(void)
{
    TEST_STEP("WSPI / QUADSPI flash");

    test_printf("    status=BUILD_ONLY + N/A on cuav_v5\r\n");
    test_printf("    reason=hwdef has no QUADSPI peripheral wired\r\n");
    test_printf("    action=use H7 board target when E_wspi_flash HW gate exists\r\n");

    TEST_PASS();
}

int main(void)
{
    TEST_INIT("E_WSPI_FLASH");

    step_board_capability();

    TEST_DONE();
    return 0;
}
