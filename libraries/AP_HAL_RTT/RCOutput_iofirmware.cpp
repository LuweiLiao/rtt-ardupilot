/*
 * AP_HAL_RTT — IOMCU DShot thread placement (matrix parity with ChibiOS).
 *
 * ChibiOS: libraries/AP_HAL_ChibiOS/RCOutput_iofirmware.cpp — built for IOMCU_FW
 * with HAL_DSHOT_ENABLED; creates dshot thread and timer_tick setup.
 *
 * RTT application firmware (CUAV v5):
 *   - IOMCU protocol, ROMFS upload, and DShot forwarding live in RCOutput.cpp,
 *     RCOutput_serial.cpp, and AP_IOMCU — not a separate translation unit.
 *
 * RTT IOMCU coprocessor image (IOMCU_FW) is not ported in this tree; when added,
 * port the ChibiOS RCOutput_iofirmware.cpp body here under IOMCU_FW guards.
 */

#include "RCOutput.h"

#if defined(IOMCU_FW) && HAL_DSHOT_ENABLED
#error "RTT IOMCU_FW DShot: port RCOutput_iofirmware from ChibiOS when building iofirmware"
#endif
