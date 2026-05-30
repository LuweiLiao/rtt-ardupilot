# AP_Compass subsystem smoke (S_compass): HAL I2C whitelist + Compass + Declination.
# Keeps other compass drivers disabled via CXXFLAGS in S_compass/SConscript.

from __future__ import print_function

import os
import sys

from rtt_test_hal_i2c_link import (
    collect_hal_i2c_smoke_cpppath,
    collect_hal_i2c_smoke_sources,
)
from rtt_test_hal_uart_link import (
    _ap_root_from_test_dir,
    _glob_lib_sources,
)

_EXTRA_LIBS = (
    'AP_Compass',
    'AP_Declination',
)

# Not linked in S_compass (COMPASS_MOT_ENABLED=0 / COMPASS_LEARN_ENABLED=0).
_EXCLUDE_AP_COMPASS_CPP = frozenset([
    'Compass_PerMotor.cpp',
    'Compass_learn.cpp',
])


def collect_compass_subsystem_sources(test_subsystem_dir):
    ap_root, sources = collect_hal_i2c_smoke_sources(test_subsystem_dir)
    seen = set(sources)
    for lib in _EXTRA_LIBS:
        for p in _glob_lib_sources(ap_root, lib):
            base = os.path.basename(p)
            if base in _EXCLUDE_AP_COMPASS_CPP:
                continue
            if p not in seen:
                seen.add(p)
                sources.append(p)
    return ap_root, sources


def collect_compass_subsystem_cpppath(ap_root, bsp_dir, board='rtt_cuav_v5'):
    return collect_hal_i2c_smoke_cpppath(ap_root, bsp_dir, board=board)
