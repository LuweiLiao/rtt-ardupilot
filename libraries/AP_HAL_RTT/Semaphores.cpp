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
 * --------------------------------------------------------------- */

Semaphore::Semaphore()
{
    _mtx = rt_mutex_create("hal_mtx", RT_IPC_FLAG_PRIO);
}

Semaphore::~Semaphore()
{
    if (_mtx != nullptr) {
        rt_mutex_delete(_mtx);
        _mtx = nullptr;
    }
}

bool Semaphore::give()
{
    if (_mtx == nullptr) return false;
    return rt_mutex_release(_mtx) == RT_EOK;
}

bool Semaphore::take(uint32_t timeout_ms)
{
    if (_mtx == nullptr) return false;

    /* HAL_SEMAPHORE_BLOCK_FOREVER == 0 in ArduPilot.
     * ChibiOS: take(0) blocks forever.  We match that. */
    if (timeout_ms == HAL_SEMAPHORE_BLOCK_FOREVER) {
        return rt_mutex_take(_mtx, RT_WAITING_FOREVER) == RT_EOK;
    }

    if (take_nonblocking()) {
        return true;
    }

    uint64_t start = AP_HAL::micros64();
    do {
        rt_thread_mdelay(1);
        if (take_nonblocking()) {
            return true;
        }
    } while ((AP_HAL::micros64() - start) < (uint64_t)timeout_ms * 1000);

    return false;
}

bool Semaphore::take_nonblocking()
{
    if (_mtx == nullptr) return false;
    return rt_mutex_take(_mtx, 0) == RT_EOK;
}

void Semaphore::take_blocking()
{
    if (_mtx == nullptr) return;
    rt_mutex_take(_mtx, RT_WAITING_FOREVER);
}

bool Semaphore::check_owner(void)
{
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
    : AP_HAL::BinarySemaphore(initial_state)
{
    _sem = rt_sem_create("hal_bsem", initial_state ? 1 : 0, RT_IPC_FLAG_PRIO);
}

BinarySemaphore::~BinarySemaphore()
{
    if (_sem != nullptr) {
        rt_sem_delete(_sem);
        _sem = nullptr;
    }
}

bool BinarySemaphore::wait(uint32_t timeout_us)
{
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
    if (_sem == nullptr) return false;
    return rt_sem_take(_sem, RT_WAITING_FOREVER) == RT_EOK;
}

void BinarySemaphore::signal()
{
    if (_sem != nullptr) {
        rt_sem_release(_sem);
    }
}

void BinarySemaphore::signal_ISR()
{
    if (_sem != nullptr) {
        /* rt_sem_release() is ISR-safe in RT-Thread when called
         * with interrupts disabled or from ISR context */
        rt_base_t level = rt_hw_interrupt_disable();
        rt_sem_release(_sem);
        rt_hw_interrupt_enable(level);
    }
}
