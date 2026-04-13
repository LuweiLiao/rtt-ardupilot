/*
 * Force-included header for ArduPilot on RT-Thread.
 * This is passed via -include to all ArduPilot source files.
 * It provides hwdef.h defines (sensor probe lists, board config, etc.)
 * without pulling in any system headers.
 */
#pragma once

#include "hwdef.h"
