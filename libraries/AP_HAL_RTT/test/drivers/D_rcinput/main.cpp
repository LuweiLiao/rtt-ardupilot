/**
 * test_D_rcinput — AP_HAL RCInput HAL smoke (Batch C2)
 *
 * Exercises real AP_HAL::RCInput via hal.rcin: init, new_input, num_channels, read.
 * Polls RCInput::_timer_tick() and UARTDriver::_timer_tick() without
 * hal.scheduler->init() / ap_rcin thread. Image built with AP_RCPROTOCOL_ENABLED=0
 * (no AP_RCProtocol link); SBUS decode on target needs a future build with protocol.
 *
 * Pass: num_channels() > 0 after poll (requires live SBUS/PPM when protocol enabled).
 * Fail: no channels after timeout — does not fake PASS without RC source.
 *
 * Build: scons --target=cuav_v5 --test=D_rcinput -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>

#include "RCInput.h"
#include "UARTDriver.h"

extern "C" {
#include "test_runner.h"
}

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static const uint32_t POLL_MS = 3000;
static const uint32_t POLL_STEP_MS = 5;

static void tick_rcin_path(void)
{
    auto *rcin = static_cast<RTT::RCInput *>(hal.rcin);
    if (rcin != nullptr) {
        rcin->_timer_tick();
    }
    for (uint8_t i = 0; i < 10; i++) {
        auto *uart = static_cast<RTT::UARTDriver *>(hal.serial(i));
        if (uart != nullptr) {
            uart->_timer_tick();
        }
    }
}

static void step_rcinput_hal_smoke(void)
{
    TEST_STEP("RCInput HAL smoke (AP_HAL API)");

    test_printf("    hardware: SBUS/PPM receiver or simulator on board RC UART\r\n");
    test_printf("    note: AP_RCPROTOCOL_ENABLED=0 in this test image\r\n");
    test_printf("    note: no receiver => TEST_FAIL (not BUILD_ONLY)\r\n");
    test_printf("    note: scheduler->init() not called; manual _timer_tick poll\r\n");

    test_printf("    hal.scheduler->delay(200) before rcin->init()\r\n");
    hal.scheduler->delay(200);

    hal.rcin->init();
    test_printf("    rcin->init() ok\r\n");

    const bool had_input_before = hal.rcin->new_input();
    test_printf("    rcin->new_input() after init => %u\r\n", had_input_before ? 1U : 0U);

    uint8_t nch = hal.rcin->num_channels();
    test_printf("    rcin->num_channels() after init => %u\r\n", (unsigned)nch);

    uint32_t elapsed = 0;
    while (elapsed < POLL_MS) {
        tick_rcin_path();
        hal.scheduler->delay(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;

        nch = hal.rcin->num_channels();
        if (nch > 0) {
            break;
        }
    }

    test_printf("    polled %lu ms for RC frames\r\n", (unsigned long)elapsed);

    nch = hal.rcin->num_channels();
    if (nch == 0) {
        test_printf("    BLOCKED: no RC channels (num_channels=0)\r\n");
        test_printf("    connect SBUS/PPM receiver or simulator; check RC UART mapping\r\n");
        TEST_FAIL("no RC input — SBUS/PPM source required");
    }

    if (nch > 16) {
        nch = 16;
    }

    const bool fresh = hal.rcin->new_input();
    test_printf("    rcin->new_input() with signal => %u\r\n", fresh ? 1U : 0U);

    test_printf("    channels (%u):\r\n", (unsigned)nch);
    for (uint8_t i = 0; i < nch; i++) {
        const uint16_t v = hal.rcin->read(i);
        test_printf("      ch%u: %u us\r\n", (unsigned)(i + 1), (unsigned)v);
        TEST_ASSERT(v >= 800 && v <= 2200, "read() PWM in plausible RC range");
    }

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_RCINPUT");

    step_rcinput_hal_smoke();

    TEST_DONE();
    return 0;
}
