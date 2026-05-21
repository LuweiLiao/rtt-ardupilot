#!/usr/bin/env python3
"""Compare flash readback vectors with .bin file contents"""
import struct

with open('/data/firmare/pogo-apm/build/rtt_deploy/cuav_v5/rtthread.bin', 'rb') as f:
    bin_data = f.read()

# Readback from OpenOCD: 0x08008000: 200054b4 080ef00d 080ef071 080083ad 08070781 08070761 08070771 00000000
readback_vectors = [
    0x200054b4, 0x080ef00d, 0x080ef071, 0x080083ad,
    0x08070781, 0x08070761, 0x08070771, 0x00000000,
]

names = ['MSP','Reset_Handler','NMI','HardFault','MemManage','BusFault','UsageFault','(reserved)']

print('Comparing vector table (flash @ 0x08008000 vs .bin):')
all_match = True
for i, (rb, name) in enumerate(zip(readback_vectors, names)):
    bin_val = struct.unpack('<I', bin_data[i*4:(i+1)*4])[0]
    match_str = 'OK' if bin_val == rb else 'MISMATCH'
    if match_str != 'OK':
        all_match = False
    print(f'  [{name:15s}] flash=0x{rb:08X}  bin=0x{bin_val:08X}  [{match_str}]')

if all_match:
    print('\n*** VECTOR TABLE VERIFIED: flash contents match .bin file ***')
else:
    print('\n*** WARNING: VECTOR TABLE MISMATCH ***')
    exit(1)
