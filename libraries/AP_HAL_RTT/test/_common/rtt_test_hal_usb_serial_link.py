# Minimal AP_HAL_RTT + CherryUSB for D_usb_serial HAL smoke (hal.serial(0) CDC).
# Reuses D_uart_hal HAL whitelist; adds CherryUSB + hal_usb_cherryusb_shim (no native USB).

from __future__ import print_function

import os
import sys

from rtt_test_hal_uart_link import (
    _HAL_RTT_EXCLUDE_C,
    _ap_root_from_test_dir,
    collect_hal_uart_smoke_cpppath,
    collect_hal_uart_smoke_sources,
)

_NATIVE_USB_C = frozenset([
    'hal_usb_lld_rtt.c',
    'hal_usb_rtt.c',
    'usb_cdc_rtt.c',
])


def _cherryusb_sources(ap_root):
    scripts = os.path.join(ap_root, 'Tools', 'scripts')
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    import rtt_usb_backend

    out = []
    for rel in rtt_usb_backend.cherryusb_extra_sources(ap_root):
        abs_p = os.path.join(ap_root, rel)
        if os.path.isfile(abs_p):
            out.append(abs_p)
    return out


def collect_hal_usb_serial_smoke_sources(test_driver_dir):
    ap_root, sources = collect_hal_uart_smoke_sources(test_driver_dir)
    filtered = []
    for p in sources:
        base = os.path.basename(p)
        if base in _NATIVE_USB_C:
            continue
        filtered.append(p)
    seen = set(filtered)
    for p in _cherryusb_sources(ap_root):
        if p not in seen:
            seen.add(p)
            filtered.append(p)
    return ap_root, filtered


def collect_hal_usb_serial_smoke_cpppath(ap_root, bsp_dir, board='rtt_cuav_v5'):
    paths = collect_hal_uart_smoke_cpppath(ap_root, bsp_dir, board=board)
    scripts = os.path.join(ap_root, 'Tools', 'scripts')
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    import rtt_usb_backend

    extra = rtt_usb_backend.cherryusb_extra_cpppath(ap_root)
    seen = set(paths)
    for p in extra:
        if p not in seen:
            seen.add(p)
            paths.append(p)
    return paths
