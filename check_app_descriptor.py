#!/usr/bin/env python3
"""Verify app_descriptor CRC in rtthread.bin"""
import binascii, struct, sys

with open(sys.argv[1], 'rb') as f:
    img = f.read()

offset = 0x1f8
sig = b'\x40\xa2\xe4\xf1\x64\x68\x91\x06'

assert img[offset:offset+8] == sig, f'Signature not found at offset {offset}'

len1 = offset + 8
img1 = bytes(img[:len1])
crc1 = binascii.crc32(img1) & 0xFFFFFFFF

desc_len = 16
flash2_offset = offset + 8 + desc_len
img2 = bytes(img[flash2_offset:])
crc2 = binascii.crc32(img2) & 0xFFFFFFFF

crc1_stored, crc2_stored = struct.unpack_from('<II', img, offset+8)
image_size = struct.unpack_from('<I', img, offset+16)[0]
git_hash = struct.unpack_from('<I', img, offset+20)[0]
board_id = struct.unpack_from('<H', img, offset+26)[0]

print(f'CRC1: computed=0x{crc1:08x}, stored=0x{crc1_stored:08x}  {"OK" if crc1==crc1_stored else "MISMATCH!"}')
print(f'CRC2: computed=0x{crc2:08x}, stored=0x{crc2_stored:08x}  {"OK" if crc2==crc2_stored else "MISMATCH!"}')
print(f'image_size={image_size} (0x{image_size:x}), len(img)={len(img)}')
print(f'board_id=0x{board_id:04x}')
print(f'git_hash=0x{git_hash:08x}')
print(f'len1={len1} (0x{len1:x}), flash2_offset={flash2_offset} (0x{flash2_offset:x})')
print(f'CRC match: crc1={crc1 == crc1_stored}, crc2={crc2 == crc2_stored}')
