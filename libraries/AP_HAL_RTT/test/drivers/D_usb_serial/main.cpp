/**
 * test_D_usb_serial — HAL USB CDC as SERIAL0 (CherryUSB + UARTDriver)
 *
 * Exercises hal.serial(0) over CherryUSB CDC (hal_usb_cherryusb_shim.c).
 * Console / test_runner remains on UART7 via test_runner.c.
 *
 * Host: connect USB; open /dev/ttyACM* (VID 0x1209 PID 0x5741); expect banner
 * line after DTR. Optional: type bytes for echo on SERIAL0.
 *
 * Build: scons --target=cuav_v5 --test=D_usb_serial -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "test_runner.h"
#include "hal_usb_lld_rtt.h"
void ap_rtt_iwdg_kick(void);
extern volatile uint32_t test_fail_count;
}

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static bool wait_usb_ready(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        usb_lld_poll_rtt();
        if (usb_lld_is_configured_rtt()) {
            return true;
        }
        hal.scheduler->delay(10);
        waited += 10;
        ap_rtt_iwdg_kick();
    }
    return false;
}

static void pump_usb_tx(AP_HAL::UARTDriver *uart, unsigned rounds)
{
    if (uart == nullptr) {
        return;
    }
    for (unsigned i = 0; i < rounds; i++) {
        usb_lld_poll_rtt();
        uart->flush();
        hal.scheduler->delay(2);
    }
}

static void step_usb_serial_hal_smoke(void)
{
    TEST_STEP("CherryUSB init + HAL serial(0) CDC TX");

    test_printf("    hal.scheduler->delay(300) before USB init\r\n");
    hal.scheduler->delay(300);

    if (!usb_lld_init_rtt()) {
        test_printf("    usb_lld_init_rtt() returned false\r\n");
        TEST_FAIL("usb_lld_init_rtt failed");
        return;
    }
    test_printf("    usb_lld_init_rtt() ok\r\n");

    test_printf("    waiting USB configured (host cable + enumerate)...\r\n");
    /* SDIO + shell init can delay enumeration well past 20s on CUAV V5 */
    if (!wait_usb_ready(60000U)) {
        test_printf("    timeout — connect USB host and retry\r\n");
        TEST_FAIL("USB not configured in time");
        return;
    }
    test_printf("    USB configured (cherry_configured)\r\n");

    AP_HAL::UARTDriver *const s0 = hal.serial(0);
    TEST_ASSERT(s0 != nullptr, "serial(0) USB CDC driver");

    s0->begin(921600);
    test_printf("    serial(0): begin(921600) ok\r\n");

    s0->printf("D_usb_serial HAL CDC smoke on SERIAL0\r\n");

    static const char wr[] = "D_usb_serial write probe\r\n";
    const uint32_t n = s0->write((const uint8_t *)wr, (uint32_t)strlen(wr));
    test_printf("    serial(0): write returned %lu bytes\r\n", (unsigned long)n);
    TEST_ASSERT(n == strlen(wr), "write byte count");

    pump_usb_tx(s0, 80);

    /* Brief RX window: host echo test (non-fatal if idle) */
    usb_lld_poll_rtt();
    const uint32_t avail = s0->available();
    test_printf("    serial(0): rx available=%lu (optional host echo)\r\n", (unsigned long)avail);
    if (avail > 0) {
        uint8_t rx[64];
        const uint32_t got = s0->read(rx, avail > sizeof(rx) ? sizeof(rx) : avail);
        test_printf("    serial(0): read %lu bytes\r\n", (unsigned long)got);
    }

    test_printf("    host: open ACM 1209:5741 for banner + optional echo\r\n");
    test_printf("    note: UART7=test_runner; no scheduler->init()\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_USB_SERIAL");

    step_usb_serial_hal_smoke();

    test_printf("  [D_USB_SERIAL] RESULT: %s (beacon/echo loop follows)\r\n",
                test_fail_count == 0 ? "PASS" : "FAIL");

    uint32_t beacon_ms = 0;
    while (1) {
        usb_lld_poll_rtt();
        AP_HAL::UARTDriver *const s0 = hal.serial(0);
        if (s0 != nullptr && usb_lld_is_configured_rtt()) {
            pump_usb_tx(s0, 1);
            beacon_ms += 5;
            if (beacon_ms >= 3000U) {
                beacon_ms = 0;
                s0->printf("D_usb_serial CDC beacon\r\n");
                pump_usb_tx(s0, 40);
            }
            const uint32_t avail = s0->available();
            if (avail > 0) {
                uint8_t buf[64];
                const uint32_t n = s0->read(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
                if (n > 0) {
                    s0->write(buf, n);
                    pump_usb_tx(s0, 4);
                }
            }
        }
        hal.scheduler->delay(5);
        ap_rtt_iwdg_kick();
    }

    TEST_DONE();
    return 0;
}
