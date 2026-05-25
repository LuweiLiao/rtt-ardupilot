/**
 * test_l0_boot — Layer 0 Foundation Verification
 *
 * 验证最小 RT-Thread 系统的四层地基：
 *   Layer 0:   早期喂狗（rt_hw_board_init 内 _iwdg_early_feed）
 *   Layer 0.5: 时钟初始化后重配 IWDG（_iwdg_reconfig）
 *   Layer 1:   main() 线程调度成功
 *   Layer 2:   系统可持续运行（周期喂狗 + 控制台输出）
 *
 * 全部通过后再添加 ArduPilot 层。
 */

#include <rtthread.h>
#include <stdint.h>

/* ========================================================
 *  Debug markers — same names as AP_HAL_RTT for familiarity
 * ======================================================== */
volatile uint32_t rtt_dbg_hal_run_called        __attribute__((section(".data"))) = 0xDEADBEEF;
volatile uint32_t rtt_dbg_main_loop_entry_called __attribute__((section(".data"))) = 0xCAFEBABE;
volatile uint32_t rtt_dbg_main_loop_iterations   __attribute__((section(".data"))) = 0;
volatile uint32_t rtt_dbg_fast_loop_count        __attribute__((section(".data"))) = 0;

/* ========================================================
 *  IWDG register access (CMSIS style, no HAL)
 * ======================================================== */
#define IWDG_BASE       0x40003000UL
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0C))

#define TIMEOUT_LOOPS   100000

static void feed_iwdg(void)
{
    IWDG_KR = 0xAAAAU;
}

static void report_iwdg(void)
{
    uint32_t kr  = IWDG_KR;
    uint32_t pr  = IWDG_PR;
    uint32_t rlr = IWDG_RLR;
    uint32_t sr  = IWDG_SR;

    rt_kprintf("[IWDG] KR=0x%08lx  PR=%lu(/4<<%lu)  RLR=%lu(%lu ms)  SR=0x%08lx\n",
               kr, pr, pr, rlr,
               /* PR=0→/4: timeout_ms = (RLR+1)*prescaler/32 */
               ((unsigned long)((rlr + 1) * (pr == 0 ? 4 : (pr == 6 ? 256 : 4)) * 1000) / 32000),
               sr);
}

/* ========================================================
 *  main
 * ======================================================== */
int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("========================================\n");
    rt_kprintf("  RTT MINIMAL TEST — test_l0_boot       \n");
    rt_kprintf("========================================\n");
    rt_kprintf("\n");

    /* ---- Layer 0: Early feed (done in rt_hw_board_init) ---- */
    rt_kprintf("[LAYER-0] Early IWDG feed in rt_hw_board_init\n");
    rt_kprintf("[LAYER-0] CPU is alive — IWDG did not reset\n");
    rt_kprintf("[LAYER-0]  ===> PASS\n");

    /* ---- Layer 0.5: IWDG reconfig (done in rt_hw_board_init after clock init) ---- */
    rt_kprintf("[LAYER-0.5] IWDG status after reconfig:\n");
    report_iwdg();
    rt_kprintf("[LAYER-0.5]  ===> VERIFY above (expect RLR=1250, PR=6)\n");

    /* ---- Layer 1: main() reached ---- */
    rtt_dbg_hal_run_called = 0xAAAAAAAA;
    rt_kprintf("[LAYER-1] main() thread is running\n");
    rt_kprintf("[LAYER-1] rtt_dbg_hal_run_called = 0x%08lx\n",
               (unsigned long)rtt_dbg_hal_run_called);
    rt_kprintf("[LAYER-1]  ===> PASS\n");

    /* ---- Layer 2: Continuous operation ---- */
    rt_kprintf("[LAYER-2] Entering main loop — IWDG fed every 2s\n");
    rt_kprintf("[LAYER-2] Watch heartbeat prints below\n");
    rt_kprintf("\n");

    int count = 0;
    while (1)
    {
        feed_iwdg();
        rtt_dbg_main_loop_iterations = ++count;

        rt_kprintf("[LOOP] Iteration %d — alive\n", count);

        if (count % 5 == 0) {
            rt_kprintf("[HEARTBEAT] IWDG status:\n");
            report_iwdg();
        }

        rt_thread_mdelay(2000);
    }

    return 0;
}
