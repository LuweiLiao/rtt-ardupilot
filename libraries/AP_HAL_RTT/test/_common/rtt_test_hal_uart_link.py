# Minimal AP_HAL_RTT + ArduPilot library sources for D_uart_hal HAL smoke.
# Used only from test/drivers/D_uart_hal/SConscript (module-test build).

from __future__ import print_function

import glob
import os
import sys

SOURCE_EXTS = ('*.S', '*.c', '*.cpp')

# AP_HAL_RTT objects required by HAL_RTT_Class static drivers (no cherryusb / no IOMCU).
# Minimal HAL_RTT objects for UART smoke (no RC/SPI/I2C/Storage/ADC telemetry).
_HAL_RTT_UART_SMOKE_CPP = (
    'HAL_RTT_Class.cpp',
    'UARTDriver.cpp',
    'Scheduler.cpp',
    'Util.cpp',
    'Semaphores.cpp',
    'stdio.cpp',
    'system.cpp',
    'GPIO.cpp',
    'RCInput.cpp',
    'RCOutput.cpp',
    'RCOutput_serial.cpp',
    'SPIDeviceManager.cpp',
    'SPIDevice.cpp',
    'DeviceBus.cpp',
    'shared_dma.cpp',
    'I2CDeviceManager.cpp',
    'I2CDevice.cpp',
    'WSPIDevice.cpp',
    'Flash.cpp',
    'AnalogIn.cpp',
    'hal_adc_lld_rtt.c',
    'rtt_dbg_bkp.c',
    'Storage.cpp',
)

_HAL_RTT_EXCLUDE_C = frozenset([
    'hal_spi_lld.c',
    'hal_spi_lld_rtt.c',
    'rtt_board_init.c',
])

# Minimal AP libraries; vehicle / logger / full FS come from test_hal_uart_stubs.cpp.
_HAL_UART_SMOKE_LIBS = [
    'AP_HAL',
    'AP_Common',
    'AP_Math',
    'AP_Param',
    'AP_InternalError',
    'AP_BoardConfig',
    'AP_FlashStorage',
    'AP_RAMTRON',
    'StorageManager',
]


def _ap_root_from_test_dir(test_driver_dir):
    return os.path.abspath(os.path.join(test_driver_dir, '..', '..', '..', '..', '..'))


def _glob_lib_sources(ap_root, lib_name):
    rel_dir = os.path.join(ap_root, 'libraries', lib_name)
    if not os.path.isdir(rel_dir):
        return []
    out = []
    for ext in SOURCE_EXTS:
        for p in glob.glob(os.path.join(rel_dir, ext)):
            if os.path.isfile(p):
                out.append(p)
        util_dir = os.path.join(rel_dir, 'utility')
        if os.path.isdir(util_dir):
            for p in glob.glob(os.path.join(util_dir, ext)):
                if os.path.isfile(p):
                    out.append(p)
    return out


def _hal_rtt_sources(ap_root):
    hal_rtt = os.path.join(ap_root, 'libraries', 'AP_HAL_RTT')
    sources = []
    for name in _HAL_RTT_UART_SMOKE_CPP:
        p = os.path.join(hal_rtt, name)
        if os.path.isfile(p):
            sources.append(p)
    for ext in SOURCE_EXTS:
        for p in glob.glob(os.path.join(hal_rtt, ext)):
            base = os.path.basename(p)
            if base in _HAL_RTT_EXCLUDE_C:
                continue
            if base.endswith('.c') and base not in _HAL_RTT_UART_SMOKE_CPP:
                continue
            if p not in sources:
                sources.append(p)
    return sources


def collect_hal_uart_smoke_sources(test_driver_dir):
    ap_root = os.environ.get('AP_ROOT', '').strip() or _ap_root_from_test_dir(test_driver_dir)
    ap_root = os.path.abspath(ap_root)
    sources = []
    seen = set()
    for lib in _HAL_UART_SMOKE_LIBS:
        for p in _glob_lib_sources(ap_root, lib):
            if p not in seen:
                seen.add(p)
                sources.append(p)
    for p in _hal_rtt_sources(ap_root):
        if p not in seen:
            seen.add(p)
            sources.append(p)
    return ap_root, sources


def collect_hal_uart_smoke_cpppath(ap_root, bsp_dir, board='rtt_cuav_v5'):
    scripts = os.path.join(ap_root, 'Tools', 'scripts')
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    import scons_ardupilot_sources as sas

    rtt_root = os.environ.get('RTT_ROOT', '')
    if not rtt_root:
        rtt_root = os.path.join(ap_root, 'modules', 'rt-thread')
    build_root = os.path.join(ap_root, 'build', board)
    return sas._collect_cpppath(ap_root, bsp_dir, rtt_root, build_root, board=board)
