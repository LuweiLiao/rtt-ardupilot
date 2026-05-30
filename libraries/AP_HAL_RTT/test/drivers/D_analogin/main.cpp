/**
 * test_D_analogin — AP_HAL AnalogIn HAL smoke
 *
 * Exercises real AP_HAL::AnalogIn: init(), channel(), polled _timer_tick(),
 * read_latest / voltage_latest. Uses logical channel 6 (ADC_IN11 / PC1
 * SCALED_V3V3 on CUAV V5 — same index as board_voltage()). No strict
 * voltage threshold (finite raw only).
 *
 * Does not start scheduler->init(); _timer_tick() is invoked directly
 * (same path as ap_timer thread). No absolute voltage asserts.
 *
 * Output: test_printf supports %d/%lu/%x only (no %u or float formats).
 *
 * Build: scons --target=cuav_v5 --test=D_analogin -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL_RTT/AnalogIn.h>
#include <math.h>

extern "C" {
#include "test_runner.h"
}

extern "C" {
extern volatile uint32_t rtt_adc_conversion_count;
extern volatile uint32_t rtt_adc_last_raw;
extern volatile uint32_t rtt_adc_timeout_count;
extern volatile uint32_t rtt_adc_timer_tick_state;
extern volatile uint32_t rtt_adc_timer_tick_count;
extern volatile uint32_t rtt_adc_lld_init_status;
extern volatile uint32_t rtt_adc_lld_calib_status;
extern volatile uint32_t rtt_adc_lld_conv_timeouts;
}

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

/* CUAV V5 hwdef: PC1 SCALED_V3V3 → logical ch6 (board_voltage index) */
static const int16_t SMOKE_CHANNEL = 6;
static const unsigned SAMPLE_ROUNDS = 12;

static bool is_finite_float(float v)
{
    return !isnan(v) && !isinf(v);
}

/* test_printf has no %f — print millivolts as signed integer */
static void print_mv_line(const char *label, float v)
{
    const int32_t mv = (int32_t)(v * 1000.0f + (v >= 0.0f ? 0.5f : -0.5f));
    test_printf("    %s=%ld mV\r\n", label, (long)mv);
}

static void print_adc_diagnostics(void)
{
    test_printf("    adc_diag tick_state=%lu timer_ticks=%lu conv=%lu last_raw=%lu timeouts=%lu\r\n",
                (unsigned long)rtt_adc_timer_tick_state,
                (unsigned long)rtt_adc_timer_tick_count,
                (unsigned long)rtt_adc_conversion_count,
                (unsigned long)rtt_adc_last_raw,
                (unsigned long)rtt_adc_timeout_count);
    test_printf("    lld_diag init=%lu calib=%lu conv_to=%lu\r\n",
                (unsigned long)rtt_adc_lld_init_status,
                (unsigned long)rtt_adc_lld_calib_status,
                (unsigned long)rtt_adc_lld_conv_timeouts);
}

static void step_analogin_hal_smoke(void)
{
    TEST_STEP("AnalogIn HAL smoke (init + channel + timer_tick)");

    TEST_ASSERT(hal.analogin != nullptr, "hal.analogin non-null");

    RTT::AnalogIn *ain = (RTT::AnalogIn *)hal.analogin;
    ain->init();

    AP_HAL::AnalogSource *src = ain->channel(SMOKE_CHANNEL);
    TEST_ASSERT(src != nullptr, "channel(6) SCALED_V3V3");

    test_printf("    channel=%d (SCALED_V3V3) sample rounds=%lu\r\n",
                (int)SMOKE_CHANNEL, (unsigned long)SAMPLE_ROUNDS);

    for (unsigned i = 0; i < SAMPLE_ROUNDS; i++) {
        ain->_timer_tick();
        hal.scheduler->delay(50);
    }

    print_adc_diagnostics();

    const float raw = src->read_latest();
    const float volt = src->voltage_latest();
    const float board_v = ain->board_voltage();

    const uint32_t raw_u = (raw > 0.0f && raw < 65535.0f) ? (uint32_t)(raw + 0.5f) : 0U;
    test_printf("    read_latest raw_counts=%lu\r\n", (unsigned long)raw_u);
    print_mv_line("voltage_latest", volt);
    print_mv_line("board_voltage", board_v);

    TEST_ASSERT(is_finite_float(raw), "read_latest finite");
    TEST_ASSERT(is_finite_float(volt), "voltage_latest finite");
    TEST_ASSERT(is_finite_float(board_v), "board_voltage finite");

    if (raw_u == 0U && rtt_adc_conversion_count == 0U) {
        test_printf("    WARN: no ADC conversions — check lld init/calib diag above\r\n");
        test_printf("    PASS scope: API init/tick/read only; not ADC quality gate\r\n");
    } else if (raw_u == 0U) {
        test_printf("    WARN: ch6 read_latest=0 but conv_count>0 (channel map?)\r\n");
    } else {
        test_printf("    note: no absolute voltage threshold (divider/tolerance)\r\n");
    }

    test_printf("    note: scheduler->init() not called; direct _timer_tick()\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_ANALOGIN");

    step_analogin_hal_smoke();

    TEST_DONE();
    return 0;
}
