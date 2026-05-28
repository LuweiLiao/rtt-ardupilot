/**
 * test_app_descriptor.c — ArduPilot bootloader app descriptor for module tests.
 *
 * Standalone tests skip ArduPilot link, so AP_CheckFirmwareDefine.h is not pulled in.
 * The PX4-style bootloader still scans flash at 0x08008000 for the unsigned descriptor
 * signature before jumping to the application.
 */

#include <stdint.h>

#if defined(__has_include)
#if __has_include("hwdef.h")
#include "hwdef.h"
#endif
#endif

#ifndef APJ_BOARD_ID
#define APJ_BOARD_ID 50U
#endif

#ifndef APP_FW_MAJOR
#define APP_FW_MAJOR 0U
#endif

#ifndef APP_FW_MINOR
#define APP_FW_MINOR 0U
#endif

#define AP_APP_DESCRIPTOR_SIGNATURE_UNSIGNED \
    { 0x40, 0xa2, 0xe4, 0xf1, 0x64, 0x68, 0x91, 0x06 }

typedef struct {
    uint8_t sig[8];
    uint32_t image_crc1;
    uint32_t image_crc2;
    uint32_t image_size;
    uint32_t git_hash;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t board_id;
    uint8_t reserved[8];
} test_app_descriptor_t;

const test_app_descriptor_t app_descriptor __attribute__((section(".app_descriptor"))) = {
    .sig = AP_APP_DESCRIPTOR_SIGNATURE_UNSIGNED,
    .image_crc1 = 0,
    .image_crc2 = 0,
    .image_size = 0,
    .git_hash = 0,
    .version_major = APP_FW_MAJOR,
    .version_minor = APP_FW_MINOR,
    .board_id = APJ_BOARD_ID,
    .reserved = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
};
