#ifndef _AP_CONFIG_H_
#define _AP_CONFIG_H_

#include "hwdef.h"

#ifdef HAL_NUM_CAN_IFACES
#  undef HAL_NUM_CAN_IFACES
#endif
#define HAL_NUM_CAN_IFACES 0

#endif
