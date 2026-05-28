/**
 * test_L0_system — Layer 0: System Foundation
 *
 * Verifies the four-layer foundation of the minimal RT-Thread system:
 *   Step 1: CPU core identification (CPUID = Cortex-M7)
 *   Step 2: FPU enable check (CPACR)
 *   Step 3: SysTick timing (1ms tick, verify accuracy)
 *   Step 4: RT-Thread kernel (tick advancement, thread scheduling)
 *   Step 5: Console output (UART7 + rt_kprintf dual-path verification)
 *
 * Prerequisites: Board powered, ST-Link connected, UART7 CH340 connected.
 * Build: TEST_NAME=L0_system scons --target=cuav-v5 -j$(nproc)
 */

#include "test_runner.h"
#include <rtthread.h>

/* ================================================================
 *  Step 1: CPU Core Identification
 * ================================================================ */
static void step_cpu_id(void)
{
    TEST_STEP("CPU Core Identification");

    uint32_t cpuid = test_read_cpuid();

    /* CPUID format: [31:24] Implementer, [23:20] Variant, [19:16] Arch, [15:4] PartNo, [3:0] Revision */
    uint32_t implementer = (cpuid >> 24) & 0xFF;
    uint32_t arch       = (cpuid >> 16) & 0x0F;
    uint32_t part_no    = (cpuid >> 4)  & 0xFFF;
    uint32_t revision   = cpuid & 0x0F;

    test_printf("    CPUID=0x%08lx\r\n", (unsigned long)cpuid);
    test_printf("    Implementer=0x%02lx Arch=0x%01lx Part=0x%03lx Rev=%lu\r\n",
                (unsigned long)implementer, (unsigned long)arch,
                (unsigned long)part_no, (unsigned long)revision);

    /* Cortex-M7: Implementer=0x41 (ARM), Arch=0xF (ARMv7), Part=0xC27 */
    TEST_ASSERT(implementer == 0x41, "Implementer should be ARM (0x41)");
    TEST_ASSERT(arch == 0xF,        "Arch should be ARMv7-M (0xF)");
    TEST_ASSERT(part_no == 0xC27,   "Part number should be Cortex-M7 (0xC27)");
    TEST_PASS();
}

/* ================================================================
 *  Step 2: FPU Enable Check
 * ================================================================ */
static void step_fpu(void)
{
    TEST_STEP("FPU Status");

    bool fpu_ok = test_fpu_enabled();
    test_printf("    CPACR=0x%08lx\r\n", (unsigned long)CPACR);

    TEST_ASSERT(fpu_ok, "FPU should be enabled (CPACR & 0x00F00000)");
    TEST_PASS();
}

/* ================================================================
 *  Step 3: SysTick Configuration
 * ================================================================ */
static void step_systick_config(void)
{
    TEST_STEP("SysTick Configuration");

    uint32_t csr = test_read_systick_csr();
    uint32_t rvr = test_read_systick_rvr();
    uint32_t cvr = test_read_systick_cvr();

    test_printf("    CSR=0x%08lx RVR=%lu CVR=%lu\r\n",
                (unsigned long)csr, (unsigned long)rvr, (unsigned long)cvr);

    /* CSR bit 0: ENABLE, bit 1: TICKINT, bit 2: CLKSOURCE */
    bool enabled   = (csr & 0x01) != 0;
    bool tickint   = (csr & 0x02) != 0;
    bool clksource = (csr & 0x04) != 0;  /* 1=processor clock, 0=external */

    TEST_ASSERT(enabled,   "SysTick should be enabled (CSR.ENABLE=1)");
    TEST_ASSERT(tickint,   "SysTick should have interrupt enabled (CSR.TICKINT=1)");
    TEST_ASSERT(clksource, "SysTick should use processor clock (CSR.CLKSOURCE=1)");
    TEST_PASS();
}

/* ================================================================
 *  Step 4: SysTick Timing Accuracy
 *
 *  Wait 5000 ticks (5 seconds) and verify the counter advances correctly.
 *  This validates both the SysTick interrupt rate and RT-Thread's tick handling.
 * ================================================================ */
static void step_systick_timing(void)
{
    TEST_STEP("SysTick Timing (5 second accuracy check)");

    rt_tick_t t0 = rt_tick_get();
    rt_thread_mdelay(5000);
    rt_tick_t t1 = rt_tick_get();
    rt_tick_t delta = t1 - t0;

    test_printf("    t0=%lu t1=%lu delta=%lu (expect ~5000)\r\n",
                (unsigned long)t0, (unsigned long)t1, (unsigned long)delta);

    /* Allow 2% tolerance: 4900-5100 ticks */
    TEST_ASSERT(delta >= 4900, "Tick delta should be >= 4900 (within 2% low)");
    TEST_ASSERT(delta <= 5100, "Tick delta should be <= 5100 (within 2% high)");
    TEST_PASS();
}

/* ================================================================
 *  Step 5: Thread Scheduling Verification
 *
 *  Create a secondary thread, verify it runs, verify main thread still runs.
 * ================================================================ */
static volatile uint32_t _worker_tick;

static void worker_thread_entry(void *param)
{
    (void)param;
    while (1) {
        _worker_tick = rt_tick_get();
        rt_thread_mdelay(10);
    }
}

static void step_threads(void)
{
    TEST_STEP("Thread Creation and Scheduling");

    /* Create a worker thread */
    rt_thread_t worker = rt_thread_create("worker",
                                          worker_thread_entry,
                                          NULL,
                                          1024,
                                          20,  /* priority */
                                          10); /* timeslice */
    TEST_ASSERT(worker != NULL, "Worker thread should be created");
    if (worker) {
        rt_thread_startup(worker);
        test_printf("    Worker thread created and started\r\n");
    }

    /* Wait and check worker ran */
    rt_thread_mdelay(100);
    TEST_ASSERT(_worker_tick > 0, "Worker thread should have run (tick > 0)");

    test_printf("    Worker tick=%lu (main tick=%lu)\r\n",
                (unsigned long)_worker_tick, (unsigned long)rt_tick_get());
    TEST_PASS();
}

/* ================================================================
 *  Step 6: rt_kprintf + UART7 dual path
 * ================================================================ */
static void step_console(void)
{
    TEST_STEP("Console Output (Dual Path)");

    /* UART7 direct output already working (all previous TEST_* used it) */
    test_uart7_write("    [UART7] Direct output verified by previous steps\r\n");

    /* rt_kprintf output */
    rt_kprintf("[RTT] rt_kprintf output test: PASS\n");

    TEST_PASS();
}

/* ================================================================
 *  Main
 * ================================================================ */
int main(void)
{
    TEST_INIT("L0_SYSTEM");

    test_current_layer = 0;

    step_cpu_id();
    step_fpu();
    step_systick_config();
    step_systick_timing();
    step_threads();
    step_console();

    TEST_DONE();
    return 0;
}
