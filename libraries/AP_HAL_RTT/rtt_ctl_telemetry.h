/*
 * RTT control-loop telemetry — UART7 (RT console) structured snapshots.
 * One line per sample, parsed by Tools/scripts/rtt_control_loop/sensors/uart7_sensor.py
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dedicated UART7 hardware lane (usart_ll poll TX), independent of rt_kprintf. */
void rtt_ctl_uart_hw_init(void);
void rtt_ctl_hw_write(const char *s);

/* Print one RTT_CTL line immediately (also used by MSH ap_ctl). */
void rtt_ctl_print_snapshot(void);

/* Call from main loop; prints at most once per interval_ms (default 1000). */
void rtt_ctl_telemetry_tick(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
