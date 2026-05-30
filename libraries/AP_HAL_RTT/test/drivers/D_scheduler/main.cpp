/**
 * test_D_scheduler — AP_HAL Scheduler HAL smoke
 *
 * Exercises real AP_HAL::Scheduler: init(), hal_initialized(), timer proc
 * registration, and delay(). Verifies the 1 kHz ap_timer thread invokes
 * registered callbacks at least once.
 *
 * Does not call hal.run() or set_system_initialized() (RCOutput waits on
 * _initialized; monitor thread idle until then — acceptable for this gate).
 *
 * Build: scons --target=cuav_v5 --test=D_scheduler -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/utility/functor.h>
#include <AP_HAL_RTT/Scheduler.h>

extern "C" {
#include "test_runner.h"
}

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

class TimerSmoke {
public:
    void on_timer(void)
    {
        _count++;
    }

    uint32_t count(void) const
    {
        return _count;
    }

private:
    volatile uint32_t _count = 0;
};

static TimerSmoke timer_smoke;

static void step_scheduler_hal_smoke(void)
{
    TEST_STEP("Scheduler HAL smoke (timer proc + delay)");

    AP_HAL::Scheduler *sched = hal.scheduler;
    TEST_ASSERT(sched != nullptr, "hal.scheduler non-null");

    sched->register_timer_process(
        FUNCTOR_BIND(&timer_smoke, &TimerSmoke::on_timer, void));

    test_printf("    scheduler->init() (ap_timer + worker threads)\r\n");
    sched->init();

    test_printf("    scheduler->hal_initialized() (unblock ap_timer)\r\n");
    ((RTT::Scheduler *)sched)->hal_initialized();

    sched->expect_delay_ms(600);
    test_printf("    scheduler->delay(500)\r\n");
    sched->delay(500);

    const uint32_t n = timer_smoke.count();
    test_printf("    timer callback count=%lu\r\n", (unsigned long)n);
    TEST_ASSERT(n > 0, "timer proc invoked at least once");

    test_printf("    note: set_system_initialized() not called\r\n");
    test_printf("    note: RC threads started but idle until _initialized\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_SCHEDULER");

    step_scheduler_hal_smoke();

    TEST_DONE();
    return 0;
}
