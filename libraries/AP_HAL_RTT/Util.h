/*
 * AP_HAL_RTT — Util (aligned with ChibiOS)
 * Time functions, memory stats, system ID, safety, tone, watchdog.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include "HAL_RTT_Namespace.h"

class ExpandingString;

class RTT::Util : public AP_HAL::Util
{
public:
    uint32_t available_memory() override;
    bool get_system_id(char buf[50]) override;
    bool get_system_id_unformatted(uint8_t buf[], uint8_t &len) override;
    void set_hw_rtc(uint64_t time_utc_usec) override;
    uint64_t get_hw_rtc() const override;
    void thread_info(ExpandingString& str) override;
    void mem_info(ExpandingString& str) override;

    void *malloc_type(size_t size, AP_HAL::Util::Memory_Type mem_type) override;
    void free_type(void *ptr, size_t size, AP_HAL::Util::Memory_Type mem_type) override;

    enum safety_state safety_switch_state(void) override;

    bool toneAlarm_init(uint8_t types) override;
    void toneAlarm_set_buzzer_tone(float frequency, float volume, uint32_t duration_ms) override;

    bool was_watchdog_reset() const override;
    bool get_random_vals(uint8_t* data, size_t size) override;
    void set_soft_armed(const bool b) override;

    uint32_t get_millis() const;
    uint64_t get_micros64() const;

private:
    bool _soft_armed = false;
};
