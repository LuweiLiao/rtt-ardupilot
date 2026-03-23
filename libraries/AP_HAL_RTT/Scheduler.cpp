/*
 * AP_HAL_RTT Scheduler — mirrors ChibiOS HAL thread architecture.
 *
 * Threads created by init():
 *   ap_timer  : 1kHz — UART _timer_tick on all ports, then _run_timers()
 *   ap_io     : 1kHz — _run_io() (registered IO callbacks)
 *   storage   : 1kHz — hal.storage->_timer_tick()  (if HAL_WITH_RAMTRON)
 *
 * GCS communication is driven entirely by the main thread through the
 * standard ArduPilot delay-callback mechanism:
 *   main thread delay() → call_delay_cb() → scheduler_delay_callback()
 *     → gcs().update_receive/send() + HEARTBEAT
 *
 * thread_create() implements the ArduPilot upper-layer thread API so
 * AP_Scheduler and other subsystems can create worker threads.
 */

#include "AP_HAL_RTT/Scheduler.h"
#include "AP_HAL_RTT/UARTDriver.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>

#include <AP_RCProtocol/AP_RCProtocol_config.h>
#if AP_RCPROTOCOL_ENABLED
#include <AP_RCProtocol/AP_RCProtocol.h>
#endif

using namespace RTT;

extern const AP_HAL::HAL& hal;

bool Scheduler::_system_initialized = false;

Scheduler::Scheduler()
{
}

/* ----------------------------------------------------------------
 *  Timer thread — equivalent to ChibiOS _timer_thread
 *  1 kHz: UART flush + registered timer callbacks + failsafe
 * ---------------------------------------------------------------- */
void Scheduler::_timer_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;
    while (true) {
        rt_thread_mdelay(1);

        for (uint8_t i = 0; i < 10; i++) {
            auto *uart = (UARTDriver *)hal.serial(i);
            if (uart) {
                uart->_timer_tick();
            }
        }

        sched->_run_timers();
    }
}

/* ----------------------------------------------------------------
 *  IO thread — equivalent to ChibiOS _io_thread
 *  1 kHz: registered IO callbacks (param_io_timer, etc.)
 * ---------------------------------------------------------------- */
void Scheduler::_io_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;
    while (true) {
        rt_thread_mdelay(1);
        sched->_run_io();
    }
}

/* ----------------------------------------------------------------
 *  Storage thread — equivalent to ChibiOS _storage_thread
 * ---------------------------------------------------------------- */
void Scheduler::_storage_thread_entry(void *arg)
{
    (void)arg;
    while (true) {
        rt_thread_mdelay(1);
        if (hal.storage != nullptr) {
            hal.storage->_timer_tick();
        }
    }
}

/* ----------------------------------------------------------------
 *  thread_create — equivalent to ChibiOS thread_create
 *  Used by AP_Scheduler and other subsystems to spawn worker threads.
 * ---------------------------------------------------------------- */
void Scheduler::_thread_create_trampoline(void *arg)
{
    auto *proc = (AP_HAL::MemberProc *)arg;
    (*proc)();
    free(proc);
}

bool Scheduler::thread_create(AP_HAL::MemberProc proc, const char* name,
                              uint32_t stack_size, priority_base base, int8_t priority)
{
    auto *tproc = (AP_HAL::MemberProc *)malloc(sizeof(proc));
    if (!tproc) {
        return false;
    }
    *tproc = proc;

    uint8_t rtt_prio;
    switch (base) {
    case PRIORITY_BOOST:
    case PRIORITY_TIMER:
        rtt_prio = RT_THREAD_PRIORITY_MAX / 4;
        break;
    case PRIORITY_IO:
        rtt_prio = RT_THREAD_PRIORITY_MAX / 2;
        break;
    case PRIORITY_STORAGE:
        rtt_prio = RT_THREAD_PRIORITY_MAX / 2 + 2;
        break;
    case PRIORITY_SCRIPTING:
    case PRIORITY_NET:
    default:
        rtt_prio = RT_THREAD_PRIORITY_MAX / 2 + 4;
        break;
    }
    int8_t adj = priority;
    if (adj > 0 && rtt_prio > (uint8_t)adj) {
        rtt_prio -= adj;
    } else if (adj < 0 && rtt_prio < (uint8_t)(RT_THREAD_PRIORITY_MAX + adj)) {
        rtt_prio -= adj;
    }
    if (rtt_prio >= RT_THREAD_PRIORITY_MAX) {
        rtt_prio = RT_THREAD_PRIORITY_MAX - 1;
    }

    if (stack_size < 2048) {
        stack_size = 2048;
    }

    rt_thread_t th = rt_thread_create(name, _thread_create_trampoline,
                                      tproc, stack_size, rtt_prio, 20);
    if (th != nullptr) {
        rt_thread_startup(th);
        return true;
    }
    free(tproc);
    return false;
}

/* ----------------------------------------------------------------
 *  init — create all HAL threads
 * ---------------------------------------------------------------- */
void Scheduler::init()
{
    _timer_thread_ctx = rt_thread_create("ap_timer",
                                         _timer_thread_entry,
                                         this,
                                         8192,
                                         RT_THREAD_PRIORITY_MAX / 4,
                                         20);
    if (_timer_thread_ctx) {
        rt_thread_startup(_timer_thread_ctx);
    }

    _io_thread_ctx = rt_thread_create("ap_io",
                                      _io_thread_entry,
                                      this,
                                      8192,
                                      RT_THREAD_PRIORITY_MAX / 2,
                                      20);
    if (_io_thread_ctx) {
        rt_thread_startup(_io_thread_ctx);
    }

    _storage_thread_ctx = rt_thread_create("storage",
                                          _storage_thread_entry,
                                          nullptr,
                                          2048,
                                          RT_THREAD_PRIORITY_MAX / 2 + 2,
                                          20);
    if (_storage_thread_ctx != nullptr) {
        rt_thread_startup(_storage_thread_ctx);
    }
}

/* ----------------------------------------------------------------
 *  delay / delay_microseconds — matches ChibiOS semantics
 *
 *  delay() calls call_delay_cb() on every ms tick when in main
 *  thread, which triggers scheduler_delay_callback() → GCS comms.
 * ---------------------------------------------------------------- */
void Scheduler::delay(uint16_t ms)
{
    uint64_t start = AP_HAL::micros64();
    while ((AP_HAL::micros64() - start) / 1000 < ms) {
        delay_microseconds(1000);
        if (_min_delay_cb_ms <= ms) {
            if (in_main_thread()) {
                call_delay_cb();
            }
        }
    }
}

void Scheduler::delay_microseconds(uint16_t us)
{
    if (us == 0) {
        return;
    }
    rt_tick_t ticks = (rt_tick_t)((uint32_t)us * RT_TICK_PER_SECOND / 1000000U);
    if (ticks == 0) {
        ticks = 1;
    }
    rt_thread_delay(ticks);
}

/* ----------------------------------------------------------------
 *  Timer / IO process registration — same as ChibiOS
 * ---------------------------------------------------------------- */
void Scheduler::register_timer_process(AP_HAL::MemberProc proc)
{
    _timer_sem.take_blocking();
    for (uint8_t i = 0; i < _num_timer_procs; i++) {
        if (_timer_proc[i] == proc) {
            _timer_sem.give();
            return;
        }
    }
    if (_num_timer_procs < RTT_SCHEDULER_MAX_TIMER_PROCS) {
        _timer_proc[_num_timer_procs] = proc;
        _num_timer_procs++;
    }
    _timer_sem.give();
}

void Scheduler::register_io_process(AP_HAL::MemberProc proc)
{
    _io_sem.take_blocking();
    for (uint8_t i = 0; i < _num_io_procs; i++) {
        if (_io_proc[i] == proc) {
            _io_sem.give();
            return;
        }
    }
    if (_num_io_procs < RTT_SCHEDULER_MAX_IO_PROCS) {
        _io_proc[_num_io_procs] = proc;
        _num_io_procs++;
    }
    _io_sem.give();
}

void Scheduler::register_timer_failsafe(AP_HAL::Proc failsafe, uint32_t period_us)
{
    (void)period_us;
    _failsafe = failsafe;
}

/* ---------------------------------------------------------------- */
void Scheduler::reboot(bool hold_in_bootloader)
{
    (void)hold_in_bootloader;
    rt_thread_mdelay(100);
    rt_hw_cpu_reset();
    for (;;) {
        rt_thread_mdelay(1000);
    }
}

bool Scheduler::in_main_thread() const
{
    return _main_thread_id != nullptr && rt_thread_self() == _main_thread_id;
}

void Scheduler::set_system_initialized()
{
    if (_system_initialized) {
        AP_HAL::panic("PANIC: Scheduler::set_system_initialized called more than once");
    }
    _system_initialized = true;
}

bool Scheduler::is_system_initialized()
{
    return _system_initialized;
}

/* ----------------------------------------------------------------
 *  Internal: run registered callbacks
 * ---------------------------------------------------------------- */
void Scheduler::_run_timers()
{
    if (_in_timer_proc) {
        return;
    }
    _in_timer_proc = true;

    uint8_t num_procs = 0;
    _timer_sem.take_blocking();
    num_procs = _num_timer_procs;
    _timer_sem.give();

    for (uint8_t i = 0; i < num_procs; i++) {
        if (_timer_proc[i]) {
            _timer_proc[i]();
        }
    }
    if (_failsafe != nullptr) {
        _failsafe();
    }

    _in_timer_proc = false;
}

void Scheduler::_run_io()
{
    if (_in_io_proc) {
        return;
    }
    _in_io_proc = true;

    uint8_t num_procs = 0;
    _io_sem.take_blocking();
    num_procs = _num_io_procs;
    _io_sem.give();

    for (uint8_t i = 0; i < num_procs; i++) {
        if (_io_proc[i]) {
            _io_proc[i]();
        }
    }

#if AP_RCPROTOCOL_ENABLED
    AP::RC().update();
#endif

    _in_io_proc = false;
}

void Scheduler::expect_delay_ms(uint32_t ms)
{
    _expected_delay_ms = ms;
}
