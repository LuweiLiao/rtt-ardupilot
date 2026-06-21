/*
   GCS MAVLink functions related to parameter handling

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "GCS_config.h"

#if HAL_GCS_ENABLED

#include <AP_HAL/AP_HAL.h>

#include "GCS.h"
#include <AP_Logger/AP_Logger.h>
#include <AP_BoardConfig/AP_BoardConfig.h>

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#include <AP_HAL_RTT/Storage.h>
#endif

extern const AP_HAL::HAL& hal;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#define RTT_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
volatile uint32_t rtt_dbg_gcs_param_queued_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_queued_empty RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_async_sent_total RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_stream_sent_total RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_async_sent RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_stream_sent RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_count_initial RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_count_after_async RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_bytes_allowed RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_txspace RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_link_bw RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_time_breaks RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_txbuf_breaks RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_completed RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_index RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_quantum_caps RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_quantum_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_broadcast_deferred RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_time_budget_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_zero_stream_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_zero_due_async_cover RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_zero_due_count_zero RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_zero_due_txbuf RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_stream_before_index RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_stream_after_index RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_stream_elapsed_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_max_stream_elapsed_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_zero_reason RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_call_gap_max_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_call_gap_last_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_call_gap_large_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_call_gap_last_index RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_send_gap_max_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_send_gap_last_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_send_gap_large_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_send_gap_before_index RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_send_gap_after_index RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_call_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_last_send_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_request_list_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_active_until_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_active_window_opened RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_active_window_closed RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_delay_pump_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_request_read_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_request_push_ok RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_request_push_fail RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_io_timer_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_io_no_reply_space RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_io_pop_ok RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_io_pop_empty RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_io_find_ok RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_io_find_fail RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_reply_push_ok RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_gcs_param_reply_push_fail RTT_DBG_DTCM_BSS;
#endif

// queue of pending parameter requests and replies
// Use thread-safe ObjectBuffer_TS because param_io_timer runs in a
// separate IO thread on RT-Thread (and ChibiOS), racing with pushes
// from the main thread in handle_param_request_read.
ObjectBuffer_TS<GCS_MAVLINK::pending_param_request> GCS_MAVLINK::param_requests(20);
ObjectBuffer_TS<GCS_MAVLINK::pending_param_reply> GCS_MAVLINK::param_replies(5);

bool GCS_MAVLINK::param_timer_registered;

/**
 * @brief Send the next pending parameter, called from deferred message
 * handling code
 */
void
GCS_MAVLINK::queued_param_send()
{
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_queued_calls++;
    rtt_dbg_gcs_param_last_stream_sent = 0;
    const uint32_t rtt_param_call_ms = AP_HAL::millis();
    if (rtt_dbg_gcs_param_last_call_ms != 0U) {
        const uint32_t call_gap_ms = rtt_param_call_ms - rtt_dbg_gcs_param_last_call_ms;
        rtt_dbg_gcs_param_call_gap_last_ms = call_gap_ms;
        rtt_dbg_gcs_param_call_gap_last_index = _queued_parameter_index;
        if (call_gap_ms > rtt_dbg_gcs_param_call_gap_max_ms) {
            rtt_dbg_gcs_param_call_gap_max_ms = call_gap_ms;
        }
        if (call_gap_ms > 250U && _queued_parameter != nullptr) {
            rtt_dbg_gcs_param_call_gap_large_count++;
        }
    }
    rtt_dbg_gcs_param_last_call_ms = rtt_param_call_ms;
#endif
    // send parameter async replies
    uint8_t async_replies_sent_count = send_parameter_async_replies();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_last_async_sent = async_replies_sent_count;
    rtt_dbg_gcs_param_async_sent_total += async_replies_sent_count;
#endif

    // now send the streaming parameters (from PARAM_REQUEST_LIST)
    if (_queued_parameter == nullptr) {
        // .... or not....
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_queued_empty++;
        if (chan == MAVLINK_COMM_0 && rtt_dbg_gcs_param_active_until_ms != 0U) {
            rtt_dbg_gcs_param_active_until_ms = 0U;
            rtt_dbg_gcs_param_active_window_closed++;
        }
#endif
        return;
    }

    const uint32_t tnow = AP_HAL::millis();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    if (chan == MAVLINK_COMM_0) {
        /*
         * [Cybernetics Ch.4] Closed-loop: only keep the RTT delay-callback
         * MAVLink producer assist alive while a real USB PARAM list transfer
         * is in progress.  This gives ChibiOS-like producer continuity without
         * permanently stealing startup/INS time from sensor and EKF work.
         */
        rtt_dbg_gcs_param_active_until_ms = tnow + 30000U;
    }
#endif
    const uint32_t tstart = AP_HAL::micros();

    // use at most 30% of bandwidth on parameters
    const uint32_t link_bw = _port->bw_in_bytes_per_second();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_last_link_bw = link_bw;
#endif

    uint32_t param_bw_divisor = 3333;
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    if (chan == MAVLINK_COMM_0) {
        // [Cybernetics Ch.15] Extremum seeking: raise USB parameter share while retaining txspace feedback.
        param_bw_divisor = 2000;
    }
#endif
    uint32_t bytes_allowed = link_bw * (tnow - _queued_parameter_send_time_ms) / param_bw_divisor;
    const uint16_t size_for_one_param_value_msg = MAVLINK_MSG_ID_PARAM_VALUE_LEN + packet_overhead();
    if (bytes_allowed < size_for_one_param_value_msg) {
        bytes_allowed = size_for_one_param_value_msg;
    }
    const uint32_t txspace_bytes = txspace();
    if (bytes_allowed > txspace_bytes) {
        bytes_allowed = txspace_bytes;
    }
    uint32_t count = bytes_allowed / size_for_one_param_value_msg;

    // when we don't have flow control we really need to keep the
    // param download very slow, or it tends to stall
    if (!have_flow_control() && count > 5) {
        count = 5;
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    if (chan == MAVLINK_COMM_0 && count > 4) {
        /*
         * [Cybernetics Ch.4] Closed-loop time slicing: RTT/CherryUSB can now
         * report a short queue, but a single PARAM_VALUE burst can still occupy
         * the GCS send window long enough for live sensor/status streams to miss
         * their 5Hz slots.  Keep parameter throughput via a shorter scheduler
         * interval, but bound each parameter quantum.
         */
        count = 4;
        rtt_dbg_gcs_param_quantum_caps++;
    }
    rtt_dbg_gcs_param_last_quantum_count = count;
#endif
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_last_bytes_allowed = bytes_allowed;
    rtt_dbg_gcs_param_last_txspace = txspace_bytes;
    rtt_dbg_gcs_param_last_count_initial = count;
#endif
    if (async_replies_sent_count >= count) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        if (_queued_parameter != nullptr) {
            rtt_dbg_gcs_param_zero_stream_calls++;
            rtt_dbg_gcs_param_zero_due_async_cover++;
            rtt_dbg_gcs_param_last_zero_reason = 1;
            rtt_dbg_gcs_param_last_count_after_async = 0;
            rtt_dbg_gcs_param_last_stream_before_index = _queued_parameter_index;
            rtt_dbg_gcs_param_last_stream_after_index = _queued_parameter_index;
            rtt_dbg_gcs_param_last_stream_elapsed_us = AP_HAL::micros() - tstart;
        }
#endif
        return;
    }
    count -= async_replies_sent_count;
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_last_count_after_async = count;
#endif

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    /*
     * [Cybernetics Ch.4] Closed-loop: DWC2/CherryUSB accounting is now clean,
     * but stress runs still show PARAM_VALUE bursts breaking on the 1 ms CPU
     * budget, producing 20s-class downloads while USB bytes remain conserved.
     * ChibiOS can usually finish the same MAVLink quantum inside 1 ms because
     * SerialUSB only queues stable buffers here; RTT's path also services the
     * CherryUSB ring/flush feedback.  Give only the USB main channel a slightly
     * wider per-quantum CPU budget, while keeping txspace and the 4-message
     * quantum cap as the backpressure guard.
     */
    const uint32_t param_time_budget_us = (chan == MAVLINK_COMM_0) ? 2500U : 1000U;
    rtt_dbg_gcs_param_last_time_budget_us = param_time_budget_us;
#else
    const uint32_t param_time_budget_us = 1000U;
#endif

    uint32_t stream_sent = 0;
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    const uint32_t rtt_stream_start_index = _queued_parameter_index;
#endif
    while (count && _queued_parameter != nullptr && last_txbuf_is_greater(33)) {
        char param_name[AP_MAX_NAME_SIZE];
        _queued_parameter->copy_name_token(_queued_parameter_token, param_name, sizeof(param_name), true);

        mavlink_msg_param_value_send(
            chan,
            param_name,
            _queued_parameter->cast_to_float(_queued_parameter_type),
            mav_param_type(_queued_parameter_type),
            _queued_parameter_count,
            _queued_parameter_index);

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        const uint32_t rtt_param_send_ms = AP_HAL::millis();
        if (rtt_dbg_gcs_param_last_send_ms != 0U) {
            const uint32_t send_gap_ms = rtt_param_send_ms - rtt_dbg_gcs_param_last_send_ms;
            rtt_dbg_gcs_param_send_gap_last_ms = send_gap_ms;
            if (send_gap_ms > rtt_dbg_gcs_param_send_gap_max_ms) {
                rtt_dbg_gcs_param_send_gap_max_ms = send_gap_ms;
            }
            if (send_gap_ms > 250U) {
                rtt_dbg_gcs_param_send_gap_large_count++;
                rtt_dbg_gcs_param_send_gap_before_index = _queued_parameter_index - 1U;
                rtt_dbg_gcs_param_send_gap_after_index = _queued_parameter_index;
            }
        }
        rtt_dbg_gcs_param_last_send_ms = rtt_param_send_ms;
#endif

        _queued_parameter = AP_Param::next_scalar(&_queued_parameter_token, &_queued_parameter_type);
        _queued_parameter_index++;
        stream_sent++;

        if (AP_HAL::micros() - tstart > param_time_budget_us) {
            // don't use more than 1ms sending blocks of parameters
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            rtt_dbg_gcs_param_time_breaks++;
#endif
            break;
        }
        count--;
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    if (count != 0 && _queued_parameter != nullptr && !last_txbuf_is_greater(33)) {
        rtt_dbg_gcs_param_txbuf_breaks++;
    }
    if (_queued_parameter == nullptr) {
        rtt_dbg_gcs_param_completed++;
        if (chan == MAVLINK_COMM_0 && rtt_dbg_gcs_param_active_until_ms != 0U) {
            rtt_dbg_gcs_param_active_until_ms = 0U;
            rtt_dbg_gcs_param_active_window_closed++;
        }
    }
    const uint32_t rtt_stream_elapsed_us = AP_HAL::micros() - tstart;
    rtt_dbg_gcs_param_last_stream_before_index = rtt_stream_start_index;
    rtt_dbg_gcs_param_last_stream_after_index = _queued_parameter_index;
    rtt_dbg_gcs_param_last_stream_elapsed_us = rtt_stream_elapsed_us;
    if (rtt_stream_elapsed_us > rtt_dbg_gcs_param_max_stream_elapsed_us) {
        rtt_dbg_gcs_param_max_stream_elapsed_us = rtt_stream_elapsed_us;
    }
    if (stream_sent == 0 && _queued_parameter != nullptr) {
        rtt_dbg_gcs_param_zero_stream_calls++;
        if (async_replies_sent_count >= rtt_dbg_gcs_param_last_count_initial) {
            rtt_dbg_gcs_param_zero_due_async_cover++;
            rtt_dbg_gcs_param_last_zero_reason = 1;
        } else if (rtt_dbg_gcs_param_last_count_after_async == 0) {
            rtt_dbg_gcs_param_zero_due_count_zero++;
            rtt_dbg_gcs_param_last_zero_reason = 2;
        } else if (!last_txbuf_is_greater(33)) {
            rtt_dbg_gcs_param_zero_due_txbuf++;
            rtt_dbg_gcs_param_last_zero_reason = 3;
        } else {
            rtt_dbg_gcs_param_last_zero_reason = 4;
        }
    } else {
        rtt_dbg_gcs_param_last_zero_reason = 0;
    }
    rtt_dbg_gcs_param_last_stream_sent = stream_sent;
    rtt_dbg_gcs_param_stream_sent_total += stream_sent;
    rtt_dbg_gcs_param_last_index = _queued_parameter_index;
#endif
    _queued_parameter_send_time_ms = tnow;
}

/*
  return true if a channel has flow control
 */
bool GCS_MAVLINK::have_flow_control(void)
{
    if (_port == nullptr) {
        return false;
    }

    if (_port->flow_control_enabled()) {
        return true;
    }

    if (chan == MAVLINK_COMM_0) {
        // assume USB console has flow control
        return hal.gpio->usb_connected();
    }

    return false;
}


/*
  handle a request to change stream rate. Note that copter passes in
  save==false so we don't want the save to happen when the user connects the
  ground station.
 */
void GCS_MAVLINK::handle_request_data_stream(const mavlink_message_t &msg)
{
    mavlink_request_data_stream_t packet;
    mavlink_msg_request_data_stream_decode(&msg, &packet);

    int16_t freq = 0;     // packet frequency

    if (packet.start_stop == 0)
        freq = 0;                     // stop sending
    else if (packet.start_stop == 1)
        freq = packet.req_message_rate;                     // start sending
    else
        return;

    // if stream_id is still NUM_STREAMS at the end of this switch
    // block then either we set stream rates for all streams, or we
    // were asked to set the streamrate for an unrecognised stream
    streams stream_id = NUM_STREAMS;
    switch (packet.req_stream_id) {
    case MAV_DATA_STREAM_ALL:
        for (uint8_t i=0; i<NUM_STREAMS; i++) {
            if (i == STREAM_PARAMS) {
                // don't touch parameter streaming rate; it is
                // considered "internal".
                continue;
            }
            if (persist_streamrates()) {
                streamRates[i].set_and_save_ifchanged(freq);
            } else {
                streamRates[i].set(freq);
            }
            initialise_message_intervals_for_stream((streams)i);
        }
        break;
    case MAV_DATA_STREAM_RAW_SENSORS:
        stream_id = STREAM_RAW_SENSORS;
        break;
    case MAV_DATA_STREAM_EXTENDED_STATUS:
        stream_id = STREAM_EXTENDED_STATUS;
        break;
    case MAV_DATA_STREAM_RC_CHANNELS:
        stream_id = STREAM_RC_CHANNELS;
        break;
    case MAV_DATA_STREAM_RAW_CONTROLLER:
        stream_id = STREAM_RAW_CONTROLLER;
        break;
    case MAV_DATA_STREAM_POSITION:
        stream_id = STREAM_POSITION;
        break;
    case MAV_DATA_STREAM_EXTRA1:
        stream_id = STREAM_EXTRA1;
        break;
    case MAV_DATA_STREAM_EXTRA2:
        stream_id = STREAM_EXTRA2;
        break;
    case MAV_DATA_STREAM_EXTRA3:
        stream_id = STREAM_EXTRA3;
        break;
    }

    if (stream_id == NUM_STREAMS) {
        // asked to set rate on unknown stream (or all were set already)
        return;
    }

    AP_Int16 *rate = &streamRates[stream_id];

    if (rate != nullptr) {
        if (persist_streamrates()) {
            rate->set_and_save_ifchanged(freq);
        } else {
            rate->set(freq);
        }
        initialise_message_intervals_for_stream(stream_id);
    }
}

void GCS_MAVLINK::handle_param_request_list(const mavlink_message_t &msg)
{
    if (!params_ready()) {
        return;
    }

    mavlink_param_request_list_t packet;
    mavlink_msg_param_request_list_decode(&msg, &packet);

    // requesting parameters is a convenient way to get extra information
    send_banner();

    // Start sending parameters - next call to ::update will kick the first one out
    _queued_parameter = AP_Param::first(&_queued_parameter_token, &_queued_parameter_type);
    _queued_parameter_index = 0;
    _queued_parameter_count = AP_Param::count_parameters();
    _queued_parameter_send_time_ms = AP_HAL::millis(); // avoid initial flooding
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    /*
     * [Cybernetics Ch.4] Closed-loop: PARAM gap diagnostics must measure
     * gaps inside one active PARAM_REQUEST_LIST, not the intentional idle time
     * between two host download rounds.
     */
    rtt_dbg_gcs_param_request_list_count++;
    rtt_dbg_gcs_param_last_call_ms = 0;
    rtt_dbg_gcs_param_last_send_ms = 0;
    if (chan == MAVLINK_COMM_0) {
        rtt_dbg_gcs_param_active_until_ms = AP_HAL::millis() + 30000U;
        rtt_dbg_gcs_param_active_window_opened++;
    }
#endif

    // Ensure MSG_NEXT_PARAM is scheduled even if _PARAMS stream rate is 0
    send_message(MSG_NEXT_PARAM);
}

void GCS_MAVLINK::handle_param_request_read(const mavlink_message_t &msg)
{
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_request_read_count++;
#endif
    if (param_requests.space() == 0) {
        // we can't process this right now, drop it
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_request_push_fail++;
#endif
        return;
    }
    
    mavlink_param_request_read_t packet;
    mavlink_msg_param_request_read_decode(&msg, &packet);

    /*
      we reserve some space for sending parameters if the client ever
      fails to get a parameter due to lack of space
     */
    uint32_t saved_reserve_param_space_start_ms = reserve_param_space_start_ms;
    reserve_param_space_start_ms = 0; // bypass packet_overhead_chan reservation checking
    if (!HAVE_PAYLOAD_SPACE(chan, PARAM_VALUE)) {
        reserve_param_space_start_ms = AP_HAL::millis();
    } else {
        reserve_param_space_start_ms = saved_reserve_param_space_start_ms;
    }

    struct pending_param_request req;
    req.chan = chan;
    req.param_index = packet.param_index;
    memcpy(req.param_name, packet.param_id, MIN(sizeof(packet.param_id), sizeof(req.param_name)));
    req.param_name[AP_MAX_NAME_SIZE] = 0;

    struct pending_param_reply reply {};
    AP_Param *vp = nullptr;
    if (req.param_index != -1) {
        AP_Param::ParamToken token {};
        vp = AP_Param::find_by_index(req.param_index, &reply.p_type, &token);
        if (vp != nullptr) {
            vp->copy_name_token(token, reply.param_name, AP_MAX_NAME_SIZE, true);
        }
    } else {
        strncpy(reply.param_name, req.param_name, AP_MAX_NAME_SIZE+1);
        vp = AP_Param::find(req.param_name, &reply.p_type);
    }

    if (vp == nullptr) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_io_find_fail++;
#endif
        return;
    }

    reply.chan = req.chan;
    reply.param_name[AP_MAX_NAME_SIZE] = 0;
    reply.value = vp->cast_to_float(reply.p_type);
    reply.param_index = req.param_index;
    reply.count = AP_Param::count_parameters();

    /*
     * [Cybernetics Ch.4] Closed-loop: RTT's low-priority IO thread can be
     * starved while the 400Hz main loop is already close to saturation.  A
     * single PARAM_REQUEST_READ lookup is small and must answer promptly for
     * GCS compatibility, so enqueue the reply synchronously and let the normal
     * deferred MAVLink sender transmit it.
     */
    if (param_replies.push(reply)) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_io_find_ok++;
        rtt_dbg_gcs_param_reply_push_ok++;
#endif
        send_message(MSG_NEXT_PARAM);
    } else {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_reply_push_fail++;
#endif
    }
}

void GCS_MAVLINK::handle_param_set(const mavlink_message_t &msg)
{
    mavlink_param_set_t packet;
    mavlink_msg_param_set_decode(&msg, &packet);
    enum ap_var_type var_type;

    // set parameter
    AP_Param *vp;
    char key[AP_MAX_NAME_SIZE+1];
    strncpy(key, (char *)packet.param_id, AP_MAX_NAME_SIZE);
    key[AP_MAX_NAME_SIZE] = 0;

    // find existing param so we can get the old value
    uint16_t parameter_flags = 0;
    vp = AP_Param::find(key, &var_type, &parameter_flags);
    if (vp == nullptr || isnan(packet.param_value) || isinf(packet.param_value)) {
        return;
    }

    float old_value = vp->cast_to_float(var_type);

    if (!vp->allow_set_via_mavlink(parameter_flags)) {
        // don't warn the user about this failure if we are dropping
        // messages here.  This is on the assumption that scripting is
        // currently responsible for setting parameters and may set
        // the value instead of us.
        if (gcs().get_allow_param_set()) {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "Param write denied (%s)", key);
        }
        // send the readonly value
        send_parameter_value(key, var_type, old_value);
        return;
    }

    // set the value
    vp->set_float(packet.param_value, var_type);

    /*
      we force the save if the value is not equal to the old
      value. This copes with the use of override values in
      constructors, such as PID elements. Otherwise a set to the
      default value which differs from the constructor value doesn't
      save the change
     */
    bool force_save = !is_equal(packet.param_value, old_value);

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    /*
     * [Cybernetics Ch.4] Closed-loop: RTT USB can acknowledge PARAM_SET much
     * faster than the IO/storage threads can make it durable. Keep full
     * parameter download asynchronous, but make single writes durable before
     * confirming them on the same link.
     */
    vp->save_sync(force_save, false);
    bool storage_flush_ok = true;
    if (hal.storage != nullptr) {
        auto *rtt_storage = static_cast<RTT::Storage *>(hal.storage);
        storage_flush_ok = rtt_storage->flush(200U);
    }
#else
    // save the change (async via IO thread — same as ChibiOS)
    vp->save(force_save);
#endif

    if (force_save && (parameter_flags & AP_PARAM_FLAG_ENABLE)) {
        AP_Param::invalidate_count();
    }

    // [Cybernetics Ch.4] Closed-loop: confirm PARAM_SET on the same link immediately.
    send_parameter_value(key, var_type, vp->cast_to_float(var_type));

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    if (!storage_flush_ok) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "Param storage flush timeout (%s)", key);
    }
#endif

#if HAL_LOGGING_ENABLED
    AP_Logger *logger = AP_Logger::get_singleton();
    if (logger != nullptr) {
        logger->Write_Parameter(key, vp->cast_to_float(var_type));
    }
#endif
}

void GCS_MAVLINK::send_parameter_value(const char *param_name, ap_var_type param_type, float param_value)
{
    if (!HAVE_PAYLOAD_SPACE(chan, PARAM_VALUE)) {
        return;
    }
    mavlink_msg_param_value_send(
        chan,
        param_name,
        param_value,
        mav_param_type(param_type),
        AP_Param::count_parameters(),
        -1);
}

/*
  send a parameter value message to all active MAVLink connections
 */
void GCS::send_parameter_value(const char *param_name, ap_var_type param_type, float param_value)
{
    mavlink_param_value_t packet{};
    const uint8_t to_copy = MIN(ARRAY_SIZE(packet.param_id), strlen(param_name));
    memcpy(packet.param_id, param_name, to_copy);
    packet.param_value = param_value;
    packet.param_type = GCS_MAVLINK::mav_param_type(param_type);
    packet.param_count = AP_Param::count_parameters();
    packet.param_index = -1;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    const mavlink_msg_entry_t *entry = mavlink_get_msg_entry(MAVLINK_MSG_ID_PARAM_VALUE);
    if (entry == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < num_gcs(); i++) {
        GCS_MAVLINK &c = *chan(i);
        if (c.is_private() || !c.is_active()) {
            continue;
        }
#if HAL_HIGH_LATENCY2_ENABLED
        if (c.is_high_latency_link) {
            continue;
        }
#endif
        /*
         * [Cybernetics Ch.4] Closed-loop: on RTT USB, a full PARAM_REQUEST_LIST
         * owns the PARAM_VALUE stream.  Do not interleave index=-1 parameter
         * broadcasts such as STAT_RUNTIME into the same CDC backlog; they are
         * refreshed after the list transfer and otherwise disturb ordering and
         * live telemetry on the short USB queue.
         */
        if (c.get_chan() == MAVLINK_COMM_0 && c._queued_parameter != nullptr) {
            rtt_dbg_gcs_param_broadcast_deferred++;
            continue;
        }
        c.send_message((const char *)&packet, entry);
    }
#else
    gcs().send_to_active_channels(MAVLINK_MSG_ID_PARAM_VALUE,
                                  (const char *)&packet);
#endif

#if HAL_LOGGING_ENABLED
    // also log to AP_Logger
    AP_Logger *logger = AP_Logger::get_singleton();
    if (logger != nullptr) {
        logger->Write_Parameter(param_name, param_value);
    }
#endif
}


/*
  timer callback for async parameter requests
 */
void GCS_MAVLINK::param_io_timer(void)
{
    struct pending_param_request req;
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_io_timer_calls++;
#endif

    // this is mostly a no-op, but doing this here means we won't
    // block the main thread counting parameters (~30ms on PH)
    AP_Param::count_parameters();

    if (param_replies.space() == 0) {
        // no room
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_io_no_reply_space++;
#endif
        return;
    }
    
    if (!param_requests.pop(req)) {
        // nothing to do
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_io_pop_empty++;
#endif
        return;
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_io_pop_ok++;
#endif

    struct pending_param_reply reply;
    AP_Param *vp;

    if (req.param_index != -1) {
        AP_Param::ParamToken token {};
        vp = AP_Param::find_by_index(req.param_index, &reply.p_type, &token);
        if (vp == nullptr) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            rtt_dbg_gcs_param_io_find_fail++;
#endif
            return;
        }
        vp->copy_name_token(token, reply.param_name, AP_MAX_NAME_SIZE, true);
    } else {
        strncpy(reply.param_name, req.param_name, AP_MAX_NAME_SIZE+1);
        vp = AP_Param::find(req.param_name, &reply.p_type);
        if (vp == nullptr) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            rtt_dbg_gcs_param_io_find_fail++;
#endif
            return;
        }
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_gcs_param_io_find_ok++;
#endif

    reply.chan = req.chan;
    reply.param_name[AP_MAX_NAME_SIZE] = 0;
    reply.value = vp->cast_to_float(reply.p_type);
    reply.param_index = req.param_index;
    reply.count = AP_Param::count_parameters();

    // queue for transmission
    if (param_replies.push(reply)) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_reply_push_ok++;
#endif
        /*
         * [Cybernetics Ch.4] Closed-loop: on RTT the deferred MSG_NEXT_PARAM
         * can run before this IO callback has produced the reply.  Re-arm it
         * after enqueueing so single PARAM_REQUEST_READ behaves like the
         * continuously scheduled PARAM_REQUEST_LIST stream.
         */
        send_message(MSG_NEXT_PARAM);
    } else {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_gcs_param_reply_push_fail++;
#endif
    }
}

/*
  send replies to PARAM_REQUEST_READ
 */
uint8_t GCS_MAVLINK::send_parameter_async_replies()
{
    uint8_t async_replies_sent_count = 0;

    while (async_replies_sent_count < 5) {
        struct pending_param_reply reply;
        if (!param_replies.peek(reply)) {
            return async_replies_sent_count;
        }

        /*
          we reserve some space for sending parameters if the client ever
          fails to get a parameter due to lack of space
        */
        // RTT HAL: skip HAVE_PAYLOAD_SPACE check — queued_param_send handles throttling

        mavlink_msg_param_value_send(
            reply.chan,
            reply.param_name,
            reply.value,
            mav_param_type(reply.p_type),
            reply.count,
            reply.param_index);

        _queued_parameter_send_time_ms = AP_HAL::millis();
        async_replies_sent_count++;

        if (!param_replies.pop()) {
            // internal error...
            return async_replies_sent_count;
        }
    }
    return async_replies_sent_count;
}

void GCS_MAVLINK::handle_common_param_message(const mavlink_message_t &msg)
{
    switch (msg.msgid) {
    case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
        handle_param_request_list(msg);
        break;
    case MAVLINK_MSG_ID_PARAM_SET:
        handle_param_set(msg);
        break;
    case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
        handle_param_request_read(msg);
        break;
    }
}

#endif  // HAL_GCS_ENABLED
