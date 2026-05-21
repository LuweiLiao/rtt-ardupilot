/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include <stdbool.h>

/*
  initialise microSD card if available. Called from AP_BoardConfig
  during initialisation (via the SD card slowdown parameter path).
 */
bool sdcard_init(void);

/*
  stop sdcard interface (for reboot / remount with different speed)
 */
void sdcard_stop(void);

/*
  retry sdcard init — called periodically from the main loop when
  the SD card was not previously mounted
 */
bool sdcard_retry(void);
