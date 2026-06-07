#!/usr/bin/env python3
# encoding: utf-8
"""
Generate ap_sources, ap_cpppath, ap_defines for scons full-build (Plan A).
No waf dependency; matches waf behavior for rtt_pixhawk6c_mini + Copter.
Usage: python3 scons_ardupilot_sources.py <AP_ROOT> [--board rtt_pixhawk6c_mini] [--vehicle ArduCopter] [--output <path>]
        or from BSP: python3 ../../../../Tools/scripts/scons_ardupilot_sources.py $(pwd)/../../../../ [--output .]
Output: writes scons_ardupilot_config.py (or --output path) with ap_sources, ap_cpppath, ap_defines.
"""

import argparse
import glob
import json
import os
import pickle
import sys

# Match Tools/ardupilotwaf/ardupilotwaf.py
COMMON_VEHICLE_DEPENDENT_CAN_LIBRARIES = [
    'AP_CANManager', 'AP_KDECAN', 'AP_PiccoloCAN', 'AP_PiccoloCAN/piccolo_protocol',
]
COMMON_VEHICLE_DEPENDENT_LIBRARIES = [
    'AP_AccelCal', 'AP_ADC', 'AP_AHRS', 'AP_Airspeed', 'AP_Baro', 'AP_BattMonitor',
    'AP_BoardConfig', 'AP_Camera', 'AP_Common', 'AP_Compass', 'AP_Declination', 'AP_GPS',
    'AP_GSOF', 'AP_HAL', 'AP_HAL_Empty', 'AP_InertialSensor', 'AP_Math',
    'AP_Mission', 'AP_DAL', 'AP_NavEKF', 'AP_NavEKF2', 'AP_NavEKF3', 'AP_Notify',
    'AP_OpticalFlow', 'AP_Param', 'AP_Rally', 'AP_RangeFinder', 'AP_Scheduler',
    'AP_SerialManager', 'AP_Terrain', 'AP_Vehicle', 'AP_InternalError', 'AP_Logger',
    'Filter', 'GCS_MAVLink', 'RC_Channel', 'SRV_Channel', 'StorageManager', 'AP_Tuning',
    'AP_RPM', 'AP_RSSI', 'AP_Mount', 'AP_Module', 'AP_Button', 'AP_ICEngine', 'AP_Networking',
    'AP_Frsky_Telem', 'AP_IBus_Telem', 'AP_FlashStorage', 'AP_Relay', 'AP_ServoRelayEvents',
    'AP_Volz_Protocol', 'AP_SBusOut', 'AP_IOMCU', 'AP_Parachute', 'AP_RAMTRON', 'AP_RCProtocol',
    'AP_Radio', 'AP_TempCalibration', 'AP_VisualOdom', 'AP_BLHeli', 'AP_ROMFS', 'AP_Proximity',
    'AP_Gripper', 'AP_RTC', 'AC_Sprayer', 'AC_Fence', 'AC_Avoidance', 'AP_LandingGear',
    'AP_RobotisServo', 'AP_NMEA_Output', 'AP_OSD', 'AP_Filesystem', 'AP_ADSB', 'AP_ADSB/sagetech-sdk',
    'AC_PID', 'AP_SerialLED', 'AP_EFI', 'AP_Hott_Telem', 'AP_ESC_Telem', 'AP_Servo_Telem',
    'AP_Stats', 'AP_GyroFFT', 'AP_RCTelemetry', 'AP_Generator', 'AP_MSP', 'AP_OLC',
    'AP_WheelEncoder', 'AP_ExternalAHRS', 'AP_VideoTX', 'AP_FETtecOneWire', 'AP_TemperatureSensor',
    'AP_Torqeedo', 'AP_CustomRotations', 'AP_AIS', 'AP_OpenDroneID', 'AP_CheckFirmware',
    'AP_ExternalControl', 'AP_JSON', 'AP_Beacon', 'AP_Arming', 'AP_RCMapper', 'AP_MultiHeap', 'AP_Follow',
]
IGNORED_AP_LIBRARIES = {'doc', 'AP_Scripting'}

# ArduCopter/wscript Copter-only libs
COPTER_LIBRARIES = [
    'AC_AttitudeControl', 'AC_InputManager', 'AC_PrecLand', 'AC_Sprayer', 'AC_Autorotation',
    'AC_WPNav', 'AP_Camera', 'AP_IRLock', 'AP_Motors', 'AP_Avoidance', 'AP_AdvancedFailsafe',
    'AP_SmartRTL', 'AP_WheelEncoder', 'AP_Winch', 'AP_LTM_Telem', 'AP_Devo_Telem',
    'AC_AutoTune', 'AP_KDECAN', 'AP_SurfaceDistance',
]

SOURCE_EXTS = ('*.S', '*.c', '*.cpp')

# Root-level SPI LLD experiments; production SPI uses hwdef/common board drv_spi_lld + SPIDevice CMSIS.
_AP_HAL_RTT_ROOT_EXCLUDE = frozenset([
    'hal_spi_lld.c',
    'hal_spi_lld_rtt.c',
])


def _libs_for_copter(with_can=True):
    out = list(COMMON_VEHICLE_DEPENDENT_LIBRARIES)
    if with_can:
        out.extend(COMMON_VEHICLE_DEPENDENT_CAN_LIBRARIES)
    out.extend(COPTER_LIBRARIES)
    return out


def _glob_library_sources(ap_root, lib_name):
    """Glob sources under libraries/<lib_name>: root *.S,*.c,*.cpp and utility/* (waf ant_glob equivalent)."""
    rel_dir = os.path.join('libraries', lib_name)
    abs_dir = os.path.join(ap_root, rel_dir)
    if not os.path.isdir(abs_dir):
        return []
    collected = []
    for ext in SOURCE_EXTS:
        for p in glob.glob(os.path.join(abs_dir, ext)):
            if os.path.isfile(p):
                collected.append(os.path.relpath(p, ap_root))
    util_dir = os.path.join(abs_dir, 'utility')
    if os.path.isdir(util_dir):
        for ext in SOURCE_EXTS:
            for p in glob.glob(os.path.join(util_dir, ext)):
                if os.path.isfile(p):
                    collected.append(os.path.relpath(p, ap_root))
    return collected


def _load_hwdef_env(bsp_dir):
    env_py = os.path.join(os.path.abspath(bsp_dir), 'env.py')
    if not os.path.isfile(env_py):
        return {}
    try:
        with open(env_py, 'rb') as f:
            data = pickle.load(f)
        return data if isinstance(data, dict) else {}
    except Exception as e:
        sys.stderr.write('warning: failed to load hwdef env.py %s: %s\n' % (env_py, e))
        return {}


def _env_truthy(env, name):
    return str(env.get(name, '0')).strip().lower() in ('1', 'true', 'yes', 'on')


def _glob_subdir_sources(ap_root, rel_dir):
    abs_dir = os.path.join(ap_root, rel_dir)
    if not os.path.isdir(abs_dir):
        return []
    collected = []
    for ext in SOURCE_EXTS:
        for p in glob.glob(os.path.join(abs_dir, ext)):
            if os.path.isfile(p):
                collected.append(os.path.relpath(p, ap_root))
    return collected


def _usb_backend_for_collect():
    scripts = os.path.join(os.path.dirname(os.path.abspath(__file__)))
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    import rtt_usb_backend
    return rtt_usb_backend.resolve_usb_backend()['backend']


def _append_bsp_include(paths, bsp_dir, hwdef_common, *rel_parts):
    """Prefer deploy BSP paths; fall back to hwdef/common when deploy is incomplete."""
    rel = os.path.join(*rel_parts)
    for base in (bsp_dir, hwdef_common):
        d = os.path.join(base, rel)
        if os.path.isdir(d) and d not in paths:
            paths.append(d)
            return


def _collect_sources(ap_root, bsp_dir, rtt_root):
    ap_root = os.path.abspath(ap_root)
    bsp_dir = os.path.abspath(bsp_dir)
    rtt_root = os.path.abspath(rtt_root)
    usb_backend = os.environ.get('RTT_USB_BACKEND', '').strip().lower() or _usb_backend_for_collect()
    sources = []

    # ArduCopter/*.cpp
    copter_dir = os.path.join(ap_root, 'ArduCopter')
    if os.path.isdir(copter_dir):
        for ext in SOURCE_EXTS:
            for p in glob.glob(os.path.join(copter_dir, ext)):
                if os.path.isfile(p):
                    sources.append(os.path.relpath(p, ap_root))

    # Libraries (common + Copter)
    seen_lib = set()
    for lib in _libs_for_copter():
        if lib in IGNORED_AP_LIBRARIES:
            continue
        if lib == 'AP_HAL_RTT':
            continue  # handled below (root-level .c/.cpp only)
        if lib in seen_lib:
            continue
        seen_lib.add(lib)
        sources.extend(_glob_library_sources(ap_root, lib))

    # AP_HAL_RTT: root-level .c and .cpp only (no rtt_bsp_*); exclude rtt_board_init.c (BSP provides rt_hw_board_init)
    hal_rtt = os.path.join(ap_root, 'libraries', 'AP_HAL_RTT')
    scripts = os.path.join(ap_root, 'Tools', 'scripts')
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    import rtt_usb_backend
    if os.path.isdir(hal_rtt):
        for ext in SOURCE_EXTS:
            for p in glob.glob(os.path.join(hal_rtt, ext)):
                if os.path.isfile(p):
                    name = os.path.basename(p)
                    if name in _AP_HAL_RTT_ROOT_EXCLUDE:
                        continue
                    if name == 'rtt_board_init.c':
                        continue
                    rel = os.path.relpath(p, ap_root)
                    if rel.startswith('libraries/AP_HAL_RTT/rtt_bsp_'):
                        continue
                    if not rtt_usb_backend.filter_ap_hal_rtt_source(rel, usb_backend):
                        continue
                    sources.append(rel)

    # SCons full-build for RTT can enable scripting from hwdef.h, so include
    # both AP_Scripting wrappers and bundled Lua runtime sources.
    sources.extend(_glob_library_sources(ap_root, 'AP_Scripting'))
    sources.extend(_glob_subdir_sources(ap_root, os.path.join('libraries', 'AP_Scripting', 'lua', 'src')))
    generated_lua_bindings = os.path.join(ap_root, 'build', 'rtt_cuav_v5', 'libraries', 'AP_Scripting', 'lua_generated_bindings.cpp')
    if os.path.isfile(generated_lua_bindings):
        sources.append(generated_lua_bindings)

    # BSP HAL, HAL_Drivers, system_stm32h7xx: not added here; RTT BSP already builds
    # board/ and packages/ via its SConscript, so we avoid duplicate symbols.

    if usb_backend == 'cherryusb':
        for rel in rtt_usb_backend.cherryusb_extra_sources(ap_root):
            if rel not in sources:
                sources.append(rel)

    hwdef_env = _load_hwdef_env(bsp_dir)
    if _env_truthy(hwdef_env, 'SIM_ENABLED'):
        sources.extend(_glob_library_sources(ap_root, 'SITL'))

    return sources


def _collect_cpppath(ap_root, bsp_dir, rtt_root, build_root, board="rtt_pixhawk6c_mini"):
    ap_root = os.path.abspath(ap_root)
    bsp_dir = os.path.abspath(bsp_dir)
    rtt_root = os.path.abspath(rtt_root)
    build_root = os.path.abspath(build_root) if build_root else ap_root
    usb_backend = os.environ.get('RTT_USB_BACKEND', '').strip().lower() or _usb_backend_for_collect()
    paths = []

    paths.append(build_root)
    paths.append(os.path.join(ap_root, 'build', board))
    paths.append(os.path.join(ap_root, 'libraries'))
    paths.append(os.path.join(ap_root, 'libraries', 'AP_Common', 'missing'))
    paths.append(os.path.join(ap_root, 'build', board, 'libraries'))
    paths.append(os.path.join(ap_root, 'build', board, 'libraries', 'GCS_MAVLink'))
    paths.append(os.path.join(ap_root, 'libraries', 'AP_HAL_RTT', 'include'))
    paths.append(os.path.join(ap_root, 'libraries', 'AP_HAL_RTT'))
    paths.append(os.path.join(rtt_root, 'include'))
    paths.append(os.path.join(rtt_root, 'bsp'))
    paths.append(os.path.join(rtt_root, 'libcpu', 'arm', 'cortex-m7'))
    finsh = os.path.join(rtt_root, 'components', 'finsh')
    if os.path.isdir(finsh):
        paths.append(finsh)
    rtdevice = os.path.join(rtt_root, 'components', 'drivers', 'include')
    if os.path.isdir(rtdevice):
        paths.append(rtdevice)
    paths.append(bsp_dir)
    paths.append(os.path.join(bsp_dir, 'board'))
    _hwdef_common = os.path.join(ap_root, 'libraries', 'AP_HAL_RTT', 'hwdef', 'common')
    _append_bsp_include(paths, bsp_dir, _hwdef_common, 'board', 'drivers_ll')
    stm32_lib = os.path.join(rtt_root, 'bsp', 'stm32', 'libraries', 'HAL_Drivers')
    for sub in ('', 'drivers', 'config', 'drivers/config'):
        d = os.path.join(stm32_lib, sub) if sub else stm32_lib
        if os.path.isdir(d):
            paths.append(d)

    # CMSIS-DSP include path (for arm_math.h used by AP_HAL_RTT/DSP.h)
    _cmsis_dsp = os.path.join(ap_root, 'libraries', 'AP_GyroFFT', 'CMSIS_5', 'include')
    if os.path.isdir(_cmsis_dsp):
        paths.append(_cmsis_dsp)

    if board == 'rtt_cuav_v5':
        for rel in (
            ('packages', 'stm32f7_hal_driver-latest', 'Inc'),
            ('board', 'CubeMX_Config', 'Inc'),
            ('packages', 'stm32f7_cmsis_driver-latest', 'Include'),
            ('packages', 'CMSIS-Core-latest', 'Include'),
        ):
            _append_bsp_include(paths, bsp_dir, _hwdef_common, *rel)
    else:
        for rel in (
            ('packages', 'stm32h7_hal_driver-latest', 'Inc'),
            ('board', 'CubeMX_Config', 'Inc'),
            ('packages', 'stm32h7_cmsis_driver-latest', 'Include'),
            ('packages', 'CMSIS-Core-latest', 'Include'),
        ):
            _append_bsp_include(paths, bsp_dir, _hwdef_common, *rel)
    # DroneCAN/libcanard include path (needed by AP_DroneCAN/AP_Canard_iface.h)
    dronecan_dir = os.path.join(ap_root, 'modules', 'DroneCAN', 'libcanard')
    if os.path.isdir(dronecan_dir):
        paths.append(dronecan_dir)
    # DroneCAN generated headers (dronecan_msgs.h etc.)
    dronecan_gen = os.path.join(ap_root, 'build', board, 'dronecan-gen', 'include')
    if os.path.isdir(dronecan_gen):
        paths.append(dronecan_gen)
    if usb_backend == 'cherryusb':
        scripts = os.path.join(ap_root, 'Tools', 'scripts')
        if scripts not in sys.path:
            sys.path.insert(0, scripts)
        import rtt_usb_backend
        paths.extend(rtt_usb_backend.cherryusb_extra_cpppath(ap_root))
    return paths


def _collect_defines_h7(sim_enabled=False):
    # Match boards.py is_h7 branch and RTT DEFINES for rtt_pixhawk6c_mini.
    defines = [
        'CONFIG_HAL_BOARD=HAL_BOARD_RTT',
        'USE_HAL_DRIVER=1',
        'STM32H743xx=1',
        'ARM_MATH_CM7=1',
        'USE_FLASH_ECC=0',
        'USE_SDIO_TRANSCEIVER=0',
        'USE_MULTI_CORE_SHARED_CODE=0',
        'USE_SPI_CRC=0',
        'LSI_VALUE=32000',
        'HAL_WITH_RAMTRON=1',
        'HAL_STORAGE_SIZE=32768',
        'LUA_32BITS=1',
        'BSP_USBD_SPEED_HSINFS=0',
        'BSP_USBD_PHY_UTMI=0',
        '__AP_LINE__=__LINE__',
        'APM_BUILD_DIRECTORY=APM_BUILD_ArduCopter',
        'AP_BUILD_TARGET_NAME="arducopter"',
        'FRAME_CONFIG=MULTICOPTER_FRAME',
        'AP_DDS_ENABLED=0',
    ]
    defines.append('AP_SIM_ENABLED=%d' % (1 if sim_enabled else 0))
    return defines


def _collect_defines_f7(sim_enabled=False):
    # STM32F765/CUAV V5 RTT BSP (STM32F767xx HAL used by BSP)
    defines = [
        'CONFIG_HAL_BOARD=HAL_BOARD_RTT',
        'USE_HAL_DRIVER=1',
        'STM32F767xx=1',
        'ARM_MATH_CM7=1',
        'HAL_STORAGE_SIZE=16384',
        'LUA_32BITS=1',
        '__AP_LINE__=__LINE__',
        'APM_BUILD_DIRECTORY=APM_BUILD_ArduCopter',
        'AP_BUILD_TARGET_NAME="arducopter"',
        'FRAME_CONFIG=MULTICOPTER_FRAME',
        'AP_DDS_ENABLED=0',
        'HAL_NUM_CAN_IFACES=0',
        'DRONECAN_CXX_WRAPPERS=1',
    ]
    defines.append('AP_SIM_ENABLED=%d' % (1 if sim_enabled else 0))
    return defines


def _collect_defines(board="rtt_pixhawk6c_mini", sim_enabled=False):
    if board == 'rtt_cuav_v5':
        return _collect_defines_f7(sim_enabled=sim_enabled)
    return _collect_defines_h7(sim_enabled=sim_enabled)


def main():
    parser = argparse.ArgumentParser(description='Generate scons ArduPilot config for RTT full build')
    parser.add_argument('ap_root', help='ArduPilot repo root (pogo-apm root)')
    parser.add_argument('--board', default='rtt_pixhawk6c_mini', help='Board name')
    parser.add_argument('--vehicle', default='ArduCopter', help='Vehicle (ArduCopter only supported)')
    parser.add_argument('--output', '-o', default='', help='Output path for scons_ardupilot_config.py (default: stdout or BSP dir)')
    parser.add_argument('--bsp-dir', default='', help='Explicit BSP directory to use for include path collection')
    parser.add_argument('--json', action='store_true', help='Print JSON to stdout instead of writing .py')
    args = parser.parse_args()

    ap_root = os.path.abspath(args.ap_root)
    if not os.path.isdir(ap_root):
        sys.stderr.write('error: AP_ROOT not a directory: %s\n' % ap_root)
        sys.exit(1)

    # BSP and RTT paths
    rtt_root = os.path.join(ap_root, 'modules', 'rt-thread')
    if args.bsp_dir:
        bsp_dir = os.path.abspath(args.bsp_dir)
    else:
        if args.board == 'rtt_cuav_v5':
            bsp_rel = 'stm32/stm32f765-cuav-v5'
        else:
            bsp_rel = 'stm32/stm32h743-pixhawk6c-mini'
        bsp_dir = os.path.join(rtt_root, 'bsp', bsp_rel)
    if not os.path.isdir(bsp_dir):
        sys.stderr.write('warning: BSP dir not found: %s\n' % bsp_dir)
    build_root = ap_root

    hwdef_env = _load_hwdef_env(bsp_dir)
    sim_enabled = _env_truthy(hwdef_env, 'SIM_ENABLED')

    ap_sources = _collect_sources(ap_root, bsp_dir, rtt_root)
    ap_cpppath = _collect_cpppath(ap_root, bsp_dir, rtt_root, build_root, args.board)
    ap_defines = _collect_defines(args.board, sim_enabled=sim_enabled)

    if args.json:
        out = json.dumps({'ap_sources': ap_sources, 'ap_cpppath': ap_cpppath, 'ap_defines': ap_defines}, indent=2)
        print(out)
        return

    py_content = '''# Generated by Tools/scripts/scons_ardupilot_sources.py - do not edit
ap_sources = %s
ap_cpppath = %s
ap_defines = %s
''' % (repr(ap_sources), repr(ap_cpppath), repr(ap_defines))

    if args.output:
        out_path = os.path.abspath(args.output)
        if os.path.isdir(out_path):
            out_path = os.path.join(out_path, 'scons_ardupilot_config.py')
        with open(out_path, 'w') as f:
            f.write(py_content)
        sys.stderr.write('Wrote %s\n' % out_path)
    else:
        print(py_content)


if __name__ == '__main__':
    main()
