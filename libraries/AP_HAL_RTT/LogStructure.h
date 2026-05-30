#pragma once

#include <AP_Logger/LogStructure.h>

/*
 * HAL-layer logger message IDs for RTT (parity with AP_HAL_ChibiOS/LogStructure.h).
 * Scheduler MON logging can be wired later; structures must exist for Logger tables.
 */

#define LOG_IDS_FROM_HAL_RTT \
    LOG_MON_MSG,                 \
    LOG_WDOG_MSG

// @LoggerMessage: MON
// @Description: Main loop performance monitoring message.
struct PACKED log_MON {
    LOG_PACKET_HEADER;
    uint64_t time_us;
    uint32_t loop_delay;
    int8_t current_task;
    uint32_t internal_error_mask;
    uint16_t internal_error_count;
    uint16_t internal_error_line;
    uint16_t mavmsg;
    uint16_t mavcmd;
    uint16_t semline;
    uint32_t spicnt;
    uint32_t i2ccnt;
};

// @LoggerMessage: WDOG
// @Description: Watchdog diagnostics
struct PACKED log_WDOG {
    LOG_PACKET_HEADER;
    uint64_t time_us;
    int8_t scheduler_task;
    uint32_t internal_errors;
    uint16_t internal_error_count;
    uint16_t internal_error_last_line;
    uint16_t last_mavlink_msgid;
    uint16_t last_mavlink_cmd;
    uint16_t semaphore_line;
    uint16_t fault_line;
    uint16_t fault_type;
    uint32_t fault_addr;
    uint8_t fault_thd_prio;
    uint32_t fault_icsr;
    uint32_t fault_lr;
    char thread_name4[4];
};

#define LOG_STRUCTURE_FROM_HAL_RTT                                  \
    { LOG_MON_MSG, sizeof(log_MON),                                     \
      "MON","QIbIHHHHHII","TimeUS,Dly,Tsk,IErr,IErrCnt,IErrLn,MM,MC,SmLn,SPICnt,I2CCnt", "s----------", "F----------", false }, \
    { LOG_WDOG_MSG, sizeof(log_WDOG),                                   \
     "WDOG","QbIHHHHHHHIBIIn","TimeUS,Tsk,IE,IEC,IEL,MvMsg,MvCmd,SmLn,FL,FT,FA,FP,ICSR,LR,TN", "s--------------", "F--------------", false },
