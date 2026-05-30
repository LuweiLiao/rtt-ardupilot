/**
 * test_S_mavlink_usb — Subsystem: USB CDC + MAVLink HEARTBEAT on hal.serial(0)
 *
 * CherryUSB + HAL UARTDriver serial(0), same link as D_usb_serial.
 * Host: open ACM 1209:5741 @ 921600; pymavlink wait_heartbeat or parse msgid 0.
 *
 * Build: scons --target=cuav_v5 --test=S_mavlink_usb -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "test_runner.h"
#include "hal_usb_lld_rtt.h"
void ap_rtt_iwdg_kick(void);
}

#include "ardupilotmega/mavlink.h"

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static const uint8_t k_mav_sys_id = 1;
static const uint8_t k_mav_comp_id = MAV_COMP_ID_AUTOPILOT1;

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

static uint16_t pack_heartbeat(uint8_t *buf, size_t buf_len)
{
    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack(
        k_mav_sys_id,
        k_mav_comp_id,
        &msg,
        MAV_TYPE_QUADROTOR,
        MAV_AUTOPILOT_ARDUPILOTMEGA,
        0,
        0,
        MAV_STATE_STANDBY);
    return mavlink_msg_to_send_buffer(buf, &msg);
}

static void step_mavlink_usb_smoke(void)
{
    TEST_STEP("CherryUSB + HAL serial(0) MAVLink HEARTBEAT");

    test_printf("    hal.scheduler->delay(300) before USB init\r\n");
    hal.scheduler->delay(300);

    if (!usb_lld_init_rtt()) {
        test_printf("    usb_lld_init_rtt() returned false\r\n");
        TEST_FAIL("usb_lld_init_rtt failed");
        return;
    }
    test_printf("    usb_lld_init_rtt() ok\r\n");

    test_printf("    waiting USB configured (host cable + enumerate)...\r\n");
    if (!wait_usb_ready(20000U)) {
        test_printf("    timeout — connect USB host and retry\r\n");
        TEST_FAIL("USB not configured in time");
        return;
    }
    test_printf("    USB configured (cherry_configured)\r\n");

    AP_HAL::UARTDriver *const s0 = hal.serial(0);
    TEST_ASSERT(s0 != nullptr, "serial(0) USB CDC driver");

    s0->begin(921600);
    test_printf("    serial(0): begin(921600) ok\r\n");

    uint8_t hb_buf[MAVLINK_MAX_PACKET_LEN];
    const uint16_t hb_len = pack_heartbeat(hb_buf, sizeof(hb_buf));
    test_printf("    mavlink heartbeat packed len=%u sys=%u comp=%u\r\n",
                (unsigned)hb_len,
                (unsigned)k_mav_sys_id,
                (unsigned)k_mav_comp_id);
    TEST_ASSERT(hb_len >= 12 && hb_len <= sizeof(hb_buf), "heartbeat frame size");

    const uint32_t n = s0->write(hb_buf, hb_len);
    test_printf("    serial(0): heartbeat write returned %lu bytes\r\n", (unsigned long)n);
    TEST_ASSERT(n == hb_len, "heartbeat write byte count");

    pump_usb_tx(s0, 80);

    s0->printf("S_mavlink_usb CDC MAVLink smoke\r\n");
    pump_usb_tx(s0, 40);

    test_printf("    host: ACM 1209:5741 @921600 pymavlink wait_heartbeat\r\n");
    test_printf("    note: UART7=test_runner; periodic HEARTBEAT in main loop\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("S_MAVLINK_USB");

    step_mavlink_usb_smoke();

    uint32_t hb_ms = 0;
    uint32_t beacon_ms = 0;
    uint8_t hb_buf[MAVLINK_MAX_PACKET_LEN];

    while (1) {
        usb_lld_poll_rtt();
        AP_HAL::UARTDriver *const s0 = hal.serial(0);
        if (s0 != nullptr && usb_lld_is_configured_rtt()) {
            pump_usb_tx(s0, 1);

            hb_ms += 5;
            if (hb_ms >= 1000U) {
                hb_ms = 0;
                const uint16_t hb_len = pack_heartbeat(hb_buf, sizeof(hb_buf));
                if (hb_len > 0) {
                    s0->write(hb_buf, hb_len);
                    pump_usb_tx(s0, 8);
                }
            }

            beacon_ms += 5;
            if (beacon_ms >= 3000U) {
                beacon_ms = 0;
                s0->printf("S_mavlink_usb CDC beacon\r\n");
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
