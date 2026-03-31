/*
 * AP_HAL_RTT Scheduler — mirrors ChibiOS HAL thread architecture:
 *   monitor thread: 10Hz  watchdog, stuck detection, stack check, GPIO timer_tick
 *   timer thread  : 1kHz  _run_timers() (no UART — separated to avoid CDC blocking)
 *   rcout thread  : 1kHz  RCOutput timer_tick / push cycle
 *   rcin thread   : 1kHz  RCInput _timer_tick (RC protocol processing)
 *   uart thread   : 1kHz  UART _timer_tick() (isolated from timer callbacks)
 *   io thread     : 1kHz  _run_io()
 *   storage thread: 1kHz  hal.storage->_timer_tick()
 * GCS is driven by main-thread delay() -> call_delay_cb() mechanism.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include "HAL_RTT_Namespace.h"
#include "Semaphores.h"
#include <rtthread.h>

#define RTT_SCHEDULER_MAX_TIMER_PROCS 8
#define RTT_SCHEDULER_MAX_IO_PROCS    8

/*
 * RT-Thread priority mapping — mirrors ChibiOS priority order.
 * ChibiOS higher number = higher priority; RT-Thread lower number = higher priority.
 * We map to ensure the same relative ordering.
 */
#define APM_RTT_MONITOR_PRIORITY  (RT_THREAD_PRIORITY_MAX / 8 - 1)  // highest HAL thread
#define APM_RTT_TIMER_PRIORITY    (RT_THREAD_PRIORITY_MAX / 8)      // 4
#define APM_RTT_RCOUT_PRIORITY    (RT_THREAD_PRIORITY_MAX / 8)      // same as timer
#define APM_RTT_RCIN_PRIORITY     (RT_THREAD_PRIORITY_MAX / 6)      // 5
#define APM_RTT_MAIN_PRIORITY     (RT_THREAD_PRIORITY_MAX / 3)      // 10
#define APM_RTT_MAIN_BOOST        (RT_THREAD_PRIORITY_MAX / 4)      // 8
#define APM_RTT_SPI_PRIORITY      (RT_THREAD_PRIORITY_MAX / 6)      // 5
#define APM_RTT_I2C_PRIORITY      (RT_THREAD_PRIORITY_MAX / 5)      // 6
#define APM_RTT_UART_PRIORITY     (RT_THREAD_PRIORITY_MAX / 4 + 1)  // 9 — must be near main_boost to prevent starvation
#define APM_RTT_LED_PRIORITY      (RT_THREAD_PRIORITY_MAX / 3 + 2)  // 12
#define APM_RTT_IO_PRIORITY       (RT_THREAD_PRIORITY_MAX / 2)      // 16
#define APM_RTT_STORAGE_PRIORITY  (RT_THREAD_PRIORITY_MAX / 2 + 2)  // 18
#define APM_RTT_SCRIPTING_PRIORITY (RT_THREAD_PRIORITY_MAX - 2)     // lowest

class RTT::Scheduler : public AP_HAL::Scheduler
{
public:
    Scheduler();
    void init() override;
    void set_callbacks(AP_HAL::HAL::Callbacks* cb) { callbacks = cb; }
    void delay(uint16_t ms) override;
    void delay_microseconds(uint16_t us) override;
    void delay_microseconds_boost(uint16_t us) override;
    bool check_called_boost(void);
    void boost_end(void) override;
    void register_timer_process(AP_HAL::MemberProc proc) override;
    void register_io_process(AP_HAL::MemberProc proc) override;
    void register_timer_failsafe(AP_HAL::Proc failsafe, uint32_t period_us) override;
    void reboot(bool hold_in_bootloader) override;
    bool in_main_thread() const override;
    void set_system_initialized() override;
    bool is_system_initialized() override { return _initialized; }
    void hal_initialized() { _hal_initialized = true; }
    bool thread_create(AP_HAL::MemberProc proc, const char* name, uint32_t stack_size,
                       priority_base base, int8_t priority) override;
    void expect_delay_ms(uint32_t ms) override;
    bool in_expected_delay() const override;

    void *disable_interrupts_save(void) override;
    void  restore_interrupts(void *) override;

    void watchdog_pat(void);

    void set_main_thread_id(rt_thread_t id) { _main_thread_id = id; }

private:
    AP_HAL::HAL::Callbacks* callbacks = nullptr;
    AP_HAL::Proc _failsafe = nullptr;

    AP_HAL::MemberProc _timer_proc[RTT_SCHEDULER_MAX_TIMER_PROCS];
    uint8_t _num_timer_procs = 0;
    AP_HAL::MemberProc _io_proc[RTT_SCHEDULER_MAX_IO_PROCS];
    uint8_t _num_io_procs = 0;

    bool _initialized = false;
    volatile bool _hal_initialized = false;
    rt_thread_t _main_thread_id = nullptr;

    Semaphore _timer_sem;
    Semaphore _io_sem;

    bool _in_timer_proc = false;
    bool _in_io_proc = false;
    bool _priority_boosted = false;
    bool _called_boost = false;

    uint32_t _expect_delay_start = 0;
    uint32_t _expect_delay_length = 0;
    uint8_t  _expect_delay_nesting = 0;
    uint32_t last_watchdog_pat_ms = 0;
    volatile bool _iwdg_started = false;

    void _run_timers();
    void _run_io();

    uint8_t calculate_thread_priority(priority_base base, int8_t priority) const;

    static void _delay_microseconds_dwt(uint16_t us);

    static void _timer_thread_entry(void *arg);
    rt_thread_t _timer_thread_ctx = nullptr;

    static void _rcout_thread_entry(void *arg);
    rt_thread_t _rcout_thread_ctx = nullptr;

    static void _rcin_thread_entry(void *arg);
    rt_thread_t _rcin_thread_ctx = nullptr;

    static void _uart_thread_entry(void *arg);
    rt_thread_t _uart_thread_ctx = nullptr;

    static void _io_thread_entry(void *arg);
    rt_thread_t _io_thread_ctx = nullptr;

    static void _storage_thread_entry(void *arg);
    rt_thread_t _storage_thread_ctx = nullptr;

    static void _monitor_thread_entry(void *arg);
    rt_thread_t _monitor_thread_ctx = nullptr;

    void _check_stack_free(void);

    static void _thread_create_trampoline(void *arg);
};
