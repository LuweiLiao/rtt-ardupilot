/*
 * AP_HAL_RTT — Util
 * Time functions, memory stats, system ID, persistent data.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include "HAL_RTT_Namespace.h"

class RTT::Util : public AP_HAL::Util
{
public:
    uint32_t available_memory() override;
    bool get_system_id(char buf[50]) override;
    bool get_system_id_unformatted(uint8_t buf[], uint8_t &len) override;
    void set_hw_rtc(uint64_t time_utc_usec) override;
    uint64_t get_hw_rtc() const override;
    void thread_info(ExpandingString& str) override;

    uint32_t get_millis() const;
    uint64_t get_micros64() const;
};
