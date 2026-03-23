/*
 * ArduPilot + RT-Thread HAL - Semaphores (rt_sem wrappers).
 * 需链接 librtthread。
 */

#include "AP_HAL_RTT/Semaphores.h"
#include <rtthread.h>

using namespace RTT;

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
    rt_int32_t tick = (timeout_ms == 0) ? RT_WAITING_FOREVER : (rt_int32_t)rt_tick_from_millisecond(timeout_ms);
    return rt_mutex_take(_mtx, tick) == RT_EOK;
}

bool Semaphore::take_nonblocking()
{
    if (_mtx == nullptr) return false;
    return rt_mutex_take(_mtx, 0) == RT_EOK;
}

void Semaphore::take_blocking()
{
    IGNORE_RETURN(take(0));
}

BinarySemaphore::BinarySemaphore(bool initial_state)
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
    rt_int32_t tick = (timeout_us == 0) ? RT_WAITING_FOREVER : (rt_int32_t)rt_tick_from_millisecond((timeout_us + 999) / 1000);
    return rt_sem_take(_sem, tick) == RT_EOK;
}

bool BinarySemaphore::wait_blocking()
{
    return wait(0);
}

void BinarySemaphore::signal()
{
    if (_sem != nullptr) rt_sem_release(_sem);
}
