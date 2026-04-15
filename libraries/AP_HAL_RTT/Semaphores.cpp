/*
 * AP_HAL_RTT — Semaphores (aligned with ChibiOS semantics)
 *
 * Key semantics (matching ChibiOS + base class):
 *   take(0) == take(HAL_SEMAPHORE_BLOCK_FOREVER) == block forever
 *   take_nonblocking() == try once, no wait
 *   wait(0) == non-blocking try (BinarySemaphore)
 *   signal_ISR() — safe to call from ISR context
 */

#include "AP_HAL_RTT/Semaphores.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>

using namespace RTT;

/* ---------------------------------------------------------------
 *  Semaphore (Mutex wrapper)
 *
 *  Lazy init: C++ global constructors run BEFORE the RTT heap is
 *  initialised (rt_system_heap_init is called from main→rtthread_startup).
 *  We defer rt_mutex_create / rt_sem_create until first actual use.
 * --------------------------------------------------------------- */

static bool _rtt_heap_ready()
{
    /* Simple check: if we're running in a thread context (not during early
     * C++ constructors), rt_thread_self() returns non-NULL.
     * During __libc_init_array (before main), the scheduler hasn't started
     * and rt_thread_self() returns NULL. */
    return rt_thread_self() != RT_NULL;
}

Semaphore::Semaphore()
{
    _mtx = nullptr;
}

Semaphore::~Semaphore()
{
    if (_mtx != nullptr) {
        rt_mutex_delete(_mtx);
        _mtx = nullptr;
    }
}

void Semaphore::_ensure_mtx()
{
    if (_mtx == nullptr) {
        if (!_rtt_heap_ready()) {
            /* Heap not ready yet - this should not happen during normal operation
             * but may occur during early C++ constructors. We'll retry later. */
            return;
        }
        static uint16_t idx = 0;
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "hm%u", idx++);
        _mtx = rt_mutex_create(name, RT_IPC_FLAG_PRIO);
        if (_mtx == nullptr) {
            rt_kprintf("Semaphore: FAILED to create mutex '%s'\n", name);
        }
    }
}

bool Semaphore::give()
{
    _ensure_mtx();
    if (_mtx == nullptr) return false;
    return rt_mutex_release(_mtx) == RT_EOK;
}

bool Semaphore::take(uint32_t timeout_ms)
{
    _ensure_mtx();
    if (_mtx == nullptr) return false;

    /* HAL_SEMAPHORE_BLOCK_FOREVER == 0 in ArduPilot.
     * ChibiOS: take(0) blocks forever.  We match that. */
    if (timeout_ms == HAL_SEMAPHORE_BLOCK_FOREVER) {
        return rt_mutex_take(_mtx, RT_WAITING_FOREVER) == RT_EOK;
    }

    /* Use native rt_mutex_take with tick timeout instead of polling.
     * The old mdelay(1)+polling pattern wasted CPU cycles and could
     * starve lower-priority threads. */
    rt_tick_t ticks = (rt_tick_t)((uint64_t)timeout_ms * RT_TICK_PER_SECOND / 1000U);
    if (ticks == 0) ticks = 1;
    return rt_mutex_take(_mtx, ticks) == RT_EOK;
}

bool Semaphore::take_nonblocking()
{
    _ensure_mtx();
    if (_mtx == nullptr) return false;
    return rt_mutex_take(_mtx, 0) == RT_EOK;
}

void Semaphore::take_blocking()
{
    _ensure_mtx();
    if (_mtx == nullptr) return;
    rt_mutex_take(_mtx, RT_WAITING_FOREVER);
}

bool Semaphore::check_owner(void)
{
    _ensure_mtx();
    if (_mtx == nullptr) return false;
    return _mtx->owner == rt_thread_self();
}

void Semaphore::assert_owner(void)
{
    if (!check_owner()) {
        AP_HAL::panic("semaphore owner check failed");
    }
}

/* ---------------------------------------------------------------
 *  BinarySemaphore (counting semaphore limited to 0/1)
 * --------------------------------------------------------------- */

BinarySemaphore::BinarySemaphore(bool initial_state)
    : AP_HAL::BinarySemaphore(initial_state), _initial_state(initial_state)
{
    _sem = nullptr;
}

BinarySemaphore::~BinarySemaphore()
{
    if (_sem != nullptr) {
        rt_sem_delete(_sem);
        _sem = nullptr;
    }
}

void BinarySemaphore::_ensure_sem()
{
    if (_sem == nullptr && _rtt_heap_ready()) {
        static uint16_t idx = 0;
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "hb%u", idx++);
        _sem = rt_sem_create(name, _initial_state ? 1 : 0, RT_IPC_FLAG_PRIO);
    }
}

bool BinarySemaphore::wait(uint32_t timeout_us)
{
    _ensure_sem();
    if (_sem == nullptr) return false;

    if (timeout_us == 0) {
        return rt_sem_take(_sem, 0) == RT_EOK;
    }

    /* Loop in 60ms chunks to handle 16-bit timer overflow on some platforms */
    while (timeout_us > 0) {
        const uint32_t us = (timeout_us > 60000U) ? 60000U : timeout_us;
        rt_tick_t ticks = (rt_tick_t)((uint64_t)us * RT_TICK_PER_SECOND / 1000000U);
        if (ticks == 0) ticks = 1;
        if (rt_sem_take(_sem, ticks) == RT_EOK) {
            return true;
        }
        timeout_us -= us;
    }
    return false;
}

bool BinarySemaphore::wait_blocking()
{
    _ensure_sem();
    if (_sem == nullptr) return false;
    return rt_sem_take(_sem, RT_WAITING_FOREVER) == RT_EOK;
}

void BinarySemaphore::signal()
{
    _ensure_sem();
    if (_sem != nullptr) {
        rt_sem_release(_sem);
    }
}

void BinarySemaphore::signal_ISR()
{
    /* MUST NOT call _ensure_sem() here — it invokes rt_thread_self()
     * and potentially rt_sem_create(), neither of which is ISR-safe.
     * If _sem hasn't been created yet, the signal is lost (acceptable:
     * ISR should only fire after driver init completes). */
    if (_sem != nullptr) {
        rt_base_t level = rt_hw_interrupt_disable();
        rt_sem_release(_sem);
        rt_hw_interrupt_enable(level);
    }
}
