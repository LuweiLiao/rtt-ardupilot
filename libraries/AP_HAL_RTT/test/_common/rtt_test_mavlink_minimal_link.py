# MAVLink v2 generated headers for subsystem S_mavlink_usb (heartbeat-only smoke).
# Prefer build/{board}/libraries/GCS_MAVLink/include from a prior full scons build;
# fall back to committed build_old tree.

from __future__ import print_function

import os


def _ap_root_from_test_dir(test_subsystem_dir):
    return os.path.abspath(os.path.join(test_subsystem_dir, '..', '..', '..', '..', '..'))


def resolve_mavlink_v2_include(ap_root, board='rtt_cuav_v5'):
    candidates = [
        os.path.join(ap_root, 'build', board, 'libraries', 'GCS_MAVLink', 'include', 'mavlink', 'v2.0'),
        os.path.join(ap_root, 'build_old', board, 'libraries', 'GCS_MAVLink', 'include', 'mavlink', 'v2.0'),
        os.path.join(ap_root, 'build_old', 'rtt_cuav_v5', 'libraries', 'GCS_MAVLink', 'include', 'mavlink', 'v2.0'),
    ]
    for p in candidates:
        mavlink_h = os.path.join(p, 'ardupilotmega', 'mavlink.h')
        if os.path.isfile(mavlink_h):
            return p
    raise RuntimeError(
        'S_mavlink_usb: no mavlink v2 headers (ardupilotmega/mavlink.h). '
        'Run a full cuav_v5 scons once or keep build_old/rtt_cuav_v5/libraries/GCS_MAVLink/include')


def collect_mavlink_minimal_cpppath(test_subsystem_dir, board='rtt_cuav_v5'):
    ap_root = _ap_root_from_test_dir(test_subsystem_dir)
    return [resolve_mavlink_v2_include(ap_root, board=board)]
