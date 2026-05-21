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
 * DShot serial command output.
 *
 * Reference: libraries/AP_HAL_ChibiOS/RCOutput_serial.cpp
 *
 * RTT implementation notes (ADR-NNN):
 *   - Low-level DMA pulse generation (ChibiOS pwm_group + DMAR) not yet
 *     available on RTT.  DShot commands are queued and forwarded to IOMCU
 *     when iomcu_dshot is active.
 *   - dshot_send_command() is a stub returning false until the underlying
 *     TIM/DMA infrastructure is ported.
 *   - All other functions (send_dshot_command, set_reversed_mask,
 *     set_reversible_mask, update_channel_masks) are functionally
 *     equivalent to ChibiOS.
 */

#include "RCOutput.h"
#include <AP_Math/AP_Math.h>
#include <AP_BoardConfig/AP_BoardConfig.h>
#include <AP_InternalError/AP_InternalError.h>
#include <AP_Vehicle/AP_Vehicle_Type.h>

#if HAL_DSHOT_ENABLED

#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
extern AP_IOMCU iomcu;
#endif

using namespace RTT;

extern const AP_HAL::HAL& hal;

/*
  RTT does not implement the low-level dshot_send_command().
  The ChibiOS version (RCOutput_serial.cpp L38-92) uses pwm_group with
  DMA pulse generation (DMAR buffer, send_pulses_DMAR) which requires
  timer-group infrastructure not yet available in RTT.
  DShot commands are forwarded to IOMCU or queued for later send.

  Send a dshot command, if command timeout is 0 then 10 commands are sent.
  chan is the servo channel to send the command to.

  Reference: ChibiOS RCOutput_serial.cpp L96-132
 */
void RCOutput::send_dshot_command(uint8_t command, uint8_t chan,
                                  uint32_t command_timeout_ms,
                                  uint16_t repeat_count,
                                  bool priority)
{
    // once armed only priority commands will be accepted
    if (hal.util->get_soft_armed() && !priority) {
        return;
    }

    // not an FMU channel: route through IOMCU
    if (chan < chan_offset || chan == ALL_CHANNELS) {
#if HAL_WITH_IO_MCU
        if (iomcu_dshot) {
            iomcu.send_dshot_command(command, chan, command_timeout_ms,
                                     repeat_count, priority);
        }
#endif
        if (chan != ALL_CHANNELS) {
            return;
        }
    }

    // build the command packet
    DshotCommandPacket pkt;
    pkt.command = command;
    if (chan != ALL_CHANNELS) {
        pkt.chan = chan - chan_offset;  // normalize to FMU channel
    } else {
        pkt.chan = ALL_CHANNELS;
    }

    if (command_timeout_ms == 0) {
        pkt.cycle = MAX(10, repeat_count);
    } else {
        pkt.cycle = MAX(command_timeout_ms * 1000UL / _dshot_period_us,
                        repeat_count);
    }

    // prioritise anything that is not an LED or BEEP command
    if (!_dshot_command_queue.push(pkt) && priority) {
        _dshot_command_queue.push_force(pkt);
    }
}

/*
  Set the dshot outputs that should be reversed (as opposed to 3D).
  The chanmask passed is added (ORed) into any existing mask.
  The mask uses servo channel numbering.

  Reference: ChibiOS RCOutput_serial.cpp L137-139
 */
void RCOutput::set_reversed_mask(uint32_t chanmask)
{
    _reversed_mask |= chanmask;
}

/*
  Set the dshot outputs that should be reversible/3D.
  The chanmask passed is added (ORed) into any existing mask.
  The mask uses servo channel numbering.

  Reference: ChibiOS RCOutput_serial.cpp L144-152
 */
void RCOutput::set_reversible_mask(uint32_t chanmask)
{
    _reversible_mask |= chanmask;
#if HAL_WITH_IO_MCU
    const uint32_t iomcu_mask = ((1U << chan_offset) - 1);
    if (iomcu_dshot && (chanmask & iomcu_mask)) {
        iomcu.set_reversible_mask(chanmask & iomcu_mask);
    }
#endif
}

/*
  Update the dshot outputs that should be reversible/3D at 1Hz.

  Reference: ChibiOS RCOutput_serial.cpp L154-186
 */
void RCOutput::update_channel_masks()
{
    // post arming dshot commands will not be accepted
    if (hal.util->get_soft_armed() || _disable_channel_mask_updates) {
        return;
    }

    // The masks use servo channel numbering
    for (uint8_t i = 0; i < RTT_RCOUT_MAX_CHANNELS; i++) {
        switch (_dshot_esc_type) {
        case DSHOT_ESC_BLHELI:
        case DSHOT_ESC_BLHELI_S:
        case DSHOT_ESC_BLHELI_EDT:
        case DSHOT_ESC_BLHELI_EDT_S:
            if (_reversible_mask & (1UL << i)) {
                send_dshot_command(DSHOT_3D_ON, i, 0, 10, true);
            }
            if (_reversed_mask & (1UL << i)) {
                send_dshot_command(DSHOT_REVERSE, i, 0, 10, true);
            }
            break;
        default:
            break;
        }
    }

    if (_dshot_esc_type == DSHOT_ESC_BLHELI_EDT ||
        _dshot_esc_type == DSHOT_ESC_BLHELI_EDT_S) {
        send_dshot_command(DSHOT_EXTENDED_TELEMETRY_ENABLE,
                           ALL_CHANNELS, 0, 10, true);
    }
}

/*
  Set the dshot rate as a multiple of the loop rate.

  Reference: ChibiOS RCOutput.cpp L522-552
 */
void RCOutput::set_dshot_rate(uint8_t dshot_rate, uint16_t loop_rate_hz)
{
    uint32_t drate = dshot_rate * loop_rate_hz;

    // never allow rates below 800 Hz
    while (drate < 800) {
        dshot_rate++;
        drate = dshot_rate * loop_rate_hz;
    }
    // prevent stupidly high rate multiples
    while (dshot_rate > 1 && drate > MAX(4096U, loop_rate_hz)) {
        dshot_rate--;
        drate = dshot_rate * loop_rate_hz;
    }

    _dshot_rate = dshot_rate;
    _dshot_period_us = 1000000UL / drate;

#if HAL_WITH_IO_MCU
    if (iomcu_dshot) {
        iomcu.set_dshot_period(_dshot_period_us, _dshot_rate);
    }
#endif
}

#endif  // HAL_DSHOT_ENABLED
