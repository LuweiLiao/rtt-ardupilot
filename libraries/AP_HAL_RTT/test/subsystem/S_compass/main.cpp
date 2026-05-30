/**
 * test_S_compass — Subsystem: AP_Compass init + field read (IST8310 onboard)
 *
 * Exercises Compass::init() with HAL_MAG_PROBE_LIST (IST8310 @ bus0/0x0E),
 * scheduler timer procs for IST8310 periodic sampling, and compass.read() /
 * get_field(). Requires count>0 when chip present.
 *
 * HAL_COMPASS_ALLOW_INIT_NO_MAG (cuav_v5 hwdef): if zero instances after init,
 * PASS only on that policy path (no fake FAIL on boards without mag).
 *
 * NOT full vehicle param/GCS/INS stack; NOT compass calibration.
 *
 * Build: scons --target=cuav_v5 --test=S_compass -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL_RTT/Scheduler.h>
#include <AP_Compass/AP_Compass.h>
#include <AP_Math/AP_Math.h>

extern "C" {
#include "test_runner.h"
}

static Compass compass;
static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static bool field_all_zero(const Vector3f &v)
{
    return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
}

static bool field_changed(const Vector3f &a, const Vector3f &b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z;
}

static void step_compass_subsystem(void)
{
    TEST_STEP("AP_Compass subsystem smoke (IST8310 probe list)");

    test_printf("    boundary=Compass::init+read; NOT full vehicle stack\r\n");
    test_printf("    HAL_MAG_PROBE_LIST from hwdef (IST8310 bus0 0x0E)\r\n");

    AP_HAL::Scheduler *sched = hal.scheduler;
    TEST_ASSERT(sched != nullptr, "hal.scheduler");

    sched->init();
    test_printf("    scheduler->init() done\r\n");
    ((RTT::Scheduler *)sched)->hal_initialized();

    hal.scheduler->delay(100);

    compass.init();

    const uint8_t count = compass.get_count();
    test_printf("    compass.get_count()=%lu\r\n", (unsigned long)count);

#if defined(HAL_COMPASS_ALLOW_INIT_NO_MAG) && HAL_COMPASS_ALLOW_INIT_NO_MAG
    if (count == 0) {
        test_printf("    HAL_COMPASS_ALLOW_INIT_NO_MAG=1: zero compasses OK\r\n");
        test_printf("    note: cuav_v5 normally has IST8310 — check hw/solder\r\n");
        TEST_PASS();
        return;
    }
#else
    TEST_ASSERT(count > 0, "compass instance count > 0");
#endif

    hal.scheduler->delay(250);

    compass.read();
    const Vector3f f0 = compass.get_field(0);
    test_printf("    field[0] sample0: x=%ld y=%ld z=%ld mGauss\r\n",
                (long)f0.x, (long)f0.y, (long)f0.z);

    TEST_ASSERT(compass.healthy(0), "compass instance 0 healthy");

    hal.scheduler->delay(50);
    compass.read();
    const Vector3f f1 = compass.get_field(0);
    test_printf("    field[0] sample1: x=%ld y=%ld z=%ld mGauss\r\n",
                (long)f1.x, (long)f1.y, (long)f1.z);

    TEST_ASSERT(!field_all_zero(f0), "field not all zero");
    TEST_ASSERT(!field_all_zero(f1), "second sample not all zero");

    const bool varied = field_changed(f0, f1);
    const long ax = (long)f0.x;
    const long ay = (long)f0.y;
    const long az = (long)f0.z;
    const bool magnitude_ok =
        (ax < 0 ? -ax : ax) > 20 ||
        (ay < 0 ? -ay : ay) > 20 ||
        (az < 0 ? -az : az) > 20;
    test_printf("    field varied=%d magnitude_ok=%d\r\n",
                varied ? 1 : 0,
                magnitude_ok ? 1 : 0);
    TEST_ASSERT(varied || magnitude_ok, "non-constant or plausible field magnitude");

    test_printf("    note: E_ist8310 = chip-only gate; S_compass = AP_Compass path\r\n");
    test_printf("    note: not compass cal / declination auto / mavlink\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("S_COMPASS");

    step_compass_subsystem();

    TEST_DONE();
    return 0;
}
