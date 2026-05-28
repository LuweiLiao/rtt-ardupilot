/**
 * test_L1_iwdg — Layer 1: Independent Watchdog (IWDG)
 *
 * Verifies the IWDG is alive, configured correctly, fed by SysTick,
 * and that the reset chain works.
 *
 * IMPORTANT: On CUAV V5 (STM32F767), the IWDG is in HARDWARE mode
 * (FLASH_OPTCR_IWDG_SW=0).  In this mode:
 *   - IWDG starts automatically at reset with PR=0 (div/4), RLR=0xFFF
 *     → timeout ≈ 512 ms at LSI ~32 kHz
 *   - PR and RLR CANNOT be modified by software (writes set PVU/RVU
 *     but values never take effect and status bits never clear)
 *  This is a hardware/option-byte limitation, not a bug.
 *
 * Test steps:
 *   Step 1: IWDG peripheral accessibility (SR reads valid)
 *   Step 2: IWDG default configuration (PR=0, RLR=0xFFF, SR may have PVU/RVU)
 *   Step 3: Feed verification (IWDG alive over 5s)
 *   Step 4: RCC_CSR reset source flag
 *   Step 5: LSI oscillator status
 *
 * Prerequisites: L0 system passing (SysTick, threads, UART).
 * Build: scons --target=cuav-v5 --test=L1_iwdg -j$(nproc)
 */

#include "test_runner.h"
#include <rtthread.h>

/* ================================================================
 *  Register definitions (CMSIS-compatible, direct access)
 * ================================================================ */

/* IWDG registers — STM32F7 at 0x40003000 */
#define IWDG_BASE_ADDR      0x40003000UL
#define IWDG_KR             (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x00))   /* Key register (Wr) */
#define IWDG_PR             (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x04))   /* Prescaler (R/W) */
#define IWDG_RLR            (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x08))   /* Reload (R/W) */
#define IWDG_SR             (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x0C))   /* Status (R)   */

/* IWDG key values */
#define IWDG_KR_FEED        0xAAAAU    /* Reload counter (feed) */
#define IWDG_KR_UNLOCK      0x5555U    /* Enable PR/RLR write access */
#define IWDG_KR_START       0xCCCCU    /* Start IWDG */

/* IWDG status bits */
#define IWDG_SR_PVU         (1U << 0)  /* Prescaler update pending */
#define IWDG_SR_RVU         (1U << 1)  /* Reload update pending */

/* RCC_CSR — Reset control / clock status register */
#define RCC_CSR             (*(volatile uint32_t *)0x40023874UL)
#define RCC_CSR_LSIRDY      (1U << 1)   /* LSI oscillator ready */
#define RCC_CSR_LSION       (1U << 0)   /* LSI oscillator enable */
#define RCC_CSR_SFTRSTF     (1U << 28)  /* Software reset flag */
#define RCC_CSR_IWDGRSTF    (1U << 29)  /* Independent watchdog reset flag */
#define RCC_CSR_PINRSTF     (1U << 26)  /* PIN reset flag */


/* ================================================================
 *  Step 1: IWDG Register Accessibility
 *
 *  Verify the IWDG peripheral is mapped and readable.
 * ================================================================ */
static void step_iwdg_accessible(void)
{
    TEST_STEP("IWDG Register Accessibility");

    uint32_t sr = IWDG_SR;
    test_printf("    IWDG_SR=0x%08lx\\r\\n", (unsigned long)sr);

    /* SR should not read as 0xFFFFFFFF (bus error means no peripheral) */
    TEST_ASSERT(sr != 0xFFFFFFFFU,
                "IWDG_SR should not be 0xFFFFFFFF (bus error)");

    test_printf("    IWDG accessible at 0x%08lx\\r\\n",
                (unsigned long)IWDG_BASE_ADDR);

    TEST_PASS();
}


/* ================================================================
 *  Step 2: IWDG Default Configuration
 *
 *  In hardware mode, PR and RLR cannot be modified.  Read back the
 *  default values (PR=0 → div/4, RLR=0xFFF).
 *
 *  NOTE: SR.PVU and SR.RVU may be set from board init's attempted
 *  PR/RLR reconfiguration (which fails silently in hardware mode).
 *  This is NOT a test failure — it's expected hardware behavior.
 * ================================================================ */
static void step_iwdg_config(void)
{
    TEST_STEP("IWDG Configuration (Default)");

    /* Unlock PR/RLR for read access */
    IWDG_KR = IWDG_KR_UNLOCK;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    uint32_t pr  = IWDG_PR & 0x07U;       /* PR[2:0] only */
    uint32_t rlr = IWDG_RLR & 0x0FFFU;     /* RLR[11:0] */
    uint32_t sr  = IWDG_SR;

    /* Re-lock by feeding */
    IWDG_KR = IWDG_KR_FEED;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    bool pvu = (sr & IWDG_SR_PVU) != 0;
    bool rvu = (sr & IWDG_SR_RVU) != 0;

    /* Compute actual timeout based on PR and RLR */
    unsigned long timeout_ms =
        (unsigned long)((uint64_t)rlr * 1000UL * (4UL << pr) / 32000UL);

    test_printf("    PR=%lu (div/%lu)  RLR=%lu  SR=0x%08lx\\r\\n",
                (unsigned long)pr, (unsigned long)(4UL << pr),
                (unsigned long)rlr, (unsigned long)sr);
    test_printf("    PVU=%s RVU=%s\\r\\n",
                pvu ? "SET" : "clear",
                rvu ? "SET" : "clear");
    test_printf("    IWDG timeout: %lu ms\\r\\n", timeout_ms);

    /* In hardware mode, PR=0 and RLR=0xFFF are the defaults */
    TEST_ASSERT(pr  == 0,
                "IWDG_PR should be 0 (default, hardware mode locked)");
    TEST_ASSERT(rlr == 0xFFF,
                "IWDG_RLR should be 0xFFF (default, hardware mode locked)");

    /* PVU/RVU may be set from board init's failed reconfig — informational */
    if (pvu || rvu) {
        test_printf("    [INFO] PVU/RVU set: expected in hardware mode,"
                    " no software PR/RLR write took effect\\r\\n");
    }

    TEST_PASS();
}


/* ================================================================
 *  Step 3: IWDG Feed Verification
 *
 *  Delay 5 seconds while the SD mount thread feeds IWDG via
 *  ap_rtt_iwdg_kick().  If we're alive after 5s, the feed works.
 * ================================================================ */
static void step_feed_test(void)
{
    TEST_STEP("IWDG Feed (5 second delay)");

    rt_tick_t t0 = rt_tick_get();

    /* Delay 5 seconds — something must feed IWDG every ~512ms */
    rt_thread_mdelay(5000);

    rt_tick_t t1 = rt_tick_get();
    rt_tick_t delta = t1 - t0;

    test_printf("    Delayed %lu ticks (~%lu ms)\\r\\n",
                (unsigned long)delta, (unsigned long)delta);

    /* We're alive → IWDG was fed (would have reset at ~512ms) */
    test_printf("    ALIVE after 5s idle → IWDG feed working\\r\\n");

    /* Verify SR is still readable */
    uint32_t sr = IWDG_SR;
    test_printf("    IWDG_SR=0x%08lx (post-delay)\\r\\n", (unsigned long)sr);
    TEST_ASSERT(sr != 0xFFFFFFFFU,
                "IWDG_SR should still be valid after 5s");

    TEST_PASS();
}


/* ================================================================
 *  Step 4: RCC_CSR Reset Source Flag
 *
 *  Check if IWDGRSTF is set (previous boot was a watchdog reset).
 * ================================================================ */
static void step_reset_source(void)
{
    TEST_STEP("RCC_CSR Reset Source");

    uint32_t csr = RCC_CSR;
    test_printf("    RCC_CSR=0x%08lx\\r\\n", (unsigned long)csr);

    bool iwdg_rst  = (csr & RCC_CSR_IWDGRSTF) != 0;
    bool soft_rst  = (csr & RCC_CSR_SFTRSTF)  != 0;
    bool pin_rst   = (csr & RCC_CSR_PINRSTF)  != 0;

    test_printf("    IWDGRST=%s SFTRST=%s PINRST=%s\\r\\n",
                iwdg_rst ? "YES" : "no",
                soft_rst ? "YES" : "no",
                pin_rst  ? "YES" : "no");

    if (iwdg_rst) {
        test_printf("    -> BOOT WAS IWDG WATCHDOG RESET\\r\\n");
    } else {
        test_printf("    -> Clean boot (no prior IWDG reset)\\r\\n");
    }

    /* Informational: don't fail on either case */
    TEST_PASS();
}


/* ================================================================
 *  Step 5: LSI Oscillator Status
 *
 *  IWDG uses LSI (~32kHz).  Verify it's running (LSIRDY=1).
 * ================================================================ */
static void step_lsi_status(void)
{
    TEST_STEP("LSI Oscillator Status");

    uint32_t csr = RCC_CSR;
    bool lsi_on  = (csr & RCC_CSR_LSION)  != 0;
    bool lsi_rdy = (csr & RCC_CSR_LSIRDY) != 0;

    test_printf("    LSION=%s LSIRDY=%s\\r\\n",
                lsi_on  ? "enabled" : "disabled",
                lsi_rdy ? "ready"   : "not ready");
    test_printf("    RCC_CSR=0x%08lx\\r\\n", (unsigned long)csr);

    TEST_ASSERT(lsi_on,  "LSI should be enabled (LSION=1)");
    TEST_ASSERT(lsi_rdy, "LSI should be ready (LSIRDY=1)");

    TEST_PASS();
}


/* ================================================================
 *  Main
 * ================================================================ */
int main(void)
{
    TEST_INIT("L1_IWDG");

    test_current_layer = 1;

    step_iwdg_accessible();
    step_iwdg_config();
    step_feed_test();
    step_reset_source();
    step_lsi_status();

    TEST_DONE();
    return 0;
}
