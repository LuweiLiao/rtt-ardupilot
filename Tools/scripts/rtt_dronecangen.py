#!/usr/bin/env python3
"""
Generate DroneCAN DSDL C headers/sources for the RTT SCons build.

This mirrors the waf dronecangen step used by ChibiOS builds, but keeps the
output under build/<board>/dronecan-gen so generated files stay out of source.
"""

import os
import subprocess
import sys


def _dsdl_dirs(ap_root):
    dirs = []
    for rel in (
        os.path.join('modules', 'DroneCAN', 'DSDL'),
        os.path.join('libraries', 'AP_DroneCAN', 'dsdl'),
    ):
        base = os.path.join(ap_root, rel)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            path = os.path.join(base, name)
            if os.path.isdir(path) and name[:1].islower():
                dirs.append(path)
    return dirs


def main():
    if len(sys.argv) != 3:
        print('usage: rtt_dronecangen.py <AP_ROOT> <board>', file=sys.stderr)
        return 2

    ap_root = os.path.abspath(sys.argv[1])
    board = sys.argv[2]
    dsdlc = os.path.join(ap_root, 'modules', 'DroneCAN', 'dronecan_dsdlc', 'dronecan_dsdlc.py')
    if not os.path.isfile(dsdlc):
        print('error: missing DroneCAN DSDLC compiler: %s' % dsdlc, file=sys.stderr)
        return 1

    source_dirs = _dsdl_dirs(ap_root)
    if not source_dirs:
        print('error: no DroneCAN DSDL directories found', file=sys.stderr)
        return 1

    out_dir = os.path.join(ap_root, 'build', board, 'dronecan-gen')
    os.makedirs(out_dir, exist_ok=True)
    cmd = [sys.executable, dsdlc, '-O%s' % out_dir] + source_dirs
    ret = subprocess.call(cmd, cwd=ap_root)
    if ret != 0:
        print('error: DroneCAN DSDLC failed with %d' % ret, file=sys.stderr)
        return ret

    header = os.path.join(out_dir, 'include', 'dronecan_msgs.h')
    if not os.path.isfile(header):
        print('error: expected generated header not found: %s' % header, file=sys.stderr)
        return 1

    print('Generated DroneCAN DSDL -> %s' % out_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
