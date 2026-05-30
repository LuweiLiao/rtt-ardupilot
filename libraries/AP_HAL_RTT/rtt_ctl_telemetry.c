/*
 * Structured UART7 telemetry for closed-loop debug agents.
 * TX path: direct CMSIS poll on UART7 (PE8/PF6) — not rt_kprintf/console DMA.
 */
#include "rtt_ctl_telemetry.h"
#include "rtt_dbg_bkp.h"
#include "hal_usb_lld_rtt.h"
#include <rtthread.h>
#include <stdio.h>
#include <string.h>

#if defined(SOC_SERIES_STM32F7)
#include "drv_usart_ll.h"
#endif

extern volatile uint32_t rtt_dbg_hal_run_called;
extern volatile uint32_t rtt_dbg_main_loop_entry_called;
extern volatile uint32_t rtt_dbg_main_loop_iterations;
extern volatile uint32_t rtt_dbg_loop_time_us;
extern volatile uint32_t rtt_dbg_overrun_count;
extern volatile uint32_t rtt_dbg_setup_stage;
extern volatile uint32_t rtt_dbg_storage_backend;
extern volatile uint32_t rtt_dbg_usb_init;
extern volatile uint32_t rtt_dbg_usb_usbrst;
extern volatile uint32_t rtt_dbg_usb_enumdne;
extern volatile uint32_t rtt_dbg_usb_setup_stup;

extern volatile rt_uint32_t rtt_dbg_hardfault_stack_pc;

static uint32_t _last_print_ms;
static rt_bool_t _hw_ready;

static uint32_t _now_ms(void)
{
    return (uint32_t)((rt_tick_get() * 1000U) / RT_TICK_PER_SECOND);
}

void rtt_ctl_uart_hw_init(void)
{
#if defined(SOC_SERIES_STM32F7)
    if (_hw_ready) {
        return;
    }
    usart_ll_init(&uart7_ll_cfg);
    _hw_ready = RT_TRUE;
#else
    _hw_ready = RT_TRUE;
#endif
}

void rtt_ctl_hw_write(const char *s)
{
    if (s == RT_NULL || s[0] == '\0') {
        return;
    }
#if defined(SOC_SERIES_STM32F7)
    if (!_hw_ready) {
        rtt_ctl_uart_hw_init();
    }
    (void)usart_ll_tx_poll(UART7, (const uint8_t *)s, (int)strlen(s));
#else
    (void)s;
#endif
}

static void _emit_line(const char *line)
{
    rtt_ctl_hw_write(line);
    if (line[strlen(line) - 1] != '\n') {
        rtt_ctl_hw_write("\n");
    }
}

void rtt_ctl_print_snapshot(void)
{
    const unsigned cfg = usb_lld_is_configured_rtt() ? 1U : 0U;
    const unsigned conn = usb_lld_get_connected_rtt() ? 1U : 0U;
    const unsigned loop_hz = (rtt_dbg_loop_time_us > 0U)
        ? (1000000U / rtt_dbg_loop_time_us) : 0U;

    char buf[320];
    const int n = snprintf(
        buf, sizeof(buf),
        "RTT_CTL t=%lu hal=0x%08lx ent=0x%08lx iter=%lu "
        "usb=%lu stup=%lu usbrst=%lu enmd=%lu cfg=%u conn=%u "
        "loop_us=%lu loop_hz=%u ov=%lu stg=%lu stor=%lu hf=0x%08lx",
        (unsigned long)_now_ms(),
        (unsigned long)rtt_dbg_hal_run_called,
        (unsigned long)rtt_dbg_main_loop_entry_called,
        (unsigned long)rtt_dbg_main_loop_iterations,
        (unsigned long)rtt_dbg_usb_init,
        (unsigned long)rtt_dbg_usb_setup_stup,
        (unsigned long)rtt_dbg_usb_usbrst,
        (unsigned long)rtt_dbg_usb_enumdne,
        cfg,
        conn,
        (unsigned long)rtt_dbg_loop_time_us,
        loop_hz,
        (unsigned long)rtt_dbg_overrun_count,
        (unsigned long)rtt_dbg_setup_stage,
        (unsigned long)rtt_dbg_storage_backend,
        (unsigned long)rtt_dbg_hardfault_stack_pc);
    if (n > 0 && n < (int)sizeof(buf)) {
        _emit_line(buf);
    }
    if (rtt_dbg_bkp_prev_fault_pending()) {
        char fbuf[96];
        rtt_dbg_bkp_format_prev_fault(fbuf, sizeof(fbuf));
        _emit_line(fbuf);
    }
}

void rtt_ctl_telemetry_tick(uint32_t interval_ms)
{
    const uint32_t now = _now_ms();
    if (interval_ms == 0U) {
        interval_ms = 1000U;
    }
    if ((now - _last_print_ms) < interval_ms) {
        return;
    }
    _last_print_ms = now;
    rtt_ctl_print_snapshot();
}

#if defined(RT_USING_FINSH) && defined(MSH_USING_BUILT_IN_COMMANDS)
#include <finsh.h>
static void ap_ctl(void)
{
    rtt_ctl_print_snapshot();
}
MSH_CMD_EXPORT(ap_ctl, dump RTT_CTL control-loop telemetry line);
#endif
