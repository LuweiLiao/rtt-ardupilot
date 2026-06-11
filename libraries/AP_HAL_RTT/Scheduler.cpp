/*
 * AP_HAL_RTT Scheduler — mirrors ChibiOS HAL thread architecture.
 *
 * Threads created by init():
 *   ap_monitor : 10Hz — watchdog, stuck detection, GPIO timer_tick
 *   ap_timer   : 1kHz — _run_timers() + AnalogIn + failsafe
 *   ap_rcout   : 1kHz — RCOutput timer_tick (PWM/DShot push cycle)
 *   ap_rcin    : 1kHz — RCInput _timer_tick (RC protocol processing)
 *   ap_uart    : 1kHz — UART _timer_tick() (isolated from timer callbacks)
 *   ap_io      : 1kHz — _run_io() (registered IO callbacks)
 *   storage    : 1kHz — hal.storage->_timer_tick()
 *
 * GCS communication is driven entirely by the main thread through the
 * standard ArduPilot delay-callback mechanism:
 *   main thread delay() → call_delay_cb() → scheduler_delay_callback()
 *     → gcs().update_receive/send() + HEARTBEAT
 */

#include "AP_HAL_RTT/Scheduler.h"
#include "AP_HAL_RTT/UARTDriver.h"
#include "AP_HAL_RTT/HAL_RTT_Class.h"
#include "AP_HAL_RTT/AnalogIn.h"
#include "AP_HAL_RTT/RCOutput.h"
#include "AP_HAL_RTT/RCInput.h"
#include "AP_HAL_RTT/GPIO.h"
#include "hal_usb_lld_rtt.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>
#include <AP_InternalError/AP_InternalError.h>
#include <AP_Filesystem/AP_Filesystem.h>
#include <AP_Logger/AP_Logger.h>
#include <rtthread.h>
/* STM32F7 HAL/CMSIS registers for RCC/PWR/RTC backup domain access */
#include <stm32f7xx.h>

#include "rtt_dbg_bkp.h"

volatile uint32_t rtt_dbg_sw_reboot_count = 0;

#if HAL_WITH_IO_MCU
/* ch.h provides thread_t / chThdGetSelfX for AP_IOMCU's thread_main.
 * thread_create must allocate a thread_t wrapper and set rt_thread->user_data
 * so chThdGetSelfX() works inside threads created by the scheduler. */
#include <ch.h>
#endif

#if !defined(IOMCU_FW)
extern "C" void ap_rtt_iwdg_init(void);
#endif

using namespace RTT;

extern const AP_HAL::HAL& hal;

/* DWT registers for sub-tick busy-wait */
#define DWT_CTRL_REG   (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT_REG (*(volatile uint32_t *)0xE0001004)
#define SCB_DEMCR_REG  (*(volatile uint32_t *)0xE000EDFC)

Scheduler::Scheduler()
{
}

/* ----------------------------------------------------------------
 *  DWT-based busy-wait for short delays (≤ 200 µs).
 *  Safe for brief intervals: does not materially starve
 *  lower-priority threads. Earlier deadlocks were caused by
 *  multi-hundred-µs busy-waits preventing DeviceBus (IOC, UART)
 *  threads from running.  The 200 µs threshold was chosen
 *  by comparing ChibiOS (1 MHz timer, no busy-wait at all)
 *  against RTT (1 kHz, must busy-wait for sub-tick).
 * ---------------------------------------------------------------- */
extern "C" uint32_t SystemCoreClock;

static void _poll_usb_if_active(void);

void Scheduler::_delay_microseconds_dwt(uint16_t us)
{
    SCB_DEMCR_REG |= (1U << 24);
    DWT_CTRL_REG |= 1U;

    const uint32_t cycles = us * (SystemCoreClock / 1000000U);
    const uint32_t start = DWT_CYCCNT_REG;
    uint32_t poll_div = 0;
    while ((DWT_CYCCNT_REG - start) < cycles) {
        if (!usb_lld_is_configured_rtt() && ((++poll_div & 0x3FU) == 0U)) {
            _poll_usb_if_active();
        }
        /* spin — compiler barrier only, no DSB.
         * DSB stalls the ~14-cycle pipeline on every iteration
         * without improving wall-clock timing (which is governed
         * by the unsigned CYCCNT delta).  Removing DSB lets
         * pending interrupts fire between loop iterations,
         * improving interrupt latency during busy-wait. */
        asm volatile("" ::: "memory");
    }
}

/* ----------------------------------------------------------------
 *  Timer thread — equivalent to ChibiOS _timer_thread
 *  1 kHz: UART flush + registered timer callbacks + failsafe
 * ---------------------------------------------------------------- */
void Scheduler::_timer_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_hal_initialized) {
        rt_thread_mdelay(1);
    }

    while (true) {
        rt_thread_mdelay(1);

        sched->_run_timers();

        /* Feed IWDG only during expected delays — mirrors ChibiOS
         * _timer_thread at Scheduler.cpp:366-368:
         *   if (sched->in_expected_delay()) {
         *       sched->watchdog_pat();
         *   }
         * During normal operation the main loop calls watchdog_pat()
         * at the end of each iteration (HAL_RTT_Class.cpp:247).  If the
         * timer thread pats unconditionally it keeps last_watchdog_pat_ms
         * constantly updated, which defeats the monitor thread's 500ms
         * stuck-detection check at Scheduler.cpp:263.
         * Safe even before IWDG is started (0xAAAA to KR when off = no-op). */
        if (sched->in_expected_delay()) {
            sched->watchdog_pat();
        }
    }
}

/* ----------------------------------------------------------------
 *  RCOutput thread — equivalent to ChibiOS _rcout_thread
 *  1 kHz: pushes PWM/DShot output
 * ---------------------------------------------------------------- */
void Scheduler::_rcout_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_hal_initialized) {
        rt_thread_mdelay(1);
    }

    while (!sched->_initialized) {
        rt_thread_mdelay(1);
    }

    while (true) {
        /* PWM-only mode: 50Hz is sufficient since servos are 50Hz.
         * DShot would use event-driven timing like ChibiOS.
         * Use rt_thread_mdelay to yield CPU — DWT busy-loop starves
         * all lower-priority threads (main, UART, IO, storage). */
        rt_thread_mdelay(20);
        ((RCOutput *)hal.rcout)->timer_tick();
    }
}

/* ----------------------------------------------------------------
 *  RCInput thread — equivalent to ChibiOS _rcin_thread
 *  1 kHz: processes incoming RC frames
 * ---------------------------------------------------------------- */
void Scheduler::_rcin_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_hal_initialized) {
        rt_thread_mdelay(20);
    }

    while (true) {
        rt_thread_mdelay(1);
        ((RCInput *)hal.rcin)->_timer_tick();
    }
}

/* ----------------------------------------------------------------
 *  UART thread — separated from timer to avoid USB CDC blocking
 *  the timer callbacks (ChibiOS uses per-port threads)
 * ---------------------------------------------------------------- */
void Scheduler::_uart_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_hal_initialized) {
        rt_thread_mdelay(1);
    }

    while (true) {
        rt_thread_mdelay(1);
#if HAL_WITH_IO_MCU
        /*
         * [Cybernetics Ch.4] Closed-loop: service the IOMCU UART before the
         * general serial scan.  Under USB parameter pressure, IOMCU health is a
         * separate stability loop and must not wait behind CDC backlog work.
         */
        auto *iomcu_uart = get_rtt_iomcu_uart();
        if (iomcu_uart) {
            iomcu_uart->_timer_tick();
        }
#endif
        for (uint8_t i = 0; i < 10; i++) {
#if HAL_WITH_IO_MCU && defined(HAL_UART_IOMCU_IDX)
            if (i == HAL_UART_IOMCU_IDX && get_rtt_iomcu_uart() != nullptr) {
                continue;
            }
#endif
            auto *uart = (UARTDriver *)hal.serial(i);
            if (uart) {
                uart->_timer_tick();
            }
        }
    }
}

/* ----------------------------------------------------------------
 *  IO thread — equivalent to ChibiOS _io_thread
 *  1 kHz: registered IO callbacks + SD retry + stack check
 * ---------------------------------------------------------------- */
void Scheduler::_io_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_hal_initialized) {
        rt_thread_mdelay(1);
    }

#if HAL_LOGGING_ENABLED
    uint32_t last_sd_start_ms = AP_HAL::millis();
#endif
    uint32_t last_stack_check_ms = 0;

    while (true) {
        rt_thread_mdelay(1);

        sched->_run_io();

        uint32_t now = AP_HAL::millis();

#if HAL_LOGGING_ENABLED
        if (!hal.util->get_soft_armed()) {
            if (now - last_sd_start_ms > 3000) {
                last_sd_start_ms = now;
                AP::FS().retry_mount();
            }
        }
#endif

        if (now - last_stack_check_ms > 5000) {
            last_stack_check_ms = now;
            sched->_check_stack_free();
        }
    }
}

/* ----------------------------------------------------------------
 *  Storage thread — equivalent to ChibiOS _storage_thread
 * ---------------------------------------------------------------- */
void Scheduler::_storage_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_hal_initialized) {
        rt_thread_mdelay(10);
    }

    while (true) {
        rt_thread_mdelay(1);
        if (hal.storage != nullptr) {
            hal.storage->_timer_tick();
        }
    }
}

/* ----------------------------------------------------------------
 *  Monitor thread — equivalent to ChibiOS _monitor_thread
 *  10 Hz: watchdog, stuck thread detection, stack checks,
 *         GPIO timer_tick
 * ---------------------------------------------------------------- */
void Scheduler::_monitor_thread_entry(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;

    while (!sched->_initialized) {
        rt_thread_mdelay(100);
    }

    while (true) {
        rt_thread_mdelay(100);

        uint32_t now = AP_HAL::millis();
        uint32_t loop_delay = now - sched->last_watchdog_pat_ms;

        if (loop_delay >= 500 && !sched->in_expected_delay()) {
            AP::internalerror().error(AP_InternalError::error_t::main_loop_stuck,
                                     hal.util->persistent_data.semaphore_line);
        }

#ifndef IOMCU_FW
        hal.gpio->timer_tick();
#endif
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

uint8_t Scheduler::calculate_thread_priority(priority_base base, int8_t priority) const
{
    uint8_t prio = APM_RTT_IO_PRIORITY;
    static const struct {
        priority_base base;
        uint8_t p;
    } map[] = {
        { PRIORITY_BOOST,     APM_RTT_MAIN_BOOST },
        { PRIORITY_MAIN,      APM_RTT_MAIN_PRIORITY },
        { PRIORITY_SPI,       APM_RTT_SPI_PRIORITY },
        { PRIORITY_I2C,       APM_RTT_I2C_PRIORITY },
        { PRIORITY_CAN,       APM_RTT_I2C_PRIORITY },
        { PRIORITY_TIMER,     APM_RTT_TIMER_PRIORITY },
        { PRIORITY_RCOUT,     APM_RTT_RCOUT_PRIORITY },
        { PRIORITY_RCIN,      APM_RTT_RCIN_PRIORITY },
        { PRIORITY_LED,       APM_RTT_LED_PRIORITY },
        { PRIORITY_IO,        APM_RTT_IO_PRIORITY },
        { PRIORITY_UART,      APM_RTT_UART_PRIORITY },
        { PRIORITY_STORAGE,   APM_RTT_STORAGE_PRIORITY },
        { PRIORITY_SCRIPTING, APM_RTT_SCRIPTING_PRIORITY },
        { PRIORITY_NET,       APM_RTT_UART_PRIORITY },
    };
    for (uint8_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (map[i].base == base) {
            /* RT-Thread: lower number = higher priority; positive offset = higher priority */
            int16_t p = (int16_t)map[i].p - priority;
            prio = (uint8_t)constrain_int16(p, 1, RT_THREAD_PRIORITY_MAX - 2);
            break;
        }
    }
    return prio;
}

bool Scheduler::thread_create(AP_HAL::MemberProc proc, const char* name,
                              uint32_t stack_size, priority_base base, int8_t priority)
{
    auto *tproc = (AP_HAL::MemberProc *)malloc(sizeof(AP_HAL::MemberProc));
    if (!tproc) {
        return false;
    }
    *tproc = proc;

    const uint8_t rtt_prio = calculate_thread_priority(base, priority);

    if (stack_size < 1024) {
        stack_size = 1024;
    }

    rt_thread_t th = rt_thread_create(name, _thread_create_trampoline,
                                      tproc, stack_size, rtt_prio, 20);
    if (th != nullptr) {
#if HAL_WITH_IO_MCU
        /* Allocate a thread_t wrapper so chThdGetSelfX() works in this thread.
         * AP_IOMCU::thread_main uses chThdGetSelfX() and chEvtWaitAnyTimeout()
         * which need a valid thread_t with an embedded rt_event.  Threads not
         * created via chThdCreateStatic() must have user_data set manually. */
        auto *tp = (thread_t *)calloc(1, sizeof(thread_t));
        if (tp) {
            tp->rtt_thread = th;
            static int evt_seq = 0;
            char evt_name[RT_NAME_MAX];
            rt_snprintf(evt_name, sizeof(evt_name), "te%d", evt_seq++);
            rt_event_init(&tp->event, evt_name, RT_IPC_FLAG_PRIO);
            th->user_data = (uintptr_t)tp;
        }
#endif
        rt_thread_startup(th);
        return true;
    }
    free(tproc);
    return false;
}

/* ----------------------------------------------------------------
 *  init — create all HAL threads (mirrors ChibiOS Scheduler::init)
 *
 *  Thread creation order and priorities match ChibiOS:
 *    monitor > timer = rcout > rcin > (main boost) > (main normal)
 *            > uart = led > io > storage > scripting
 * ---------------------------------------------------------------- */
void Scheduler::init()
{
    _monitor_thread_ctx = rt_thread_create("ap_mon",
                                           _monitor_thread_entry,
                                           this, 1024,
                                           APM_RTT_MONITOR_PRIORITY, 20);
    if (_monitor_thread_ctx) rt_thread_startup(_monitor_thread_ctx);

    _timer_thread_ctx = rt_thread_create("ap_timer",
                                         _timer_thread_entry,
                                         this, 16384,
                                         APM_RTT_TIMER_PRIORITY, 20);
    if (_timer_thread_ctx) rt_thread_startup(_timer_thread_ctx);

    _rcout_thread_ctx = rt_thread_create("ap_rcout",
                                         _rcout_thread_entry,
                                         this, 512,
                                         APM_RTT_RCOUT_PRIORITY, 20);
    if (_rcout_thread_ctx) rt_thread_startup(_rcout_thread_ctx);

    _rcin_thread_ctx = rt_thread_create("ap_rcin",
                                        _rcin_thread_entry,
                                        this, 1024,
                                        APM_RTT_RCIN_PRIORITY, 20);
    if (_rcin_thread_ctx) rt_thread_startup(_rcin_thread_ctx);

    _uart_thread_ctx = rt_thread_create("ap_uart",
                                        _uart_thread_entry,
                                        this, 8192,
                                        APM_RTT_UART_PRIORITY, 20);
    if (_uart_thread_ctx) rt_thread_startup(_uart_thread_ctx);

    _io_thread_ctx = rt_thread_create("ap_io",
                                      _io_thread_entry,
                                      this, 8192,
                                      APM_RTT_IO_PRIORITY, 20);
    if (_io_thread_ctx) rt_thread_startup(_io_thread_ctx);

    _storage_thread_ctx = rt_thread_create("storage",
                                           _storage_thread_entry,
                                           this, 8192,
                                           APM_RTT_STORAGE_PRIORITY, 20);
    if (_storage_thread_ctx) rt_thread_startup(_storage_thread_ctx);

    /* _hal_initialized is set in _main_loop_entry() after the main
     * thread drops to startup priority — mirrors ChibiOS behaviour:
     * schedulerInstance.hal_initialized() is called at L273 of
     * HAL_ChibiOS_Class.cpp, after hal_chibios_set_priority(APM_STARTUP_PRIORITY)
     * at L265.  This ensures timer/SPI/UART threads only start running
     * AFTER the main thread is at low priority, preventing them from
     * starving the init process. */
}
/* ----------------------------------------------------------------
 *  delay / delay_microseconds — hybrid strategy
 *
 *  delay() calls call_delay_cb() on every ms tick when in main
 *  thread, which triggers scheduler_delay_callback() → GCS comms.
 *
 *  delay_microseconds():
 *    < 1 tick : DWT busy-wait (true sub-tick delay; avoids 100us -> 1tick inflation)
 *    >= 1 tick: sleep whole ticks, then busy-wait the remaining sub-tick tail
 * ---------------------------------------------------------------- */
/* Poll USB during setup delays so EP0 enumeration completes before main loop. */
static void _poll_usb_if_active(void)
{
    usb_lld_poll_rtt();
}

void Scheduler::delay(uint16_t ms)
{
    uint64_t start = AP_HAL::micros64();
    while ((AP_HAL::micros64() - start) / 1000 < ms) {
        _poll_usb_if_active();
        delay_microseconds(1000);
        if (_min_delay_cb_ms <= ms) {
            if (in_main_thread()) {
                const auto old_task = hal.util->persistent_data.scheduler_task;
                hal.util->persistent_data.scheduler_task = -4;
                call_delay_cb();
                hal.util->persistent_data.scheduler_task = old_task;
            }
        }
    }
}

void Scheduler::delay_microseconds(uint16_t us)
{
    if (us == 0) {
        return;
    }

    /*
     * Hybrid delay strategy informed by ChibiOS comparison
     * (t_62fa1d88 research handoff):
     *
     *   us ≤ 200 µs  →  DWT busy-wait + DSB  (short, acceptable CPU hog)
     *   us > 200 µs  →  rt_thread_delay()     (yield CPU to lower-priority threads)
     *
     * ChibiOS uses a 1 MHz system timer (1 tick = 1 µs) so it never needs
     * busy-wait — all delays go through chThdSleep().  RTT uses a 1 kHz
     * timer (1 tick = 1000 µs), so sub-tick delays must either busy-wait
     * or round up to 1 tick.  The 200 µs threshold balances precision
     * against scheduler fairness: short sensor delays (SPI tH, register
     * write settles, etc.) are kept cycle-accurate, while longer delays
     * yield so that IO / storage / UART threads can drain their work queues.
     *
     * Unlike the previous implementation we do NOT busy-wait a sub-tick
     * remainder after rt_thread_delay() — that would re-hog the CPU right
     * after yielding, defeating the purpose.  ChibiOS doesn't do it either.
     */
    const uint32_t tick_us = 1000000U / RT_TICK_PER_SECOND;

    if (us <= 200U) {
        _delay_microseconds_dwt(us);
        return;
    }

    _poll_usb_if_active();
    /* ≥200 µs: yield CPU.  us < tick_us (i.e. 201-999µs) rounds up to 1
     * tick (1000 µs), which is a necessary compromise given RTT's coarse
     * 1 kHz system timer.  The caller always has a micros64-based backup
     * for precise elapsed-time checks. */
    rt_thread_delay(MAX(1U, us / tick_us));
}

/* ----------------------------------------------------------------
 *  Priority boost — mirrors ChibiOS delay_microseconds_boost
 *
 *  Boosts main thread priority before wait_for_sample() to reduce
 *  jitter.  On RT-Thread we raise the thread's current priority.
 * ---------------------------------------------------------------- */
void Scheduler::delay_microseconds_boost(uint16_t us)
{
    if (!_priority_boosted && in_main_thread()) {
        rt_thread_t self = rt_thread_self();
        if (self) {
            rt_uint8_t boost_prio = (rt_uint8_t)APM_RTT_MAIN_BOOST;
            rt_thread_control(self, RT_THREAD_CTRL_CHANGE_PRIORITY, &boost_prio);
        }
        _priority_boosted = true;
        _called_boost = true;
    }
    /*
     * When boosted, yield to let DeviceBus threads (prio below boost)
     * dispatch IMU periodic callbacks (_poll_data) so FIFO data
     * accumulates.  Without this yield the DWT busy-wait loop in
     * delay_microseconds() starves all lower-priority threads,
     * wait_for_sample() never sees new data and the main loop hangs.
     *
     * ChibiOS delay_microseconds(100) -> chThdSleep(100) always yields.
     * RTT DWT busy-wait does not, so we need an explicit yield here.
     */
    if (in_main_thread() && _priority_boosted) {
        rt_thread_delay(1);
        /*
         * [Cybernetics Ch.4] Closed-loop: wait_for_sample() can hold the main
         * scheduler outside AP_Scheduler::run() for milliseconds while startup
         * sensor threads catch up.  ChibiOS can keep already-queued SerialUSB
         * buffers draining in that window; RTT also needs the MAVLink producer
         * to get a short delay-callback slot so MSG_NEXT_PARAM can refill USB.
         */
        call_delay_cb();
        _called_boost = true;
        return;
    }
    delay_microseconds(us);
}

bool Scheduler::check_called_boost(void)
{
    if (!_called_boost) {
        return false;
    }
    _called_boost = false;
    return true;
}

void Scheduler::boost_end(void)
{
    if (in_main_thread() && _priority_boosted) {
        _priority_boosted = false;
        rt_thread_t self = rt_thread_self();
        if (self) {
            rt_uint8_t normal_prio = (rt_uint8_t)APM_RTT_MAIN_PRIORITY;
            rt_thread_control(self, RT_THREAD_CTRL_CHANGE_PRIORITY, &normal_prio);
        }
    }
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
    hal.rcout->force_safety_on();

#if HAL_LOGGING_ENABLED
    if (AP_Logger::get_singleton()) {
        AP::logger().StopLogging();
    }
    AP::FS().unmount();
#endif

    /* Tell bootloader what to do after reset — mirrors ChibiOS
     * Scheduler.cpp:311  set_fast_reboot(hold_in_bootloader ? RTC_BOOT_HOLD : RTC_BOOT_FAST) */
    {
        rtt_dbg_bkp_enable_domain();
        RTC->BKP0R = hold_in_bootloader ? 0xb0070001UL     /* RTC_BOOT_HOLD */
                                        : 0xb0070002UL;    /* RTC_BOOT_FAST */
        __DSB();
    }

    rtt_dbg_sw_reboot_count++;
    rtt_dbg_bkp_stamp_sw_reboot();

    rt_hw_interrupt_disable();
    rt_hw_cpu_reset();
}

bool Scheduler::in_main_thread() const
{
    return _main_thread_id != nullptr && rt_thread_self() == _main_thread_id;
}

void Scheduler::set_system_initialized()
{
    if (_initialized) {
        AP_HAL::panic("PANIC: Scheduler::set_system_initialized called more than once");
    }
    _initialized = true;

    /* Reconfigure IWDG from ~10s (ap_rtt_iwdg_init early timeout) to ~2s
     * for normal operation.  IWDG is already running from early init;
     * we unlock PR/RLR, set tighter timeout, and wait for sync. */
#define IWDG_KR    (*(volatile uint32_t *)0x40003000)
#define IWDG_PR    (*(volatile uint32_t *)0x40003004)
#define IWDG_RLR   (*(volatile uint32_t *)0x40003008)
#define IWDG_SR    (*(volatile uint32_t *)0x4000300C)
    IWDG_KR = 0xAAAA;          /* Feed to extend counter before reconfig */
    IWDG_KR = 0x5555;          /* Unlock PR/RLR */
    IWDG_PR = 3;               /* prescaler /32 */
    IWDG_RLR = 2047;           /* ~2s timeout at LSI 32kHz */
    {
        volatile uint32_t iwdg_timeout = 1000000;
        while (IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {
            if (--iwdg_timeout == 0) {
                rt_kprintf("IWDG: SR sync timeout in set_system_initialized (SR=0x%08lx)\n",
                           (unsigned long)IWDG_SR);
                break;
            }
            __NOP();
        }
    }
    IWDG_KR = 0xAAAA;          /* Reload counter with new RLR value */
    _iwdg_started = true;
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

    ((AnalogIn *)hal.analogin)->_timer_tick();

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

    _in_io_proc = false;
}

/* ----------------------------------------------------------------
 *  expect_delay_ms — mirrors ChibiOS (nested, millis-window expiry)
 * ---------------------------------------------------------------- */
void Scheduler::expect_delay_ms(uint32_t ms)
{
    if (!in_main_thread()) {
        return;
    }

    if (ms == 0) {
        if (_expect_delay_nesting > 0) {
            _expect_delay_nesting--;
        }
        if (_expect_delay_nesting == 0) {
            _expect_delay_start = 0;
            _expect_delay_length = 0;
        }
        return;
    }

    uint32_t now = AP_HAL::millis();
    watchdog_pat();

    if (_expect_delay_nesting == 0) {
        _expect_delay_start = now;
        _expect_delay_length = ms;
    } else {
        uint32_t elapsed = now - _expect_delay_start;
        uint32_t remaining = (_expect_delay_length > elapsed) ? (_expect_delay_length - elapsed) : 0;
        if (ms > remaining) {
            _expect_delay_start = now;
            _expect_delay_length = ms;
        }
    }
    _expect_delay_nesting++;

    boost_end();
}

bool Scheduler::in_expected_delay() const
{
    if (!_initialized) {
        return true;
    }
    if (_expect_delay_start != 0) {
        uint32_t now = AP_HAL::millis();
        if (now - _expect_delay_start <= _expect_delay_length) {
            return true;
        }
    }
    return false;
}

/* ----------------------------------------------------------------
 *  disable_interrupts_save / restore_interrupts
 *  Mirrors ChibiOS chSysGetStatusAndLockX / chSysRestoreStatusX.
 *  On Cortex-M this saves PRIMASK and disables IRQs.
 * ---------------------------------------------------------------- */
void *Scheduler::disable_interrupts_save(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    return (void *)(uintptr_t)level;
}

void Scheduler::restore_interrupts(void *state)
{
    rt_hw_interrupt_enable((rt_base_t)(uintptr_t)state);
}

void Scheduler::watchdog_pat(void)
{
    last_watchdog_pat_ms = AP_HAL::millis();

    /* IWDG kick — only after IWDG has been configured by set_system_initialized() */
#if defined(HAL_BOARD_RTT) && !defined(IOMCU_FW)
    if (_iwdg_started) {
#define IWDG_KR_REG    (*(volatile uint32_t *)0x40003000)
        IWDG_KR_REG = 0xAAAA;
    }
#endif
}

/*
 * _check_stack_free — mirrors ChibiOS check_stack_free
 * Iterates RT-Thread threads and reports stack_overflow if < 64 bytes free.
 */
void Scheduler::_check_stack_free(void)
{
#ifdef RT_USING_OVERFLOW_CHECK
    const uint32_t min_stack = 64;

    struct rt_object_information *info =
        rt_object_get_information(RT_Object_Class_Thread);
    if (info == RT_NULL) return;

    rt_enter_critical();
    for (struct rt_list_node *node = info->object_list.next;
         node != &(info->object_list);
         node = node->next) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
        rt_thread_t thread = rt_list_entry(node, struct rt_thread, parent.list);
#pragma GCC diagnostic pop

        uint8_t *sp = (uint8_t *)thread->stack_addr;
        uint32_t free = 0;
        while (free < thread->stack_size && sp[free] == '#') {
            free++;
        }
        if (free < min_stack) {
#if AP_INTERNALERROR_ENABLED
            uint8_t prio = RT_SCHED_PRIV(thread).current_priority;
            AP::internalerror().error(AP_InternalError::error_t::stack_overflow, prio);
#endif
        }
    }
    rt_exit_critical();
#endif
}
