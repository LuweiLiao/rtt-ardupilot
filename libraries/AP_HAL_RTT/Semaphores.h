/*
 * AP_HAL_RTT — Semaphores (aligned with ChibiOS semantics)
 *
 * Semaphore  : rt_mutex wrapper (recursive, priority-inheritance)
 * BinarySemaphore: rt_sem wrapper (count=0/1, ISR-safe signal)
 */

#pragma once

#include <AP_HAL/Semaphores.h>
#include "HAL_RTT_Namespace.h"
#include <rtthread.h>

namespace RTT
{

class Semaphore : public AP_HAL::Semaphore
{
public:
    Semaphore();
    ~Semaphore();
    bool give() override;
    bool take(uint32_t timeout_ms) override WARN_IF_UNUSED;
    bool take_nonblocking() override WARN_IF_UNUSED;
    void take_blocking() override;

    bool check_owner(void);
    void assert_owner(void);

private:
    rt_mutex_t _mtx;
    void _ensure_mtx();
};

class BinarySemaphore : public AP_HAL::BinarySemaphore
{
public:
    BinarySemaphore(bool initial_state = false);
    ~BinarySemaphore();
    bool wait(uint32_t timeout_us) override WARN_IF_UNUSED;
    bool wait_blocking() override;
    void signal() override;
    void signal_ISR() override;

private:
    rt_sem_t _sem;
    bool _initial_state;
    void _ensure_sem();
};

} // namespace RTT
