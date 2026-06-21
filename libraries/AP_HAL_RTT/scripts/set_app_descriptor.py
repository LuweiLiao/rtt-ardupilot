#!/usr/bin/env python3
"""
set_app_descriptor.py — Post-build script for RTT (scons) ArduPilot firmware.

Fills the app_descriptor fields (image_crc1, image_crc2, image_size, git_hash)
in both .bin and .elf output files.

Reference: Tools/ardupilotwaf/chibios.py::set_app_descriptor (lines 266-328)

Usage:
    python3 set_app_descriptor.py <elf_path> <bin_path> <source_root>

The script:
1. Reads the .bin file
2. Finds the 8-byte descriptor signature
3. Computes CRC32 of image before and after the descriptor
4. Gets git hash from the repo
5. Writes crc1, crc2, image_size, git_hash back into .bin and .elf
"""

import os
import sys
import struct
import binascii
import subprocess


def crc32(data, state=0):
    """CRC32 matching ArduPilot's Tools/scripts/uploader.py (standard CRC-32)"""
    state = binascii.crc32(data, state)
    return state & 0xFFFFFFFF


def to_unsigned(i):
    """Convert possibly signed integer to unsigned 32-bit"""
    if i < 0:
        i += 2**32
    return i


def git_short_hash(source_root):
    """Return the short git hash, retrying with a scoped safe.directory.

    GitHub container jobs can run the build with a HOME/global git config that
    differs from actions/checkout's temporary setup, which makes git reject the
    workspace as dubious even after checkout configured it as safe.
    """
    source_root = os.path.abspath(source_root)
    cmd = ['git', '-C', source_root, 'rev-parse', '--short', 'HEAD']

    result = subprocess.run(
        cmd,
        capture_output=True, text=True, timeout=5
    )
    if result.returncode == 0:
        return result.stdout.strip(), None

    if 'dubious ownership' not in result.stderr:
        return None, result.stderr

    env = os.environ.copy()
    env['GIT_CONFIG_COUNT'] = '1'
    env['GIT_CONFIG_KEY_0'] = 'safe.directory'
    env['GIT_CONFIG_VALUE_0'] = source_root
    retry = subprocess.run(
        cmd,
        capture_output=True, text=True, timeout=5, env=env
    )
    if retry.returncode == 0:
        return retry.stdout.strip(), None

    return None, retry.stderr


def main():
    if len(sys.argv) < 4:
        print("Usage: set_app_descriptor.py <elf_path> <bin_path> <source_root>")
        sys.exit(1)

    elf_path = sys.argv[1]
    bin_path = sys.argv[2]
    source_root = sys.argv[3]

    # Validate files exist
    if not os.path.isfile(bin_path):
        print(f"WARNING: set_app_descriptor: bin not found: {bin_path}")
        return

    if not os.path.isfile(elf_path):
        print(f"WARNING: set_app_descriptor: elf not found: {elf_path}")
        return

    # Signatures (same as AP_CheckFirmware.h)
    SIG_UNSIGNED = b'\x40\xa2\xe4\xf1\x64\x68\x91\x06'
    SIG_SIGNED = b'\x41\xa3\xe5\xf2\x65\x69\x92\x07'

    # Read the binary
    with open(bin_path, 'rb') as f:
        img = bytearray(f.read())

    # Find the descriptor signature
    offset = img.find(SIG_UNSIGNED)
    if offset == -1:
        print("WARNING: set_app_descriptor: No APP_DESCRIPTOR found")
        return

    print(f"  APP_DESCRIPTOR found at offset 0x{offset:x}")

    # Skip past the 8-byte signature
    offset += len(SIG_UNSIGNED)

    # Unsigned descriptor: 16 bytes of CRC/size/hash (4 fields x 4 bytes)
    desc_len = 16

    # Compute CRC32 of image before descriptor fields
    img1 = bytes(img[:offset])
    crc1 = to_unsigned(crc32(img1))

    # Compute CRC32 of image after descriptor fields
    img2 = bytes(img[offset + desc_len:])
    crc2 = to_unsigned(crc32(img2))

    # Image size
    image_size = len(img)

    # Get git hash from the repo
    git_hash = 0
    try:
        git_hash_str, git_error = git_short_hash(source_root)
        if git_hash_str:
            git_hash = to_unsigned(int('0x' + git_hash_str, 16))
            print(f"  git_hash=0x{git_hash:08x} ({git_hash_str})")
        else:
            print(f"  WARNING: git rev-parse failed: {git_error}")
    except Exception as e:
        print(f"  WARNING: git hash error: {e}")

    # Mask to unsigned 32-bit
    crc1 = crc1 & 0xFFFFFFFF
    crc2 = crc2 & 0xFFFFFFFF
    image_size = image_size & 0xFFFFFFFF
    git_hash = git_hash & 0xFFFFFFFF
    print(f"  Packing: crc1=0x{crc1:08x} crc2=0x{crc2:08x} size={image_size} hash=0x{git_hash:08x}")
    # Pack descriptor fields: <IIII = crc1, crc2, image_size, git_hash
    desc = struct.pack('<IIII', crc1, crc2, image_size, git_hash)

    # Write back to .bin
    with open(bin_path, 'wb') as f:
        f.write(img[:offset])
        f.write(desc)
        f.write(img[offset + desc_len:])

    print(f"  Applied APP_DESCRIPTOR: crc1=0x{crc1:08x} crc2=0x{crc2:08x} size={image_size} hash=0x{git_hash:08x}")

    # Also update the .elf file (find zero-filled descriptor and replace)
    with open(elf_path, 'rb') as f:
        elf_img = bytearray(f.read())

    zero_descriptor = SIG_UNSIGNED + struct.pack("<IIII", 0, 0, 0, 0)
    elf_ofs = elf_img.find(zero_descriptor)

    if elf_ofs == -1:
        print("WARNING: set_app_descriptor: No zero-filled APP_DESCRIPTOR found in elf file")
        # The ELF might already have the descriptor filled from a previous run
        # Try finding the signature directly
        elf_ofs = elf_img.find(SIG_UNSIGNED)
        if elf_ofs == -1:
            print("WARNING: set_app_descriptor: No APP_DESCRIPTOR signature in elf either")
            return
        print(f"  Found existing (non-zero) descriptor in elf at 0x{elf_ofs:x}")
        # Check if already filled by checking if crc1/crc2 are non-zero
        current_crc1 = struct.unpack_from('<I', bytes(elf_img), elf_ofs + 8)[0]
        if current_crc1 != 0:
            print(f"  ELF descriptor already filled (crc1=0x{current_crc1:08x}), skipping")
            return
        elf_ofs += len(SIG_UNSIGNED)
    else:
        elf_ofs += len(SIG_UNSIGNED)

    with open(elf_path, 'wb') as f:
        f.write(elf_img[:elf_ofs])
        f.write(desc)
        f.write(elf_img[elf_ofs + desc_len:])

    print(f"  Applied APP_DESCRIPTOR to elf")


if __name__ == '__main__':
    main()
