# encoding: utf-8
"""
RTT USB backend selection for scons full-build and module tests.

Exactly one stack may provide OTG_FS_IRQHandler per link image:
  - native:    AP_HAL_RTT hal_usb_lld_rtt.c (+ usb_cdc_rtt.c)
  - tinyusb:   third-party TinyUSB + board glue (tests / future main)
  - cherryusb: third-party CherryUSB + board glue (tests / future main)
  - none:      test firmware is self-contained (e.g. L5/L6) or no USB

Environment:
  TEST_NAME        — module test (mutually exclusive with ARDUPILOT_FULL)
  ARDUPILOT_FULL   — full ArduPilot link (set by root SConstruct when no --test)
  RTT_USB_BACKEND  — force backend for full build (default: cherryusb)
"""

from __future__ import print_function

import os

BACKEND_NATIVE = 'native'
BACKEND_TINYUSB = 'tinyusb'
BACKEND_CHERRYUSB = 'cherryusb'
BACKEND_NONE = 'none'

_VALID = frozenset([BACKEND_NATIVE, BACKEND_TINYUSB, BACKEND_CHERRYUSB, BACKEND_NONE])

# AP_HAL_RTT root-level C sources tied to the native DWC2 stack
NATIVE_HAL_SOURCES = frozenset([
    'hal_usb_lld_rtt.c',
    'hal_usb_rtt.c',
    'usb_cdc_rtt.c',
])

# L8 MSC-only: SDIO must not init before USB test board_init (shared SDMMC1 IRQ hazard)
L8_MSC_NO_SDIO_TESTS = frozenset(['L8_tinyusb_msc', 'L8_cherryusb_msc'])

# RT-Thread BSP options that pull CherryUSB / legacy USB device drivers into librtthread
_BSP_USB_BUILD_KEYS = (
    'BSP_USING_USB_DEVICE',
    'BSP_USING_USB_TO_USART',
    'RT_CHERRYUSB_DEVICE',
    'RT_USING_CHERRYUSB',
)


def _infer_test_backend(test_name):
    key = test_name.strip().lower()
    if key in ('d_usb_serial', 's_mavlink_usb'):
        return BACKEND_CHERRYUSB
    if 'tinyusb' in key:
        return BACKEND_TINYUSB
    if 'cherryusb' in key:
        return BACKEND_CHERRYUSB
    if key in ('l5_usb', 'l6_cdc'):
        return BACKEND_NONE
    return BACKEND_NONE


def resolve_usb_backend():
    test_name = os.environ.get('TEST_NAME', '').strip()
    ardupilot_full = os.environ.get('ARDUPILOT_FULL', '0') == '1' and not test_name
    explicit = os.environ.get('RTT_USB_BACKEND', '').strip().lower()

    if test_name:
        backend = _infer_test_backend(test_name)
    elif ardupilot_full:
        backend = explicit or BACKEND_CHERRYUSB
    else:
        backend = BACKEND_NONE

    if backend not in _VALID:
        backend = BACKEND_CHERRYUSB if ardupilot_full else BACKEND_NONE

    # Main app and module tests each own OTG_FS; do not link BSP cherryusb glue alongside them.
    disable_bsp_usb = ardupilot_full or bool(test_name)

    cpp_defines = ['RTT_USB_BACKEND_%s=1' % backend.upper().replace('-', '_')]
    if backend == BACKEND_NATIVE:
        cpp_defines.append('RTT_USB_STACK_NATIVE=1')

    return {
        'backend': backend,
        'test_name': test_name,
        'ardupilot_full': ardupilot_full,
        'l8_msc_no_sdio': test_name in L8_MSC_NO_SDIO_TESTS,
        'disable_bsp_usb': disable_bsp_usb,
        'cpp_defines': cpp_defines,
        'bsp_usb_build_keys': _BSP_USB_BUILD_KEYS,
    }


def filter_ap_hal_rtt_source(rel_path, backend):
    """Return True if rel_path (under libraries/AP_HAL_RTT/) should be compiled."""
    base = os.path.basename(rel_path)
    if base == 'hal_usb_cherryusb_shim.c':
        return backend == BACKEND_CHERRYUSB
    if base not in NATIVE_HAL_SOURCES:
        return True
    return backend == BACKEND_NATIVE


def cherryusb_extra_sources(ap_root):
    """CherryUSB stack + board glue + shim (relative to ap_root)."""
    ap_root = os.path.abspath(ap_root)
    hal = os.path.join(ap_root, 'libraries', 'AP_HAL_RTT')
    cherry_root = os.path.join(hal, 'thirdparty', 'cherryusb')
    rel = []
    for sub in (
        os.path.join('core', 'usbd_core.c'),
        os.path.join('class', 'cdc', 'usbd_cdc_acm.c'),
        os.path.join('port', 'dwc2', 'usb_dc_dwc2.c'),
        os.path.join('osal', 'usb_osal_rtthread.c'),
    ):
        abs_p = os.path.join(cherry_root, sub)
        if os.path.isfile(abs_p):
            rel.append(os.path.relpath(abs_p, ap_root))
    for sub in (
        os.path.join('cherryusb_board', 'usb_dc_glue.c'),
        'hal_usb_cherryusb_shim.c',
    ):
        abs_p = os.path.join(hal, sub)
        if os.path.isfile(abs_p):
            rel.append(os.path.relpath(abs_p, ap_root))
    return rel


def cherryusb_extra_cpppath(ap_root):
    ap_root = os.path.abspath(ap_root)
    hal = os.path.join(ap_root, 'libraries', 'AP_HAL_RTT')
    cherry_root = os.path.join(hal, 'thirdparty', 'cherryusb')
    paths = [
        os.path.join(hal, 'cherryusb_board'),
        os.path.join(cherry_root, 'common'),
        os.path.join(cherry_root, 'core'),
        os.path.join(cherry_root, 'class', 'cdc'),
        os.path.join(cherry_root, 'port', 'dwc2'),
        os.path.join(cherry_root, 'osal'),
    ]
    return [p for p in paths if os.path.isdir(p)]


def apply_bsp_usb_build_options(env, usb_info, build_options):
    """Disable BSP USB drivers so only the selected stack owns OTG_FS."""
    if not usb_info.get('disable_bsp_usb'):
        return
    undef = []
    for key in usb_info.get('bsp_usb_build_keys', _BSP_USB_BUILD_KEYS):
        build_options[key] = 0
        undef.append(key)
    if undef:
        env.Append(CCFLAGS=' -U ' + ' -U '.join(undef))
