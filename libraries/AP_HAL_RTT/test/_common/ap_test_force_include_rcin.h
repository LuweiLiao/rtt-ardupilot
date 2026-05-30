/* Force-include for D_rcinput HAL smoke (RCInput API; no full RCProtocol link set). */
#include <ap_config.h>
#ifdef __cplusplus
#include <AP_Math/AP_Math.h>
#endif
#include "ap_strings_shim.h"

#undef HAL_WITH_IO_MCU
#define HAL_WITH_IO_MCU 0
#undef HAL_LOGGING_ENABLED
#define HAL_LOGGING_ENABLED 0

#undef AP_RCPROTOCOL_ENABLED
#define AP_RCPROTOCOL_ENABLED 0
