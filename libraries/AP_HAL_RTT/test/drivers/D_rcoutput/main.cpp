/**
 * test_D_rcoutput — AP_HAL RCOutput HAL smoke (Batch C1)
 *
 * Exercises real AP_HAL::RCOutput via hal.rcout: init, enable_ch, set_freq,
 * write, read, read_last_sent, cork/push. Only channel 0 (FMU CH1) is enabled
 * with safe low pulse widths (1000–1200 µs max). Does not start hal.run().
 *
 * Runtime verifies last-written software state via read/read_last_sent. PWM pin
 * waveform is NOT verified (no scope / no propellers / no ESC load).
 *
 * Safety: bench with NO propellers; single channel; returns to 1000 µs before exit.
 *
 * Build: scons --target=cuav_v5 --test=D_rcoutput -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdint.h>

extern "C" {
#include "test_runner.h"
}

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

/* Only FMU channel 0 (physical CH1). Never enable multi-channel sweep on bench. */
static const uint8_t SMOKE_CH = 0;
static const uint16_t PWM_SAFE_MIN_US = 1000;
static const uint16_t PWM_BRIEF_1100_US = 1100;
static const uint16_t PWM_BRIEF_1200_US = 1200;
static const uint16_t PWM_DEFAULT_HZ = 50;

static void assert_pwm_state(uint16_t expect_us, const char *label)
{
    const uint16_t rd = hal.rcout->read(SMOKE_CH);
    const uint16_t last = hal.rcout->read_last_sent(SMOKE_CH);

    test_printf("    ch%u %s: read=%u read_last_sent=%u (expect %u)\r\n",
                (unsigned)SMOKE_CH, label, (unsigned)rd, (unsigned)last, (unsigned)expect_us);

    TEST_ASSERT(rd == expect_us, "read() last-written state");
    TEST_ASSERT(last == expect_us, "read_last_sent() last-written state");
}

static void step_rcoutput_hal_smoke(void)
{
    TEST_STEP("RCOutput HAL smoke (AP_HAL API)");

    test_printf("    safety: NO propellers; CH%u only; 1000–1200 us max\r\n",
                (unsigned)SMOKE_CH);
    test_printf("    note: PWM waveform NOT verified (no scope/ESC)\r\n");

    test_printf("    hal.scheduler->delay(200) before RCOutput init\r\n");
    hal.scheduler->delay(200);

    hal.rcout->init();
    test_printf("    rcout->init() ok\r\n");

#if HAL_WITH_IO_MCU
    test_printf("    HAL_WITH_IO_MCU=1 unexpected in this image\r\n");
    TEST_FAIL();
#else
    /* Disarmed default forces writes to 0; force_safety_off for software-path check only. */
    hal.rcout->force_safety_off();
    test_printf("    rcout->force_safety_off() (HAL_WITH_IO_MCU=0, bench only)\r\n");
#endif

    hal.rcout->enable_ch(SMOKE_CH);
    test_printf("    rcout->enable_ch(%u) ok\r\n", (unsigned)SMOKE_CH);

    const uint32_t chmask = (1U << SMOKE_CH);
    hal.rcout->set_freq(chmask, PWM_DEFAULT_HZ);
    const uint16_t freq = hal.rcout->get_freq(SMOKE_CH);
    test_printf("    set_freq(%u Hz) get_freq=%u\r\n",
                (unsigned)PWM_DEFAULT_HZ, (unsigned)freq);
    TEST_ASSERT(freq == PWM_DEFAULT_HZ, "get_freq after set_freq");

    hal.rcout->write(SMOKE_CH, PWM_SAFE_MIN_US);
    assert_pwm_state(PWM_SAFE_MIN_US, "baseline 1000us");

    hal.scheduler->delay(10);

    hal.rcout->write(SMOKE_CH, PWM_BRIEF_1100_US);
    assert_pwm_state(PWM_BRIEF_1100_US, "brief 1100us");

    hal.scheduler->delay(10);

    hal.rcout->cork();
    hal.rcout->write(SMOKE_CH, PWM_BRIEF_1200_US);
    hal.rcout->push();
    assert_pwm_state(PWM_BRIEF_1200_US, "cork/push 1200us");

    hal.rcout->write(SMOKE_CH, PWM_SAFE_MIN_US);
    assert_pwm_state(PWM_SAFE_MIN_US, "restore 1000us");

    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");
    test_printf("    note: oscilloscope / ESC pulse check is out of scope\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_RCOUTPUT");

    step_rcoutput_hal_smoke();

    TEST_DONE();
    return 0;
}
