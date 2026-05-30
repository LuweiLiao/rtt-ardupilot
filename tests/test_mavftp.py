#!/usr/bin/env python3
"""MAVFTP comprehensive test for AP_HAL_RTT on CUAV v5"""

import argparse
import os
import sys
import time
import struct
from pymavlink import mavutil

DEFAULT_PORT = "/dev/ttyACM1"
TIMEOUT = 15

FTP_OP_None         = 0
FTP_OP_TermSession  = 1
FTP_OP_ResetSession = 2
FTP_OP_ListDirectory = 3
FTP_OP_OpenFileRO   = 4
FTP_OP_ReadFile     = 5
FTP_OP_CreateFile   = 6
FTP_OP_WriteFile    = 7
FTP_OP_RemoveFile   = 8
FTP_OP_CreateDir    = 9
FTP_OP_RemoveDir    = 10
FTP_OP_OpenFileWO   = 11
FTP_OP_TruncateFile = 12
FTP_OP_Rename       = 13
FTP_OP_CalcFileCRC32 = 14
FTP_OP_BurstReadFile = 15
FTP_OP_Ack          = 128
FTP_OP_Nack         = 129

FTP_ERR_None        = 0
FTP_ERR_Fail        = 1
FTP_ERR_FailErrno   = 2
FTP_ERR_InvalidData = 3
FTP_ERR_InvalidSess = 4
FTP_ERR_NoSess      = 5
FTP_ERR_EOF         = 6
FTP_ERR_UnknownCmd  = 7
FTP_ERR_FileExists  = 8
FTP_ERR_FileProtect = 9
FTP_ERR_FileNotFound = 10

class MavFTP:
    def __init__(self, conn):
        self.conn = conn
        self.seq = 0
        self.target_sys = conn.target_system
        self.target_comp = conn.target_component

    def _send(self, opcode, session=0, offset=0, size=0, data=b''):
        self.seq += 1
        payload = bytearray(251)
        struct.pack_into('<H', payload, 0, self.seq)
        payload[2] = session
        payload[3] = opcode
        payload[4] = len(data) if data else size
        payload[5] = 0  # req_opcode
        payload[6] = 0  # burst_complete
        payload[7] = 0  # padding
        struct.pack_into('<I', payload, 8, offset)
        if data:
            payload[12:12+len(data)] = data
        # pymavlink 2.4+ expects Sequence[int], not bytes/bytearray
        self.conn.mav.file_transfer_protocol_send(
            0, self.target_sys, self.target_comp, list(payload))

    def _recv(self, timeout=TIMEOUT):
        t0 = time.time()
        while time.time() - t0 < timeout:
            msg = self.conn.recv_match(type='FILE_TRANSFER_PROTOCOL',
                                       blocking=True, timeout=1)
            if msg:
                p = msg.payload
                if isinstance(p, list):
                    p = bytes(p)
                seq = struct.unpack_from('<H', p, 0)[0]
                session = p[2]
                opcode = p[3]
                size = p[4]
                req_opcode = p[5]
                burst = p[6]
                offset = struct.unpack_from('<I', p, 8)[0]
                data = bytes(p[12:12+size])
                return {
                    'seq': seq, 'session': session, 'opcode': opcode,
                    'size': size, 'req_opcode': req_opcode, 'burst': burst,
                    'offset': offset, 'data': data
                }
        return None

    def list_directory(self, path, offset=0):
        self._send(FTP_OP_ListDirectory, offset=offset,
                   data=path.encode() + b'\x00')
        return self._recv()

    def open_file_ro(self, path):
        self._send(FTP_OP_OpenFileRO, data=path.encode() + b'\x00')
        return self._recv()

    def read_file(self, session, offset, size):
        self._send(FTP_OP_ReadFile, session=session, offset=offset, size=size)
        return self._recv()

    def term_session(self, session):
        self._send(FTP_OP_TermSession, session=session)
        return self._recv()

    def reset_sessions(self):
        self._send(FTP_OP_ResetSession)
        return self._recv()

    def create_file(self, path):
        self._send(FTP_OP_CreateFile, data=path.encode() + b'\x00')
        return self._recv()

    def write_file(self, session, offset, data):
        self._send(FTP_OP_WriteFile, session=session, offset=offset, data=data)
        return self._recv()

    def remove_file(self, path):
        self._send(FTP_OP_RemoveFile, data=path.encode() + b'\x00')
        return self._recv()

    def create_dir(self, path):
        self._send(FTP_OP_CreateDir, data=path.encode() + b'\x00')
        return self._recv()

    def remove_dir(self, path):
        self._send(FTP_OP_RemoveDir, data=path.encode() + b'\x00')
        return self._recv()


def opcode_name(op):
    names = {128: 'Ack', 129: 'Nack', 0: 'None'}
    return names.get(op, f'op{op}')


def test_list_root(ftp):
    """T1: List root directory"""
    print("T1: ListDirectory '/' ...", end=' ')
    entries = []
    offset = 0
    while True:
        resp = ftp.list_directory('/', offset)
        if resp is None:
            print("FAIL (timeout)")
            return False
        if resp['opcode'] == FTP_OP_Nack:
            if resp['data'] and resp['data'][0] == FTP_ERR_EOF:
                break
            print(f"FAIL (Nack err={resp['data'][0] if resp['data'] else '?'})")
            return False
        if resp['opcode'] == FTP_OP_Ack:
            raw = resp['data']
            parts = raw.split(b'\x00')
            for p in parts:
                if p:
                    entries.append(p.decode(errors='replace'))
            offset += len([p for p in parts if p])
        else:
            print(f"FAIL (unexpected opcode {opcode_name(resp['opcode'])})")
            return False

    if len(entries) > 0:
        print(f"PASS ({len(entries)} entries: {entries[:5]}...)")
        return True
    else:
        print("FAIL (0 entries)")
        return False


def test_list_apm(ftp):
    """T2: List /APM directory"""
    print("T2: ListDirectory '/APM' ...", end=' ')
    resp = ftp.list_directory('/APM')
    if resp is None:
        print("FAIL (timeout)")
        return "fail"
    if resp['opcode'] == FTP_OP_Ack:
        raw = resp['data']
        parts = [p.decode(errors='replace') for p in raw.split(b'\x00') if p]
        print(f"PASS ({len(parts)} entries: {parts[:5]})")
        return "pass"
    elif resp['opcode'] == FTP_OP_Nack:
        err = resp['data'][0] if resp['data'] else -1
        if err == FTP_ERR_FileNotFound:
            print("SKIP (/APM not found - no SD card?)")
            return "skip"
        print(f"FAIL (Nack err={err})")
        return "fail"
    print(f"FAIL (opcode={opcode_name(resp['opcode'])})")
    return "fail"


def test_read_param_file(ftp):
    """T3: Open and read @PARAM/param.pck (ROMFS virtual file)"""
    print("T3: Read @PARAM/param.pck ...", end=' ')
    resp = ftp.open_file_ro('@PARAM/param.pck')
    if resp is None:
        print("FAIL (timeout on open)")
        return False
    if resp['opcode'] == FTP_OP_Nack:
        err = resp['data'][0] if resp['data'] else -1
        print(f"FAIL (Nack on open, err={err})")
        return False
    if resp['opcode'] != FTP_OP_Ack:
        print(f"FAIL (unexpected opcode {opcode_name(resp['opcode'])})")
        return False

    session = resp['session']
    file_size = struct.unpack_from('<I', resp['data'], 0)[0] if len(resp['data']) >= 4 else 0
    print(f"opened session={session} size={file_size} ...", end=' ')

    total_read = 0
    offset = 0
    while True:
        resp = ftp.read_file(session, offset, 239)
        if resp is None:
            print(f"FAIL (timeout at offset {offset})")
            ftp.term_session(session)
            return False
        if resp['opcode'] == FTP_OP_Nack:
            if resp['data'] and resp['data'][0] == FTP_ERR_EOF:
                break
            print(f"FAIL (Nack at offset {offset}, err={resp['data'][0] if resp['data'] else '?'})")
            ftp.term_session(session)
            return False
        if resp['opcode'] == FTP_OP_Ack:
            total_read += len(resp['data'])
            offset += len(resp['data'])
        else:
            break

    ftp.term_session(session)
    if total_read > 0:
        print(f"PASS (read {total_read} bytes)")
        return True
    print("FAIL (0 bytes read)")
    return False


def test_write_read_delete(ftp):
    """T4: Create file, write, read back, delete (requires SD card)"""
    test_path = '/APM/test_ftp.tmp'
    test_data = b'AP_HAL_RTT FTP test 1234567890\n'
    print(f"T4: Write/Read/Delete {test_path} ...", end=' ')

    resp = ftp.create_file(test_path)
    if resp is None:
        print("FAIL (timeout on create)")
        return "fail"
    if resp['opcode'] == FTP_OP_Nack:
        err = resp['data'][0] if resp['data'] else -1
        if err == FTP_ERR_FileNotFound:
            print("SKIP (no /APM directory)")
            return "skip"
        print(f"FAIL (Nack on create, err={err})")
        return "fail"

    session = resp['session']

    resp = ftp.write_file(session, 0, test_data)
    if resp is None or resp['opcode'] != FTP_OP_Ack:
        print("FAIL (write failed)")
        ftp.term_session(session)
        return "fail"

    ftp.term_session(session)

    resp = ftp.open_file_ro(test_path)
    if resp is None or resp['opcode'] != FTP_OP_Ack:
        print("FAIL (re-open failed)")
        return "fail"

    session = resp['session']
    resp = ftp.read_file(session, 0, 239)
    if resp is None or resp['opcode'] != FTP_OP_Ack:
        print("FAIL (read back failed)")
        ftp.term_session(session)
        return "fail"

    read_data = resp['data']
    ftp.term_session(session)

    if read_data[:len(test_data)] != test_data:
        print(f"FAIL (data mismatch: got {read_data[:30]})")
        return "fail"

    resp = ftp.remove_file(test_path)
    if resp is None or resp['opcode'] != FTP_OP_Ack:
        print("WARN (delete failed, but read was OK)")

    print("PASS")
    return "pass"


def test_reset_sessions(ftp):
    """T5: Reset all sessions"""
    print("T5: ResetSessions ...", end=' ')
    resp = ftp.reset_sessions()
    if resp is None:
        print("FAIL (timeout)")
        return False
    if resp['opcode'] == FTP_OP_Ack:
        print("PASS")
        return True
    print(f"FAIL (opcode={opcode_name(resp['opcode'])})")
    return False


def test_stability_check(conn):
    """T6: Post-FTP stability check - verify board hasn't crashed"""
    print("T6: Post-FTP stability (heartbeat + stream) ...", end=' ')
    hb = conn.wait_heartbeat(timeout=5)
    if not hb:
        print("FAIL (no heartbeat - board may have crashed!)")
        return False

    conn.mav.request_data_stream_send(
        conn.target_system, conn.target_component, 0, 4, 1)
    msgs = 0
    t0 = time.time()
    while time.time() - t0 < 3:
        msg = conn.recv_match(blocking=True, timeout=1)
        if msg:
            msgs += 1
    if msgs > 5:
        print(f"PASS ({msgs} msgs in 3s)")
        return True
    print(f"FAIL (only {msgs} msgs)")
    return False


def normalize_result(raw):
    """Map bool or tri-state string to pass/skip/fail."""
    if raw is True:
        return "pass"
    if raw is False:
        return "fail"
    if raw in ("pass", "skip", "fail"):
        return raw
    return "fail"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="MAVFTP comprehensive test for AP_HAL_RTT")
    parser.add_argument(
        "--port",
        default=None,
        help=f"MAVLink serial port (default: MAVFTP_PORT env or {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--require-sd",
        action="store_true",
        help="Treat SKIP (no SD /APM) as FAIL",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    port = args.port or os.environ.get("MAVFTP_PORT", DEFAULT_PORT)

    print("=== MAVFTP Comprehensive Test ===")
    print(f"Port: {port}")
    if args.require_sd:
        print("Mode: --require-sd (SKIP counts as FAIL)")

    time.sleep(0.5)
    conn = mavutil.mavlink_connection(port, baud=115200)
    time.sleep(0.5)
    hb = conn.wait_heartbeat(timeout=10)
    if not hb:
        print("FATAL: No heartbeat")
        return 1

    print(f"Connected: sys={conn.target_system} comp={conn.target_component}")
    print()

    ftp = MavFTP(conn)

    results = {}
    tests = [
        ('T1_list_root', lambda: test_list_root(ftp)),
        ('T2_list_apm', lambda: test_list_apm(ftp)),
        ('T3_read_param', lambda: test_read_param_file(ftp)),
        ('T4_write_read_del', lambda: test_write_read_delete(ftp)),
        ('T5_reset_sessions', lambda: test_reset_sessions(ftp)),
        ('T6_stability', lambda: test_stability_check(conn)),
    ]

    for name, fn in tests:
        try:
            results[name] = normalize_result(fn())
        except Exception as e:
            print(f"  EXCEPTION: {e}")
            results[name] = "fail"
        time.sleep(0.5)

    print()
    print("=" * 50)
    by_status = {"pass": [], "skip": [], "fail": []}
    for name, status in results.items():
        by_status[status].append(name)

    pass_n = len(by_status["pass"])
    skip_n = len(by_status["skip"])
    fail_n = len(by_status["fail"])
    total = len(results)

    print(f"Summary: {pass_n} PASS, {skip_n} SKIP, {fail_n} FAIL (of {total} tests)")
    print(f"MAVFTP pass count: {pass_n}/{total} (6/6 only when all tests PASS)")
    print()
    for label in ("PASS", "SKIP", "FAIL"):
        key = label.lower()
        names = by_status[key]
        if names:
            print(f"{label}:")
            for name in names:
                print(f"  {name}")

    effective_fail = fail_n
    if args.require_sd:
        effective_fail += skip_n

    conn.close()
    if effective_fail > 0:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
