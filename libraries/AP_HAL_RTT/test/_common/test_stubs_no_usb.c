/**
 * test_stubs_no_usb.c — Stubs for HAL module tests that link real CherryUSB CDC.
 *
 * Same as test_stubs.c but omits usb_lld_* no-ops so hal_usb_cherryusb_shim.c
 * can provide OTG_FS / CDC symbols (D_usb_serial).
 *
 * rtt_dbg_usb_* live in hal_usb_cherryusb_shim.c — do not duplicate here.
 */

#include <stdint.h>
#include <rtthread.h>

#define IWDG_BASE   0x40003000UL
#define IWDG_KR     (*(volatile uint32_t *)(IWDG_BASE + 0x00))

void ap_rtt_iwdg_kick(void)
{
    IWDG_KR = 0xAAAA;
    __asm volatile("dsb" ::: "memory");
}

volatile uint32_t rtt_dbg_setup_stage;

void rtt_ctl_print_snapshot(void) {}
void rtt_ctl_telemetry_tick(uint32_t interval_ms)
{
    (void)interval_ms;
}

void rtt_ctl_uart_hw_init(void)
{
}

void rtt_ctl_hw_write(const char *s)
{
    (void)s;
}

volatile uint32_t rtt_dbg_main_thread_entered      __attribute__((section(".bss"))) = 0;
volatile uint32_t rtt_dbg_components_init_done     __attribute__((section(".bss"))) = 0;
volatile uint32_t rtt_dbg_main_called              __attribute__((section(".bss"))) = 0;
