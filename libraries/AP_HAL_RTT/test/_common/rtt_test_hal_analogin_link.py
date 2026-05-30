# Minimal AP_HAL_RTT + ArduPilot library sources for D_analogin HAL smoke.
# Reuses the UART smoke whitelist (includes AnalogIn.cpp + hal_adc_lld_rtt.c).

from __future__ import print_function

from rtt_test_hal_uart_link import (
    _ap_root_from_test_dir,
    collect_hal_uart_smoke_cpppath,
    collect_hal_uart_smoke_sources,
)


def collect_hal_analogin_smoke_sources(test_driver_dir):
    return collect_hal_uart_smoke_sources(test_driver_dir)


def collect_hal_analogin_smoke_cpppath(ap_root, bsp_dir, board='rtt_cuav_v5'):
    return collect_hal_uart_smoke_cpppath(ap_root, bsp_dir, board=board)
