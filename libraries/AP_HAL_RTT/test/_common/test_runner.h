#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

/**
 * test_runner.h — Shared test framework for RTT layered verification tests.
 *
 * Each test is an independent RT-Thread application under hwdef/common/tests/test_LX_<name>/.
 * Output goes through UART7 (CMSIS direct write) AND rt_kprintf for dual-path verification.
 *
 * Usage:
 *   #include "test_runner.h"
 *   
 *   int main(void)
 *   {
 *       TEST_INIT("L0_SYSTEM");
 *       
 *       TEST_STEP("SysTick frequency");
 *       // ... test code ...
 *       TEST_PASS();
 *       // or TEST_FAIL("expected X but got Y");
 *
 *       TEST_ASSERT(tick > 0, "SysTick should be running");
 *
 *       TEST_DONE();
 *       return 0;
 *   }
 */

#include <stdint.h>
#include <stdbool.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  Debug Markers (BSS section — auto-zeroed on boot, safe from stale data)
 * ================================================================ */
extern volatile uint32_t test_runner_initialized;
extern volatile uint32_t test_current_layer;
extern volatile uint32_t test_status;       /* 0=running, 1=PASS, 2=FAIL */
extern volatile uint32_t test_step_index;
extern volatile uint32_t test_fail_count;

/* ================================================================
 *  UART7 CMSIS Direct Output (works before/without RT-Thread console)
 * ================================================================ */
void     test_uart7_init(void);
void     test_uart7_write(const char *s);
void     test_printf(const char *fmt, ...);  /* UART7 formatted print */

/* ================================================================
 *  Test Runner API
 * ================================================================ */

/**
 * TEST_INIT(layer_name) — Call once at start of main().
 * Initializes UART7, prints banner.
 */
#define TEST_INIT(name)    _test_init(name, __LINE__)

/**
 * TEST_STEP(description) — Begin a new test step.
 */
#define TEST_STEP(desc)    _test_step(desc, __LINE__)

/**
 * TEST_PASS() — Mark current step as passed.
 */
#define TEST_PASS()        _test_result(true,  __LINE__, NULL)

/**
 * TEST_FAIL(msg) — Mark current step as failed with message.
 */
#define TEST_FAIL(msg)     _test_result(false, __LINE__, msg)

/**
 * TEST_ASSERT(cond, msg) — Check condition, fail if false.
 */
#define TEST_ASSERT(cond, msg)  do {                        \
    if (!(cond)) {                                          \
        _test_result(false, __LINE__, "ASSERT: " msg);      \
    }                                                       \
} while(0)

/**
 * TEST_DONE() — Print summary and halt.
 */
#define TEST_DONE()        _test_done()

/* Internal functions (use macros above instead) */
void _test_init(const char *name, int line);
void _test_step(const char *desc, int line);
void _test_result(bool pass, int line, const char *msg);
void _test_done(void);

/* ================================================================
 *  Utility: read CPUID
 * ================================================================ */
static inline uint32_t test_read_cpuid(void)
{
    return *(volatile uint32_t *)0xE000ED00;
}

/* ================================================================
 *  Utility: read SysTick control/status
 * ================================================================ */
#define STK_CSR  (*(volatile uint32_t *)0xE000E010)
#define STK_RVR  (*(volatile uint32_t *)0xE000E014)
#define STK_CVR  (*(volatile uint32_t *)0xE000E018)

static inline uint32_t test_read_systick_csr(void) { return STK_CSR; }
static inline uint32_t test_read_systick_rvr(void) { return STK_RVR; }
static inline uint32_t test_read_systick_cvr(void) { return STK_CVR; }

/* ================================================================
 *  Utility: Cortex-M7 aux control (for cache/FPU checks)
 * ================================================================ */
#define CPACR  (*(volatile uint32_t *)0xE000ED88)

static inline bool test_fpu_enabled(void)
{
    return (CPACR & 0x00F00000) == 0x00F00000;
}

#ifdef __cplusplus
}
#endif

#endif /* TEST_RUNNER_H */
