/* Single force-include for D_uart_hal HAL smoke (ap_config + libc shims). */
#include <ap_config.h>
#ifdef __cplusplus
#include <AP_Math/AP_Math.h>
#endif
#include "ap_strings_shim.h"

/* Overrides after hwdef.h (must follow #include ap_config.h). */
#undef HAL_WITH_IO_MCU
#define HAL_WITH_IO_MCU 0
#undef HAL_LOGGING_ENABLED
#define HAL_LOGGING_ENABLED 0
#undef AP_RCPROTOCOL_ENABLED
#define AP_RCPROTOCOL_ENABLED 0
