# Minimal AP_HAL_RTT + ArduPilot library sources for D_storage HAL smoke.
# Reuses the UART smoke whitelist (already includes Storage.cpp + AP_RAMTRON).

from __future__ import print_function

from rtt_test_hal_uart_link import (
    _ap_root_from_test_dir,
    collect_hal_uart_smoke_cpppath,
    collect_hal_uart_smoke_sources,
)


def collect_hal_storage_smoke_sources(test_driver_dir):
    return collect_hal_uart_smoke_sources(test_driver_dir)


def collect_hal_storage_smoke_cpppath(ap_root, bsp_dir, board='rtt_cuav_v5'):
    return collect_hal_uart_smoke_cpppath(ap_root, bsp_dir, board=board)
