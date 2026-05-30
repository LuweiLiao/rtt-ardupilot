#!/usr/bin/env python3
"""Host acceptance for --test=S_mavlink_usb: CDC MAVLink HEARTBEAT @ 921600."""

from __future__ import print_function

import glob
import os
import subprocess
import sys
import time

BAUD = 921600
VID_PID = '1209:5741'
HEARTBEAT_TIMEOUT_S = 20


def find_acm_port():
    by_id = glob.glob('/dev/serial/by-id/*1209*5741*')
    if by_id:
        return os.path.realpath(by_id[0])
    for dev in ('/dev/ttyACM1', '/dev/ttyACM0'):
        if os.path.exists(dev):
            return dev
    return None


def lsusb_ok():
    try:
        out = subprocess.check_output(['lsusb'], text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False
    return VID_PID.replace(':', ':') in out or '1209:5741' in out


def wait_heartbeat_pymavlink(port):
    from pymavlink import mavutil

    m = mavutil.mavlink_connection(port, baud=BAUD)
    hb = m.wait_heartbeat(timeout=HEARTBEAT_TIMEOUT_S)
    if hb is None:
        return None
    return {
        'msgid': hb.get_msgId(),
        'type': hb.type,
        'autopilot': hb.autopilot,
        'system_status': hb.system_status,
        'sysid': hb.get_srcSystem(),
        'compid': hb.get_srcComponent(),
    }


def main():
    if not lsusb_ok():
        print('FAIL: lsusb missing {}'.format(VID_PID), flush=True)
        return 1

    port = find_acm_port()
    if not port:
        print('FAIL: no ACM device for {}'.format(VID_PID), flush=True)
        return 1

    print('Using port {} @ {}'.format(port, BAUD), flush=True)
    time.sleep(1.0)

    try:
        info = wait_heartbeat_pymavlink(port)
    except Exception as exc:
        print('FAIL: pymavlink error: {}'.format(exc), flush=True)
        return 1

    if not info:
        print('FAIL: no HEARTBEAT within {}s'.format(HEARTBEAT_TIMEOUT_S), flush=True)
        return 1

    if info['msgid'] != 0:
        print('FAIL: expected msgid 0 got {}'.format(info['msgid']), flush=True)
        return 1

    print('PASS HEARTBEAT msgid=0 sys={} comp={} type={} autopilot={} status={}'.format(
        info['sysid'], info['compid'], info['type'], info['autopilot'], info['system_status']),
        flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
