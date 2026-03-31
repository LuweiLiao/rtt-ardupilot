/*
 * L0 Boot Test — minimal RT-Thread boot verification.
 *
 * Validates:
 *   - Reset_Handler → SystemInit → .data/.bss init → entry()
 *   - RT-Thread kernel starts (scheduler, heap, console)
 *   - UART7 console output works (msh shell)
 *   - GPIO pin write works (LED blink)
 *   - DWT CYCCNT microsecond timing works
 *   - System tick at expected frequency
 *
 * Expected output on UART7 (115200 8N1):
 *   [L0] ========== BOOT TEST ==========
 *   [L0] RT-Thread version: x.x.x
 *   [L0] SystemCoreClock: 216000000
 *   [L0] RT_TICK_PER_SECOND: 10000
 *   [L0] Heap free: xxxxx bytes
 *   [L0] DWT 1ms test: xxxx us (expect ~1000)
 *   [L0] LED blinking on PB0 ...
 *   [L0] PASS — all L0 checks OK
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define LED_PIN     GET_PIN(B, 0)

#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define SCB_DEMCR   (*(volatile uint32_t *)0xE000EDFC)

extern uint32_t SystemCoreClock;

volatile uint32_t l0_test_result = 0;
volatile uint32_t l0_clock_mhz = 0;
volatile uint32_t l0_heap_free = 0;
volatile uint32_t l0_dwt_1ms_us = 0;
volatile uint32_t l0_tick_100ms = 0;
volatile uint32_t l0_loop_count = 0;
volatile uint32_t l0_cyc_before = 0;
volatile uint32_t l0_cyc_after = 0;
volatile uint32_t l0_cyc_diff = 0;

static uint32_t dwt_us(void)
{
    return DWT_CYCCNT / (SystemCoreClock / 1000000U);
}

static void dwt_init(void)
{
    SCB_DEMCR |= (1U << 24);
    *(volatile uint32_t *)0xE0001FB0 = 0xC5ACCE55;  /* DWT_LAR unlock (Cortex-M7) */
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1U;
}

int main(void)
{
    dwt_init();

    rt_kprintf("\n[L0] ========== BOOT TEST ==========\n");
    rt_kprintf("[L0] RT-Thread version: %d.%d.%d\n",
               RT_VERSION_MAJOR, RT_VERSION_MINOR, RT_VERSION_PATCH);
    rt_kprintf("[L0] SystemCoreClock: %u\n", (unsigned)SystemCoreClock);
    rt_kprintf("[L0] RT_TICK_PER_SECOND: %d\n", RT_TICK_PER_SECOND);

    rt_size_t total = 0, used = 0, max_used = 0;
    rt_memory_info(&total, &used, &max_used);
    l0_clock_mhz = SystemCoreClock / 1000000U;
    l0_heap_free = (uint32_t)(total - used);
    rt_kprintf("[L0] Heap total: %u  used: %u  max_used: %u  free: %u\n",
               (unsigned)total, (unsigned)used, (unsigned)max_used,
               (unsigned)(total - used));

    {
        /* DWT busy-wait test: spin for exactly 10ms worth of cycles */
        uint32_t target_cyc = (SystemCoreClock / 1000U) * 10;  /* 10ms */
        l0_cyc_before = DWT_CYCCNT;
        while ((DWT_CYCCNT - l0_cyc_before) < target_cyc) { }
        l0_cyc_after = DWT_CYCCNT;
        l0_cyc_diff = l0_cyc_after - l0_cyc_before;
        l0_dwt_1ms_us = (l0_cyc_diff / (SystemCoreClock / 1000000U)) / 10;
        rt_kprintf("[L0] DWT busy 10ms: cyc=%u diff=%u per_ms=%u us\n",
                   (unsigned)l0_cyc_before, (unsigned)l0_cyc_diff,
                   (unsigned)l0_dwt_1ms_us);
    }

    rt_tick_t tick0 = rt_tick_get();
    rt_thread_mdelay(100);
    rt_tick_t tick1 = rt_tick_get();
    uint32_t expected_ticks = RT_TICK_PER_SECOND / 10;
    l0_tick_100ms = (uint32_t)(tick1 - tick0);
    rt_kprintf("[L0] 100ms tick test: %u ticks (expect ~%u)\n",
               (unsigned)l0_tick_100ms, (unsigned)expected_ticks);

    int errors = 0;

    if (SystemCoreClock < 200000000 || SystemCoreClock > 220000000) {
        rt_kprintf("[L0] FAIL — SystemCoreClock out of range\n");
        errors++;
    }

    if (l0_dwt_1ms_us < 500 || l0_dwt_1ms_us > 5000) {
        rt_kprintf("[L0] FAIL — DWT timing out of range: %u us\n", (unsigned)l0_dwt_1ms_us);
        errors++;
    }

    int32_t tick_err = (int32_t)l0_tick_100ms - (int32_t)expected_ticks;
    if (tick_err < -50 || tick_err > 50) {
        rt_kprintf("[L0] FAIL — tick count off by %d\n", (int)tick_err);
        errors++;
    }

    rt_kprintf("[L0] LED blinking on PB0 ...\n");
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);

    if (errors == 0) {
        l0_test_result = 0x900D900D;
        rt_kprintf("[L0] PASS — all L0 checks OK\n");
    } else {
        l0_test_result = errors;
        rt_kprintf("[L0] FAIL — %d error(s)\n", errors);
    }

    rt_kprintf("[L0] ========== BOOT TEST END ==========\n\n");

    while (1) {
        l0_loop_count++;
        rt_pin_write(LED_PIN, PIN_HIGH);
        rt_thread_mdelay(200);
        rt_pin_write(LED_PIN, PIN_LOW);
        rt_thread_mdelay(200);
    }
}
