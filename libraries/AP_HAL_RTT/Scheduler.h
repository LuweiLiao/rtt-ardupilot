/*
 * AP_HAL_RTT Scheduler — mirrors ChibiOS HAL thread architecture:
 *   timer thread  : 1kHz  _run_timers() + UART _timer_tick
 *   io thread     : 1kHz  _run_io()
 *   storage thread: 1kHz  hal.storage->_timer_tick()
 * GCS is driven by main-thread delay() -> call_delay_cb() mechanism.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include "HAL_RTT_Namespace.h"
#include "Semaphores.h"
#include <rtthread.h>

#define RTT_SCHEDULER_MAX_TIMER_PROCS 10
#define RTT_SCHEDULER_MAX_IO_PROCS 10

class RTT::Scheduler : public AP_HAL::Scheduler
{
public:
    Scheduler();
    void init() override;
    void set_callbacks(AP_HAL::HAL::Callbacks* cb) { callbacks = cb; }
    void delay(uint16_t ms) override;
    void delay_microseconds(uint16_t us) override;
    void register_timer_process(AP_HAL::MemberProc proc) override;
    void register_io_process(AP_HAL::MemberProc proc) override;
    void register_timer_failsafe(AP_HAL::Proc failsafe, uint32_t period_us) override;
    void reboot(bool hold_in_bootloader) override;
    bool in_main_thread() const override;
    void set_system_initialized() override;
    bool is_system_initialized() override;
    bool thread_create(AP_HAL::MemberProc proc, const char* name, uint32_t stack_size,
                       priority_base base, int8_t priority) override;
    void expect_delay_ms(uint32_t ms) override;
    bool in_expected_delay() const override { return _expected_delay_ms > 0; }

    void set_main_thread_id(rt_thread_t id) { _main_thread_id = id; }

private:
    AP_HAL::HAL::Callbacks* callbacks = nullptr;
    AP_HAL::Proc _failsafe = nullptr;

    AP_HAL::MemberProc _timer_proc[RTT_SCHEDULER_MAX_TIMER_PROCS];
    uint8_t _num_timer_procs = 0;
    AP_HAL::MemberProc _io_proc[RTT_SCHEDULER_MAX_IO_PROCS];
    uint8_t _num_io_procs = 0;

    static bool _system_initialized;
    rt_thread_t _main_thread_id = nullptr;

    Semaphore _timer_sem;
    Semaphore _io_sem;

    bool _in_timer_proc = false;
    bool _in_io_proc = false;

    uint32_t _expected_delay_ms = 0;

    void _run_timers();
    void _run_io();

    static void _timer_thread_entry(void *arg);
    rt_thread_t _timer_thread_ctx = nullptr;

    static void _io_thread_entry(void *arg);
    rt_thread_t _io_thread_ctx = nullptr;

    static void _storage_thread_entry(void *arg);
    rt_thread_t _storage_thread_ctx = nullptr;

    static void _thread_create_trampoline(void *arg);
};
