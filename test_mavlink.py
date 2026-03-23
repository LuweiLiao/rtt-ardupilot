#!/usr/bin/env python3
import serial, time, struct, sys

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM26'
TIMEOUT = int(sys.argv[2]) if len(sys.argv) > 2 else 25

s = serial.Serial(PORT, 115200, timeout=1)
s.dtr = True
s.rts = True
time.sleep(0.5)
s.reset_input_buffer()

def crc16(data, extra):
    crc = 0xFFFF
    for b in data:
        tmp = b ^ (crc & 0xFF)
        tmp ^= (tmp << 4) & 0xFF
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)
        crc &= 0xFFFF
    tmp = extra ^ (crc & 0xFF)
    tmp ^= (tmp << 4) & 0xFF
    crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)
    return crc & 0xFFFF

def mav1(msgid, payload, extra_crc, seq=0):
    hdr = struct.pack('<BBBBB', len(payload), seq, 255, 0, msgid)
    msg = b'\xfe' + hdr + payload
    crc = crc16(msg[1:], extra_crc)
    return msg + struct.pack('<H', crc)

hb = mav1(0, struct.pack('<IBBBBB', 0, 6, 8, 0, 0, 3), 50)
prl = mav1(21, struct.pack('<BB', 1, 1), 159, 1)

for i in range(5):
    s.write(hb)
    time.sleep(0.1)
s.write(prl)

buf = b''
t0 = time.time()
while time.time() - t0 < TIMEOUT:
    s.write(hb)
    d = s.read(4096)
    buf += d
    time.sleep(0.2)

param_indices = set()
param_total = 0
msg_types = {}
i = 0
while i < len(buf):
    if buf[i] == 0xFD:
        if i + 10 > len(buf): break
        plen = buf[i+1]
        msgid = buf[i+7] | (buf[i+8] << 8) | (buf[i+9] << 16)
        total_len = 10 + plen + 2 + (1 if (buf[i+2] & 0x01) else 0)
        if i + total_len > len(buf): break
        msg_types[msgid] = msg_types.get(msgid, 0) + 1
        if msgid == 22 and plen >= 25:
            payload = buf[i+10:i+10+plen]
            pcount = (payload[5] << 8) | payload[4]
            pindex = (payload[7] << 8) | payload[6]
            param_indices.add(pindex)
            if param_total == 0: param_total = pcount
        i += total_len
    elif buf[i] == 0xFE:
        if i + 6 > len(buf): break
        plen = buf[i+1]
        total_len = 6 + plen + 2
        if i + total_len > len(buf): break
        msgid = buf[i+5]
        msg_types[msgid] = msg_types.get(msgid, 0) + 1
        i += total_len
    else:
        i += 1

print(f'Total bytes: {len(buf)}')
print(f'Message types: {msg_types}')
print(f'Unique params: {len(param_indices)}/{param_total}')
if param_total > 0 and len(param_indices) < param_total:
    missing = set(range(param_total)) - param_indices
    print(f'Missing: {len(missing)} indices')
elif param_total > 0:
    print('ALL PARAMS RECEIVED!')
s.close()
