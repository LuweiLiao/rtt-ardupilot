# encoding: utf-8
"""
RTT layered module-test layout and scons --test= name resolution.

Canonical sources live under libraries/AP_HAL_RTT/test/.
Legacy hwdef/common/tests/ keeps README + symlinks for scripts and docs.

scons --target=cuav_v5 --test=<name> sets TEST_NAME=<name> and expects
  <test_dir>/SConscript under the resolved directory (not test_<name> prefix).
"""

from __future__ import print_function

import os

# TEST_NAME (scons --test=) -> path segments under libraries/AP_HAL_RTT/test/
TEST_LAYOUT = {
    'l0_boot': ('bringup', 'l0_boot'),
    'L0_system': ('bringup', 'L0_system'),
    'L1_iwdg': ('bringup', 'L1_iwdg'),
    'L2_gpio': ('bringup', 'L2_gpio'),
    'L3_uart': ('bringup', 'L3_uart'),
    'L4_spi': ('bringup', 'L4_spi'),
    'L5_usb': ('usb', '_legacy_native', 'L5_usb'),
    'L6_cdc': ('usb', '_legacy_native', 'L6_cdc'),
    # Production USB gate (CherryUSB); legacy scons name kept:
    'L7_cherryusb_cdc': ('usb', 'L6_cherryusb_cdc_echo',),
    'L6_cherryusb_cdc_echo': ('usb', 'L6_cherryusb_cdc_echo',),
    # HAL abstract (D*) — build-only placeholders until HAL examples are linked
    'D_uart_hal': ('drivers', 'D_uart_hal'),
    'D_spi_hal': ('drivers', 'D_spi_hal'),
    'D_i2c_hal': ('drivers', 'D_i2c_hal'),
    'D_storage': ('drivers', 'D_storage'),
    'D_scheduler': ('drivers', 'D_scheduler'),
    'D_analogin': ('drivers', 'D_analogin'),
    'D_usb_serial': ('drivers', 'D_usb_serial'),
    'D_rcoutput': ('drivers', 'D_rcoutput'),
    'D_rcinput': ('drivers', 'D_rcinput'),
    # External module (E*)
    'E_sdcard': ('drivers', 'E_sdcard'),
    'E_wspi_flash': ('drivers', 'E_wspi_flash'),
    'E_imu': ('drivers', 'E_imu'),
    'E_ms5611': ('drivers', 'E_ms5611'),
    'E_fram': ('drivers', 'E_fram'),
    'E_ist8310': ('drivers', 'E_ist8310'),
    # Subsystem smoke (S*) — multi-driver chains; S_rc_chain intentionally omitted
    'S_param_storage': ('subsystem', 'S_param_storage'),
    'S_sensors': ('subsystem', 'S_sensors'),
    'S_mavlink_usb': ('subsystem', 'S_mavlink_usb'),
    'S_compass': ('subsystem', 'S_compass'),
}

# Legacy directory names under hwdef/common/tests/ (for symlinks)
LEGACY_DIR_NAMES = {
    'l0_boot': 'test_l0_boot',
    'L0_system': 'test_L0_system',
    'L1_iwdg': 'test_L1_iwdg',
    'L2_gpio': 'test_L2_gpio',
    'L3_uart': 'test_L3_uart',
    'L4_spi': 'test_L4_spi',
    'L5_usb': 'test_L5_usb',
    'L6_cdc': 'test_L6_cdc',
    'L7_cherryusb_cdc': 'test_L7_cherryusb_cdc',
    'L6_cherryusb_cdc_echo': 'test_L6_cherryusb_cdc_echo',
    'D_uart_hal': 'test_D_uart_hal',
    'D_spi_hal': 'test_D_spi_hal',
    'D_i2c_hal': 'test_D_i2c_hal',
    'D_storage': 'test_D_storage',
    'D_scheduler': 'test_D_scheduler',
    'D_analogin': 'test_D_analogin',
    'D_usb_serial': 'test_D_usb_serial',
    'D_rcoutput': 'test_D_rcoutput',
    'D_rcinput': 'test_D_rcinput',
    'E_sdcard': 'test_E_sdcard',
    'E_wspi_flash': 'test_E_wspi_flash',
    'E_imu': 'test_E_imu',
    'E_ms5611': 'test_E_ms5611',
    'E_fram': 'test_E_fram',
    'E_ist8310': 'test_E_ist8310',
    'S_param_storage': 'test_S_param_storage',
    'S_sensors': 'test_S_sensors',
    'S_mavlink_usb': 'test_S_mavlink_usb',
    'S_compass': 'test_S_compass',
}


def hal_test_root(ap_root):
    return os.path.join(os.path.abspath(ap_root), 'libraries', 'AP_HAL_RTT', 'test')


def resolve_test_paths(ap_root, test_name, bsp_cwd=None):
    """
    Return (test_dir, common_dir, layout_key) for TEST_NAME.
    Falls back to hwdef/common/tests/test_<name> when canonical tree missing.
    """
    ap_root = os.path.abspath(ap_root)
    bsp_cwd = bsp_cwd or os.getcwd()
    key = (test_name or '').strip()
    root = hal_test_root(ap_root)
    common = os.path.join(root, '_common')

    if key in TEST_LAYOUT:
        test_dir = os.path.join(root, *TEST_LAYOUT[key])
        if os.path.isfile(os.path.join(test_dir, 'SConscript')):
            return test_dir, common, key

    legacy_test = os.path.join(bsp_cwd, 'tests', 'test_' + key)
    legacy_common = os.path.join(bsp_cwd, 'tests', 'common')
    return legacy_test, legacy_common, key


def legacy_symlink_target(ap_root, test_name):
    """Relative path from hwdef/common/tests/test_XXX to canonical test dir."""
    if test_name not in TEST_LAYOUT:
        return None
    segs = TEST_LAYOUT[test_name]
    # hwdef/common/tests/test_FOO -> ../../../../test/...
    ups = ['..'] * 4
    return os.path.join(*ups + ['test'] + list(segs))
