# Minimal AP_HAL_RTT sources for D_rcinput HAL smoke (same set as D_uart_hal / D_rcoutput).

from __future__ import print_function

from rtt_test_hal_uart_link import (
    _ap_root_from_test_dir,
    collect_hal_uart_smoke_cpppath,
    collect_hal_uart_smoke_sources,
)

collect_hal_rcin_smoke_sources = collect_hal_uart_smoke_sources
collect_hal_rcin_smoke_cpppath = collect_hal_uart_smoke_cpppath
