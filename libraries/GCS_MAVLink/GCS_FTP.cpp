/*
   GCS MAVLink functions related to FTP

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

#include <AP_HAL/AP_HAL_Boards.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../../modules/rt-thread/components/libc/compilers/common/extension/sys/stat.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#endif
#ifndef _STAT_H_
#define _STAT_H_
#endif
#endif

#include "GCS_config.h"

#if AP_MAVLINK_FTP_ENABLED

#include <AP_HAL/AP_HAL.h>

#include "GCS.h"

#include <AP_Filesystem/AP_Filesystem.h>
#include <AP_HAL/utility/sparse-endian.h>
#include <AP_BoardConfig/AP_BoardConfig.h>

extern const AP_HAL::HAL& hal;

struct GCS_MAVLINK::ftp_state GCS_MAVLINK::ftp;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#define RTT_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
volatile uint32_t ftp_dbg_state RTT_DBG_DTCM_BSS = 0;
volatile uint32_t ftp_dbg_opcode RTT_DBG_DTCM_BSS = 0;
volatile uint32_t ftp_dbg_stat_count RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_ftp_requests_pushed RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_requests_dropped RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_last_request_opcode RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_last_request_seq RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_last_request_session RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_last_request_offset RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_last_request_size RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_sent RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_block_last_txbuf RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_block_payload RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_last_txspace RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_last_opcode RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_last_req_opcode RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_last_seq RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_last_offset RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_send_reply_last_size RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_push_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_push_wait_loops RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_push_last_wait_loops RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_push_max_wait_loops RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_last_offset RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_last_size RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_lseek_ret RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_read_ret RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_errno RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_ack RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_readfile_eof RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_param_banner_suppressed RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_post_send_drain_calls RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_post_send_drain_wait_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_post_send_drain_max_ms RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_post_send_drain_timeouts RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ftp_post_send_drain_last_txspace RTT_DBG_DTCM_BSS;
#endif

// timeout for session inactivity
#define FTP_SESSION_TIMEOUT 3000

bool GCS_MAVLINK::ftp_init(void) {

    // check if ftp is disabled for memory savings
#if !defined(HAL_BUILD_AP_PERIPH)
    if (AP_BoardConfig::ftp_disabled()) {
        goto failed;
    }
#endif
    // we can simply check if we allocated everything we need

    if (ftp.requests != nullptr) {
        return true;
    }

    ftp.requests = NEW_NOTHROW ObjectBuffer<pending_ftp>(5);
    if (ftp.requests == nullptr || ftp.requests->get_size() == 0) {
        goto failed;
    }

    if (!hal.scheduler->thread_create(FUNCTOR_BIND_MEMBER(&GCS_MAVLINK::ftp_worker, void),
                                      "FTP", 16384, AP_HAL::Scheduler::PRIORITY_IO, 0)) {
        goto failed;
    }

    return true;

failed:
    delete ftp.requests;
    ftp.requests = nullptr;
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "failed to initialize MAVFTP");

    return false;
}

void GCS_MAVLINK::handle_file_transfer_protocol(const mavlink_message_t &msg) {
    if (ftp_init()) {
        mavlink_file_transfer_protocol_t packet;
        mavlink_msg_file_transfer_protocol_decode(&msg, &packet);

        struct pending_ftp request;

        request.chan = chan;
        request.seq_number = le16toh_ptr(packet.payload);

        request.session = packet.payload[2];
        request.opcode = static_cast<FTP_OP>(packet.payload[3]);
        request.size = packet.payload[4];
        request.req_opcode = static_cast<FTP_OP>(packet.payload[5]);
        request.burst_complete = packet.payload[6];
        request.offset = le32toh_ptr(&packet.payload[8]);
        request.sysid = msg.sysid;
        request.compid = msg.compid;
        memcpy(request.data, &packet.payload[12], sizeof(packet.payload) - 12);

        if (!ftp.requests->push(request)) {
            // dropping the message, no buffer space to queue it in
            // we could NACK it, but that can lead to GCS confusion, so we're treating it like lost data
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            rtt_dbg_ftp_requests_dropped++;
#endif
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        } else {
            rtt_dbg_ftp_requests_pushed++;
            rtt_dbg_ftp_last_request_opcode = static_cast<uint32_t>(request.opcode);
            rtt_dbg_ftp_last_request_seq = request.seq_number;
            rtt_dbg_ftp_last_request_session = request.session;
            rtt_dbg_ftp_last_request_offset = request.offset;
            rtt_dbg_ftp_last_request_size = request.size;
#endif
        }
    }
}

bool GCS_MAVLINK::send_ftp_reply(const pending_ftp &reply)
{
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_ftp_send_reply_calls++;
    rtt_dbg_ftp_send_reply_last_txspace = txspace();
    rtt_dbg_ftp_send_reply_last_opcode = static_cast<uint32_t>(reply.opcode);
    rtt_dbg_ftp_send_reply_last_req_opcode = static_cast<uint32_t>(reply.req_opcode);
    rtt_dbg_ftp_send_reply_last_seq = reply.seq_number;
    rtt_dbg_ftp_send_reply_last_offset = reply.offset;
    rtt_dbg_ftp_send_reply_last_size = reply.size;
#endif
    if (!last_txbuf_is_greater(33)) { // It helps avoid GCS timeout if this is less than the threshold where we slow down normal streams (<=49)
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_ftp_send_reply_block_last_txbuf++;
#endif
        return false;
    }
    WITH_SEMAPHORE(comm_chan_lock(reply.chan));
    if (!HAVE_PAYLOAD_SPACE(chan, FILE_TRANSFER_PROTOCOL)) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_dbg_ftp_send_reply_block_payload++;
#endif
        return false;
    }
    uint8_t payload[251] = {};
    put_le16_ptr(payload, reply.seq_number);
    payload[2] = reply.session;
    payload[3] = static_cast<uint8_t>(reply.opcode);
    payload[4] = reply.size;
    payload[5] = static_cast<uint8_t>(reply.req_opcode);
    payload[6] = reply.burst_complete ? 1 : 0;
    put_le32_ptr(&payload[8], reply.offset);
    memcpy(&payload[12], reply.data, sizeof(reply.data));
    mavlink_msg_file_transfer_protocol_send(
        reply.chan,
        0, reply.sysid, reply.compid,
        payload);
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_ftp_send_reply_sent++;
#endif
    return true;
}

bool GCS_MAVLINK::ftp_check_name_len(const struct pending_ftp &request) {
    const size_t file_name_len = strnlen((char *)request.data, sizeof(request.data));
    if (request.size == 0) {
        return false;
    }
    if (file_name_len == request.size) {
        return true;
    }
    return (request.size - file_name_len == 1) && (request.data[sizeof(request.data) - 1] == 0);
}

void GCS_MAVLINK::ftp_error(struct pending_ftp &response, FTP_ERROR error) {
    response.opcode = FTP_OP::Nack;
    response.data[0] = static_cast<uint8_t>(error);
    response.size = 1;

    // FIXME: errno's are not thread-local as they should be on ChibiOS
    if (error == FTP_ERROR::FailErrno) {
        // translate the errno's that we have useful messages for
        switch (errno) {
            case EEXIST:
                response.data[0] = static_cast<uint8_t>(FTP_ERROR::FileExists);
                break;
            case ENOENT:
                response.data[0] = static_cast<uint8_t>(FTP_ERROR::FileNotFound);
                break;
            default:
                response.data[1] = static_cast<uint8_t>(errno);
                response.size = 2;
                break;
        }
    }
}

// send our response back out to the system
void GCS_MAVLINK::ftp_push_replies(pending_ftp &reply)
{
    ftp.last_send_ms = AP_HAL::millis(); // Used to detect active FTP session

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_ftp_push_calls++;
    uint32_t rtt_wait_loops = 0;
#endif
    while (!send_ftp_reply(reply)) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        rtt_wait_loops++;
        rtt_dbg_ftp_push_wait_loops++;
#endif
        hal.scheduler->delay(2);
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    if (reply.req_opcode == FTP_OP::ReadFile &&
        reply.opcode == FTP_OP::Ack &&
        reply.size > 64U) {
        /*
         * [Cybernetics Ch.4] Closed-loop: on RTT/CherryUSB, mavlink send
         * success only means the FTP ACK was accepted into the short USB
         * pipeline.  Pace large FTP replies by the same txspace feedback used
         * by the scheduler so the next request/reply pair does not outrun
         * the CDC IN completion chain.
         */
        rtt_dbg_ftp_post_send_drain_calls++;
        const uint32_t need_space = PAYLOAD_SIZE(reply.chan, FILE_TRANSFER_PROTOCOL);
        uint32_t waited_ms = 0;
        while (comm_get_txspace(reply.chan) < need_space && waited_ms < 25U) {
            hal.scheduler->delay(1);
            waited_ms++;
        }
        const uint32_t txspace_after = comm_get_txspace(reply.chan);
        rtt_dbg_ftp_post_send_drain_last_txspace = txspace_after;
        rtt_dbg_ftp_post_send_drain_wait_ms += waited_ms;
        if (waited_ms > rtt_dbg_ftp_post_send_drain_max_ms) {
            rtt_dbg_ftp_post_send_drain_max_ms = waited_ms;
        }
        if (txspace_after < need_space) {
            rtt_dbg_ftp_post_send_drain_timeouts++;
        }
    }
#endif
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    rtt_dbg_ftp_push_last_wait_loops = rtt_wait_loops;
    if (rtt_wait_loops > rtt_dbg_ftp_push_max_wait_loops) {
        rtt_dbg_ftp_push_max_wait_loops = rtt_wait_loops;
    }
#endif

    if (reply.req_opcode == FTP_OP::TerminateSession) {
        ftp.last_send_ms = 0;
    }
    /*
      provide same banner we would give with old param download
      Do this after send_ftp_reply() to get the first FTP response out sooner
      on slow links to avoid GCS timeout.  The slowdown of normal streams in
      get_reschedule_interval_ms() should help for subsequent responses.
    */
    if (ftp.need_banner_send_mask & (1U<<reply.chan)) {
        ftp.need_banner_send_mask &= ~(1U<<reply.chan);
        send_banner();
    }
}

void GCS_MAVLINK::ftp_worker(void) {
    pending_ftp request;
    pending_ftp reply = {};
    reply.session = -1; // flag the reply as invalid for any reuse

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    extern volatile uint32_t ftp_dbg_state;
    extern volatile uint32_t ftp_dbg_opcode;
    ftp_dbg_state = 1;
#endif

    while (true) {
        bool skip_push_reply = false;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 10;
#endif
        while (ftp.requests == nullptr || !ftp.requests->pop(request)) {
            hal.scheduler->delay(2);
        }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 20;
        ftp_dbg_opcode = static_cast<uint32_t>(request.opcode);
#endif

        // if it's a rerequest and we still have the last response then send it
        if ((request.sysid == reply.sysid) && (request.compid == reply.compid) &&
            (request.session == reply.session) && (request.seq_number + 1 == reply.seq_number)) {
            ftp_push_replies(reply);
            continue;
        }

        // setup the response
        memset(&reply, 0, sizeof(reply));
        reply.req_opcode = request.opcode;
        reply.session = request.session;
        reply.seq_number = request.seq_number + 1;
        reply.chan = request.chan;
        reply.sysid = request.sysid;
        reply.compid = request.compid;

        // sanity check the request size
        if (request.size > sizeof(request.data)) {
            ftp_error(reply, FTP_ERROR::InvalidDataSize);
            ftp_push_replies(reply);
            continue;
        }

        uint32_t now = AP_HAL::millis();

        // check for session termination
        if (request.session != ftp.current_session &&
            (request.opcode == FTP_OP::TerminateSession || request.opcode == FTP_OP::ResetSessions)) {
            // terminating a different session, just ack
            reply.opcode = FTP_OP::Ack;
        } else if (ftp.fd != -1 && request.session != ftp.current_session &&
                   now - ftp.last_send_ms < FTP_SESSION_TIMEOUT) {
            // if we have an open file and the session isn't right
            // then reject. This prevents IO on the wrong file
            ftp_error(reply, FTP_ERROR::InvalidSession);
        } else {
            if (ftp.fd != -1 &&
                request.session != ftp.current_session &&
                now - ftp.last_send_ms >= FTP_SESSION_TIMEOUT) {
                // if a new session appears and the old session has
                // been idle for more than the timeout then force
                // close the old session
                AP::FS().close(ftp.fd);
                ftp.fd = -1;
                ftp.current_session = -1;
            }
            // dispatch the command as needed
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            ftp_dbg_state = 30 + static_cast<uint32_t>(request.opcode);
#endif
            switch (request.opcode) {
                case FTP_OP::None:
                    reply.opcode = FTP_OP::Ack;
                    break;
                case FTP_OP::TerminateSession:
                case FTP_OP::ResetSessions:
                    if (ftp.fd != -1) {
                        AP::FS().close(ftp.fd);
                        ftp.fd = -1;
                    }
                    ftp.current_session = -1;
                    reply.opcode = FTP_OP::Ack;
                    break;
                case FTP_OP::ListDirectory:
                    ftp_list_dir(request, reply);
                    break;
                case FTP_OP::OpenFileRO:
                    {
                        // only allow one file to be open per session
                        if (ftp.fd != -1 && now - ftp.last_send_ms > FTP_SESSION_TIMEOUT) {
                            // no activity for 3s, assume client has
                            // timed out receiving open reply, close
                            // the file
                            AP::FS().close(ftp.fd);
                            ftp.fd = -1;
                            ftp.current_session = -1;
                        }
                        if (ftp.fd != -1) {
                            ftp_error(reply, FTP_ERROR::Fail);
                            break;
                        }

                        // sanity check that our the request looks well formed
                        if (!ftp_check_name_len(request)) {
                            ftp_error(reply, FTP_ERROR::InvalidDataSize);
                            break;
                        }

                        request.data[sizeof(request.data) - 1] = 0; // ensure the path is null terminated

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        ftp_dbg_state = 340;
#endif
                        size_t file_size = 0;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        const bool rtt_use_open_first = (((const char *)request.data)[0] != '@');
                        ftp_dbg_state = 342;
                        if (rtt_use_open_first) {
                            ftp.fd = AP::FS().open((char *)request.data, O_RDONLY);
                        } else {
                            struct stat st;
                            if (AP::FS().stat((char *)request.data, &st)) {
                                ftp_dbg_state = 341;
                                ftp_error(reply, FTP_ERROR::FailErrno);
                                break;
                            }
                            ftp_dbg_state = 342;
                            file_size = st.st_size;
                            ftp.fd = AP::FS().open((char *)request.data, O_RDONLY);
                        }
#else
                        // get the file size before opening
                        struct stat st;
                        if (AP::FS().stat((char *)request.data, &st)) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }
                        file_size = st.st_size;

                        // actually open the file
                        ftp.fd = AP::FS().open((char *)request.data, O_RDONLY);
#endif
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        ftp_dbg_state = 343;
                        extern volatile uint32_t ftp_dbg_stat_count;
                        if (ftp.fd == -1) {
                            ftp_dbg_stat_count = (uint32_t)errno;
                        }
#endif
                        if (ftp.fd == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        if (rtt_use_open_first) {
                            const int32_t end_pos = AP::FS().lseek(ftp.fd, 0, SEEK_END);
                            if (end_pos < 0 || AP::FS().lseek(ftp.fd, 0, SEEK_SET) < 0) {
                                AP::FS().close(ftp.fd);
                                ftp.fd = -1;
                                ftp_error(reply, FTP_ERROR::FailErrno);
                                break;
                            }
                            file_size = (size_t)end_pos;
                        }
#endif
                        ftp.mode = FTP_FILE_MODE::Read;
                        ftp.current_session = request.session;

                        reply.opcode = FTP_OP::Ack;
                        reply.size = sizeof(uint32_t);
                        put_le32_ptr(reply.data, (uint32_t)file_size);

                        // provide compatibility with old protocol banner download
                        if (strncmp((const char *)request.data, "@PARAM/param.pck", 16) == 0) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                            /*
                             * [Cybernetics Ch.4] Closed-loop: keep RTT USB
                             * MAVFTP parameter transfer as a pure request/reply
                             * stream while CherryUSB backpressure is being
                             * closed. Injecting STATUSTEXT banner frames after
                             * OpenFileRO can fill the short CDC queue before the
                             * next 239B ReadFile ACK reaches the host.
                             */
                            rtt_dbg_ftp_param_banner_suppressed++;
#else
                            ftp.need_banner_send_mask |= 1U<<reply.chan;
#endif
                        }
                        break;
                    }
                case FTP_OP::ReadFile:
                    {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        rtt_dbg_ftp_readfile_calls++;
                        rtt_dbg_ftp_readfile_last_offset = request.offset;
                        rtt_dbg_ftp_readfile_last_size = request.size;
                        rtt_dbg_ftp_readfile_lseek_ret = 0xFFFFFFFFU;
                        rtt_dbg_ftp_readfile_read_ret = 0xFFFFFFFFU;
                        rtt_dbg_ftp_readfile_errno = 0;
#endif
                        // must actually be working on a file
                        if (ftp.fd == -1) {
                            ftp_error(reply, FTP_ERROR::FileNotFound);
                            break;
                        }

                        // must have the file in read mode
                        if ((ftp.mode != FTP_FILE_MODE::Read)) {
                            ftp_error(reply, FTP_ERROR::Fail);
                            break;
                        }

                        // seek to requested offset
                        const int32_t seek_ret = AP::FS().lseek(ftp.fd, request.offset, SEEK_SET);
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        rtt_dbg_ftp_readfile_lseek_ret = (uint32_t)seek_ret;
                        rtt_dbg_ftp_readfile_errno = (uint32_t)errno;
#endif
                        if (seek_ret == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        // fill the buffer
                        const ssize_t read_bytes = AP::FS().read(ftp.fd, reply.data, MIN(sizeof(reply.data),request.size));
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        rtt_dbg_ftp_readfile_read_ret = (uint32_t)read_bytes;
                        rtt_dbg_ftp_readfile_errno = (uint32_t)errno;
#endif
                        if (read_bytes == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }
                        if (read_bytes == 0) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                            rtt_dbg_ftp_readfile_eof++;
#endif
                            ftp_error(reply, FTP_ERROR::EndOfFile);
                            break;
                        }

                        reply.opcode = FTP_OP::Ack;
                        reply.offset = request.offset;
                        reply.size = (uint8_t)read_bytes;
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
                        rtt_dbg_ftp_readfile_ack++;
#endif
                        break;
                    }
                case FTP_OP::Ack:
                case FTP_OP::Nack:
                    // eat these, we just didn't expect them
                    continue;
                    break;
                case FTP_OP::OpenFileWO:
                case FTP_OP::CreateFile:
                    {
                        // only allow one file to be open per session
                        if (ftp.fd != -1) {
                            ftp_error(reply, FTP_ERROR::Fail);
                            break;
                        }

                        // sanity check that our the request looks well formed
                        if (!ftp_check_name_len(request)) {
                            ftp_error(reply, FTP_ERROR::InvalidDataSize);
                            break;
                        }

                        request.data[sizeof(request.data) - 1] = 0; // ensure the path is null terminated

                        // actually open the file
                        ftp.fd = AP::FS().open((char *)request.data,
                                               (request.opcode == FTP_OP::CreateFile) ? O_WRONLY|O_CREAT|O_TRUNC : O_WRONLY);
                        if (ftp.fd == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }
                        ftp.mode = FTP_FILE_MODE::Write;
                        ftp.current_session = request.session;

                        reply.opcode = FTP_OP::Ack;
                        break;
                    }
                case FTP_OP::WriteFile:
                    {
                        // must actually be working on a file
                        if (ftp.fd == -1) {
                            ftp_error(reply, FTP_ERROR::FileNotFound);
                            break;
                        }

                        // must have the file in write mode
                        if ((ftp.mode != FTP_FILE_MODE::Write)) {
                            ftp_error(reply, FTP_ERROR::Fail);
                            break;
                        }

                        // seek to requested offset
                        if (AP::FS().lseek(ftp.fd, request.offset, SEEK_SET) == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        // fill the buffer
                        const ssize_t write_bytes = AP::FS().write(ftp.fd, request.data, request.size);
                        if (write_bytes == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        reply.opcode = FTP_OP::Ack;
                        reply.offset = request.offset;
                        break;
                    }
                case FTP_OP::CreateDirectory:
                    {
                        // sanity check that our the request looks well formed
                        if (!ftp_check_name_len(request)) {
                            ftp_error(reply, FTP_ERROR::InvalidDataSize);
                            break;
                        }

                        request.data[sizeof(request.data) - 1] = 0; // ensure the path is null terminated

                        // actually make the directory
                        if (AP::FS().mkdir((char *)request.data) == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        reply.opcode = FTP_OP::Ack;
                        break;
                    }
                case FTP_OP::RemoveDirectory:
                case FTP_OP::RemoveFile:
                    {
                        // sanity check that our the request looks well formed
                        if (!ftp_check_name_len(request)) {
                            ftp_error(reply, FTP_ERROR::InvalidDataSize);
                            break;
                        }

                        request.data[sizeof(request.data) - 1] = 0; // ensure the path is null terminated

                        // remove the file/dir
                        if (AP::FS().unlink((char *)request.data) == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        reply.opcode = FTP_OP::Ack;
                        break;
                    }
                case FTP_OP::CalcFileCRC32:
                    {
                        // sanity check that our the request looks well formed
                        if (!ftp_check_name_len(request)) {
                            ftp_error(reply, FTP_ERROR::InvalidDataSize);
                            break;
                        }

                        request.data[sizeof(request.data) - 1] = 0; // ensure the path is null terminated

                        uint32_t checksum = 0;
                        if (!AP::FS().crc32((char *)request.data, checksum)) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        // reset our scratch area so we don't leak data, and can leverage trimming
                        memset(reply.data, 0, sizeof(reply.data));
                        reply.size = sizeof(uint32_t);
                        put_le32_ptr(reply.data, checksum);
                        reply.opcode = FTP_OP::Ack;
                        break;
                    }
                case FTP_OP::BurstReadFile:
                    {
                        const uint16_t max_read = (request.size == 0?sizeof(reply.data):request.size);
                        // must actually be working on a file
                        if (ftp.fd == -1) {
                            ftp_error(reply, FTP_ERROR::FileNotFound);
                            break;
                        }

                        // must have the file in read mode
                        if ((ftp.mode != FTP_FILE_MODE::Read)) {
                            ftp_error(reply, FTP_ERROR::Fail);
                            break;
                        }

                        // seek to requested offset
                        if (AP::FS().lseek(ftp.fd, request.offset, SEEK_SET) == -1) {
                            ftp_error(reply, FTP_ERROR::FailErrno);
                            break;
                        }

                        /*
                          calculate a burst delay so that FTP burst
                          transfer doesn't use more than 1/3 of
                          available bandwidth on links that don't have
                          flow control. This reduces the chance of
                          lost packets a lot, which results in overall
                          faster transfers
                         */
                        uint32_t burst_delay_ms = 0;
                        if (valid_channel(request.chan)) {
                            auto *port = mavlink_comm_port[request.chan];
                            if (port != nullptr && port->get_flow_control() != AP_HAL::UARTDriver::FLOW_CONTROL_ENABLE) {
                                const uint32_t bw = port->bw_in_bytes_per_second();
                                if (bw > 0) {
                                    const uint16_t pkt_size = PAYLOAD_SIZE(request.chan, FILE_TRANSFER_PROTOCOL) - (sizeof(reply.data) - max_read);
                                    burst_delay_ms = 3000 * pkt_size / bw;
                                }
                            }
                        }

                        // this transfer size is enough for a full parameter file with max parameters
                        const uint32_t transfer_size = 500;
                        for (uint32_t i = 0; (i < transfer_size); i++) {
                            // fill the buffer
                            const ssize_t read_bytes = AP::FS().read(ftp.fd, reply.data, MIN(sizeof(reply.data), max_read));
                            if (read_bytes == -1) {
                                ftp_error(reply, FTP_ERROR::FailErrno);
                                break;
                            }

                            if (read_bytes != sizeof(reply.data)) {
                                // don't send any old data
                                memset(reply.data + read_bytes, 0, sizeof(reply.data) - read_bytes);
                            }

                            if (read_bytes == 0) {
                                ftp_error(reply, FTP_ERROR::EndOfFile);
                                break;
                            }

                            reply.opcode = FTP_OP::Ack;
                            reply.offset = request.offset + i * max_read;
                            reply.burst_complete = ((read_bytes < max_read) || (i == (transfer_size - 1)));
                            reply.size = (uint8_t)read_bytes;

                            ftp_push_replies(reply);

                            if (read_bytes < max_read) {
                                // ensure the NACK which we send next is at the right offset
                                reply.offset += read_bytes;
                            }

                            // prep the reply to be used again
                            reply.seq_number++;

                            hal.scheduler->delay(burst_delay_ms);
                        }

                        if (reply.opcode != FTP_OP::Nack) {
                            // prevent a duplicate packet send for
                            // normal replies of burst reads
                            skip_push_reply = true;
                        }
                        break;
                    }

                case FTP_OP::Rename: {
                    // sanity check that the request looks well formed
                    const char *filename1 = (char*)request.data;
                    const size_t len1 = strnlen(filename1, sizeof(request.data)-2);
                    const char *filename2 = (char*)&request.data[len1+1];
                    const size_t len2 = strnlen(filename2, sizeof(request.data)-(len1+1));
                    const bool is_req_size_consider_tnull = (request.size - (len1+len2) == 2 &&
                                                             request.data[sizeof(request.data) - 1] == 0);
                    if (filename1[len1] != 0 || ((len1+len2+1 != request.size) && !is_req_size_consider_tnull) || (request.size == 0)) {
                        ftp_error(reply, FTP_ERROR::InvalidDataSize);
                        break;
                    }
                    request.data[sizeof(request.data) - 1] = 0; // ensure the 2nd path is null terminated
                    // remove the file/dir
                    if (AP::FS().rename(filename1, filename2) != 0) {
                        ftp_error(reply, FTP_ERROR::FailErrno);
                        break;
                    }
                    reply.opcode = FTP_OP::Ack;
                    break;
                }

                case FTP_OP::TruncateFile:
                default:
                    // this was bad data, just nack it
                    GCS_SEND_TEXT(MAV_SEVERITY_DEBUG, "Unsupported FTP: %d", static_cast<int>(request.opcode));
                    ftp_error(reply, FTP_ERROR::Fail);
                    break;
            }
        }

        if (!skip_push_reply) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            ftp_dbg_state = 90;
#endif
            ftp_push_replies(reply);
        }

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 100;
#endif
        continue;
    }
}

// calculates how much string length is needed to fit this in a list response
int GCS_MAVLINK::gen_dir_entry(char *dest, size_t space, const char *path, const struct dirent * entry) {
#if AP_FILESYSTEM_HAVE_DIRENT_DTYPE
    const bool is_file = entry->d_type == DT_REG || entry->d_type == DT_LNK;
#else
    // assume true initially, then handle below
    const bool is_file = true;
#endif

    if (space < 3) {
        return -1;
    }
    dest[0] = 0;

#if AP_FILESYSTEM_HAVE_DIRENT_DTYPE
    if (!is_file && entry->d_type != DT_DIR) {
        return -1; // this just forces it so we can't send this back, it's easier then sending skips to a GCS
    }
#endif

    if (is_file) {
#ifdef MAX_NAME_LEN
        const uint8_t max_name_len = MIN(unsigned(MAX_NAME_LEN), 255U);
#else
        const uint8_t max_name_len = 255U;
#endif
        const size_t full_path_len = strlen(path) + strnlen(entry->d_name, max_name_len);
        if (full_path_len > 240) {
            return -1;
        }
        char full_path[256];
        hal.util->snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        memset(&st, 0, sizeof(st));
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        extern volatile uint32_t ftp_dbg_state;
        extern volatile uint32_t ftp_dbg_stat_count;
        ftp_dbg_state = 300;
        ftp_dbg_stat_count++;
#endif
        if (AP::FS().stat(full_path, &st)) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
            ftp_dbg_state = 301;
#endif
            return -1;
        }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 310;
#endif

#if !AP_FILESYSTEM_HAVE_DIRENT_DTYPE
        if (S_ISDIR(st.st_mode)) {
            return hal.util->snprintf(dest, space, "D%s%c", entry->d_name, (char)0);
        }
#endif
        return hal.util->snprintf(dest, space, "F%s\t%u%c", entry->d_name, (unsigned)st.st_size, (char)0);
    } else {
        return hal.util->snprintf(dest, space, "D%s%c", entry->d_name, (char)0);
    }
}

// list the contents of a directory, skip the offset number of entries before providing data
void GCS_MAVLINK::ftp_list_dir(struct pending_ftp &request, struct pending_ftp &response) {
    response.offset = request.offset;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    extern volatile uint32_t ftp_dbg_state;
    ftp_dbg_state = 200;
#endif

    if (!ftp_check_name_len(request)) {
        ftp_error(response, FTP_ERROR::InvalidDataSize);
        return;
    }

    request.data[sizeof(request.data) - 1] = 0;

    const size_t dir_len = strlen((char *)request.data);
    if ((dir_len > 1) && (request.data[dir_len - 1] == '/')) {
        request.data[dir_len - 1] = 0;
    }

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    ftp_dbg_state = 210;
#endif

    auto *dir = AP::FS().opendir((char *)request.data);
    if (dir == nullptr) {
        ftp_error(response, FTP_ERROR::FailErrno);
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 211;
#endif
        return;
    }

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    ftp_dbg_state = 220;
#endif

    // burn the entries we don't care about
    while (request.offset > 0) {
        const struct dirent *entry = AP::FS().readdir(dir);
        if(entry == nullptr) {
            ftp_error(response, FTP_ERROR::EndOfFile);
            AP::FS().closedir(dir);
            return;
        }

        // check how much space would be needed to emit the listing
        const int needed_space = gen_dir_entry((char *)response.data, sizeof(request.data), (char *)request.data, entry);

        if (needed_space < 0 || needed_space > (int)sizeof(request.data)) {
            continue;
        }

        request.offset--;
    }

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    ftp_dbg_state = 230;
#endif

    uint8_t index = 0;
    struct dirent *entry;
    while ((entry = AP::FS().readdir(dir))) {
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 240;
#endif
        const int required_space = gen_dir_entry((char *)(response.data + index), sizeof(response.data) - index, (char *)request.data, entry);

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
        ftp_dbg_state = 250;
#endif

        if (required_space < 0) {
            continue;
        }

        if ((required_space + index) >= (int)sizeof(request.data)) {
            break;
        }

        index += required_space + 1;
    }

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
    ftp_dbg_state = 260;
#endif

    if (index == 0) {
        ftp_error(response, FTP_ERROR::EndOfFile);
        AP::FS().closedir(dir);
        return;
    }
    
    // strip any bad temp data from our response as it can confuse a GCS, and defeats 0 trimming
    if (index < sizeof(response.data)) {
        memset(response.data + index, 0, MAX(0, (int)(sizeof(response.data)) - index));
    }

    response.opcode = FTP_OP::Ack;
    response.size = index;

    AP::FS().closedir(dir);
}

#endif  // AP_MAVLINK_FTP_ENABLED
