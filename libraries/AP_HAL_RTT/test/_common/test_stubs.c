/**
 * test_stubs.c — Stub implementations for symbols referenced by board init code
 * but not yet implemented in the test build context.
 *
 * These are NO-OP stubs that allow the test firmware to link and run.
 * Real implementations will be used in higher-layer tests.
 *
 * Symbols needed:
 *   - ap_rtt_iwdg_kick       (called from rt_board_init idle hook & board init)
 *   - usb_lld_is_configured_rtt  (called from rt_board_init idle hook)
 *   - usb_lld_poll_rtt           (called from rt_board_init idle hook)
 *   - rtt_ctl_uart_hw_init       (called from rt_board_init)
 *   - rtt_ctl_hw_write           (called from rt_board_init)
 *   - rtt_dbg_main_thread_entered    (set in components.c main thread)
 *   - rtt_dbg_components_init_done   (set in components.c main thread)
 *   - rtt_dbg_main_called            (set in components.c main thread)
 */

#include <stdint.h>
#include <rtthread.h>

/* ================================================================
 *  IWDG — Independent Watchdog (real feed, shared by L1+ tests)
 *
 *  IWDG is always enabled by hardware option bytes (~512ms timeout
 *  at boot), then reconfigured to ~10s by rt_board_init.c.
 *  Without periodic feeding the system resets within ~10s.
 * ================================================================ */
#define IWDG_BASE   0x40003000UL
#define IWDG_KR     (*(volatile uint32_t *)(IWDG_BASE + 0x00))

void ap_rtt_iwdg_kick(void)
{
    /* Feed the watchdog: IWDG_KR = 0xAAAA reloads counter with RLR */
    IWDG_KR = 0xAAAA;
    __asm volatile("dsb" ::: "memory");
}

/* ================================================================
 *  USB — Device Controller Stubs
 * ================================================================ */
uint32_t usb_lld_is_configured_rtt(void)
{
    return 0;  /* Not configured */
}

void usb_lld_poll_rtt(void)
{
    /* No-op: USB not yet initialized */
}

/* ================================================================
 *  UART7 Control Telemetry Stubs
 * ================================================================ */
void rtt_ctl_uart_hw_init(void)
{
    /* No-op: test_runner handles UART7 init independently */
}

void rtt_ctl_hw_write(const char *s)
{
    (void)s;
    /* No-op: use test_runner's output instead */
}

/* ================================================================
 *  Debug Marker Variables (BSS — auto-zeroed)
 * ================================================================ */
volatile uint32_t rtt_dbg_main_thread_entered      __attribute__((section(".bss"))) = 0;
volatile uint32_t rtt_dbg_components_init_done     __attribute__((section(".bss"))) = 0;
volatile uint32_t rtt_dbg_main_called              __attribute__((section(".bss"))) = 0;
