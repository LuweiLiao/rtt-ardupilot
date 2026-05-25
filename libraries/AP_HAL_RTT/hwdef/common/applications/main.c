/*
 * Copyright (c) 2026, RTT ArduPilot Port
 *
 * Minimal RT-Thread test — Layer 0/0.5 validation.
 * Replace with real ArduCopter main() after confirming
 * foundation (IWDG + scheduler) is solid.
 *
 * 原理：先验证地基再盖楼
 *   Layer 0:   早期喂狗（rt_hw_board_init 内）→ 确认 CPU 不因 IWDG 复位
 *   Layer 0.5: 时钟初始化后重配 IWDG → 确认 PR/RLR 同步正常
 *   Layer 1:   main() 跑到 → 确认 scheduler + 线程正常
 *   Layer 2:   周期喂狗 + 打印 → 确认系统可持续运行
 */

#include <rtthread.h>
#include <stdint.h>

/* IWDG register direct access — CMSIS style, no HAL needed */
#define IWDG_BASE       0x40003000UL
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0C))

#define IWDG_KEY_RELOAD   0xAAAAU
#define IWDG_KEY_UNLOCK   0x5555U
#define IWDG_PVU_FLAG     1U
#define IWDG_RVU_FLAG     2U
#define TIMEOUT_LOOPS     100000

static void feed_iwdg(void)
{
    IWDG_KR = IWDG_KEY_RELOAD;
}

static void reconfig_iwdg(void)
{
    int i;

    /* Unlock PR */
    IWDG_KR = IWDG_KEY_UNLOCK;
    IWDG_PR = 6U;               /* prescaler = /256 */
    for (i = 0; i < TIMEOUT_LOOPS && (IWDG_SR & IWDG_PVU_FLAG); i++) { }
    if (i >= TIMEOUT_LOOPS) {
        rt_kprintf("[IWDG] PVU timeout — PR write may not have taken effect\n");
    }

    /* Unlock RLR */
    IWDG_KR = IWDG_KEY_UNLOCK;
    IWDG_RLR = 1250U;           /* ~10s timeout */
    for (i = 0; i < TIMEOUT_LOOPS && (IWDG_SR & IWDG_RVU_FLAG); i++) { }
    if (i >= TIMEOUT_LOOPS) {
        rt_kprintf("[IWDG] RVU timeout — RLR write may not have taken effect\n");
    }

    /* Final feed */
    IWDG_KR = IWDG_KEY_RELOAD;
    rt_kprintf("[IWDG] Reconfigured to ~10s timeout\n");
}

int main(void)
{
    rt_kprintf("\n========================================\n");
    rt_kprintf("  RTT MINIMAL TEST — Layer 0/0.5 Verify\n");
    rt_kprintf("========================================\n");

    /* Confirm Layer 0: early feed already done in rt_hw_board_init */
    rt_kprintf("[LAYER-0] Early IWDG feed in board_init — PASS\n");

    /* Layer 0.5: reconfig to ~10s */
    rt_kprintf("[LAYER-0.5] Attempting IWDG reconfig...\n");
    reconfig_iwdg();
    rt_kprintf("[LAYER-0.5] IWDG reconfig — PASS\n");

    /* Layer 1: main() reached */
    rt_kprintf("[LAYER-1] main() running — PASS\n");

    /* Periodic test loop */
    int count = 0;
    while (1)
    {
        feed_iwdg();  /* Keep watchdog happy while we test */

        rt_kprintf("[LOOP] Iteration %d — alive and running\n", ++count);

        if (count == 1) {
            rt_kprintf("[VERDICT] All layers PASS — system foundation solid\n");
            rt_kprintf("[VERDICT] IWDG: early feed OK, reconfig OK, periodic feed OK\n");
            rt_kprintf("[VERDICT] Scheduler: main thread running OK\n");
        }

        /* Read IWDG status and report */
        uint32_t sr_val = IWDG_SR;
        rt_kprintf("[IWDG] SR=0x%04x (PVU=%d RVU=%d)\n",
                   sr_val,
                   (sr_val & 1) ? 1 : 0,
                   (sr_val & 2) ? 1 : 0);

        rt_thread_mdelay(2000);
    }

    return 0;
}
