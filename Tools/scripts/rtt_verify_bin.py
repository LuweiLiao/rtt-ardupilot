#!/usr/bin/env python3
"""
Verify the rtthread.bin integrity: checks the Reset_Handler literal pool
values in the .bin match the expected VTOR (0x08008000).

Run after objcopy to catch binary corruption early.

Usage: python3 rtt_verify_bin.py <rtthread.bin> [rt-thread.elf]
"""
import struct
import subprocess
import sys
import os
import glob
import shutil


def find_tool(tool):
    """Resolve an ARM tool from PATH, common env vars, or bundled /opt toolchains."""
    candidates = []
    for env_name in ('ARM_NONE_EABI_PATH', 'RTT_EXEC_PATH', 'TOOLCHAIN_PATH'):
        root = os.environ.get(env_name)
        if root:
            candidates.append(os.path.join(root, tool))
            candidates.append(os.path.join(root, 'bin', tool))

    path_tool = shutil.which(tool)
    if path_tool:
        candidates.append(path_tool)

    if tool == 'arm-none-eabi-gdb':
        # Ubuntu CI images may provide the ARM-capable debugger as
        # gdb-multiarch instead of arm-none-eabi-gdb.
        multiarch_gdb = shutil.which('gdb-multiarch')
        if multiarch_gdb:
            candidates.append(multiarch_gdb)

    candidates.extend(sorted(glob.glob(f'/opt/gcc-arm-none-eabi*/bin/{tool}')))

    seen = set()
    for candidate in candidates:
        candidate = os.path.abspath(os.path.expanduser(candidate))
        if candidate in seen:
            continue
        seen.add(candidate)
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    return tool


def read_elf_literal_pool(elf_path, pool_vma):
    """Use GDB to read 4 words from ELF at a VMA."""
    try:
        gdb = find_tool('arm-none-eabi-gdb')
        r = subprocess.run(
            [gdb, '-batch',
             '-ex', f'file {elf_path}',
             '-ex', f'x/4wx {pool_vma:#x}',
             '-ex', 'quit'],
            capture_output=True, text=True, timeout=30
        )
        for line in r.stdout.split('\n'):
            if line.startswith(f'0x{pool_vma:x}') or \
               line.startswith(f'0x{pool_vma:X}'):
                parts = line.strip().split(':')
                if len(parts) >= 2:
                    vals = parts[1].strip().split()
                    if len(vals) >= 4:
                        return [int(v, 16) for v in vals[:4]]
    except Exception as e:
        print(f"Warning: GDB error reading ELF: {e}", file=sys.stderr)
    return None


def verify_binary(bin_path, elf_path):
    """Verify the binary's Reset_Handler literal pool is correct."""
    if not os.path.isfile(bin_path):
        print(f"ERROR: {bin_path} not found", file=sys.stderr)
        return False

    with open(bin_path, 'rb') as f:
        data = f.read()

    # The .text section starts at VMA 0x08008000
    TEXT_VMA = 0x08008000

    # Find Reset_Handler by looking for SCB_VTOR literal (0xe000ed08)
    # in a 4-word pattern: [MSP, 0x08008000, 0xe000ed08, 0x20000000]
    pool_expected = None
    pool_actual = None
    pool_offset = None

    for offset in range(0, len(data) - 16, 4):
        w = struct.unpack('<I', data[offset:offset+4])[0]
        if w == 0xe000ed08:  # SCB_VTOR register address
            if offset >= 8:
                pool = struct.unpack('<4I', data[offset-8:offset+8])
                # Check if this is the Reset_Handler pool:
                # pool[0] = MSP (in SRAM, 0x200xxxxx)
                # pool[1] = VTOR (should be 0x08008000)
                # pool[2] = SCB_VTOR addr (0xe000ed08)
                # pool[3] = RAM base (0x20000000)
                if (pool[0] & 0xF0000000) == 0x20000000 and \
                   pool[1] == TEXT_VMA and \
                   pool[2] == 0xe000ed08 and \
                   pool[3] == 0x20000000:
                    pool_expected = list(pool)
                    pool_offset = offset - 8
                    pool_actual = list(pool)
                    break

    if pool_expected is None:
        # Fall back to GDB ELF comparison
        print("WARNING: Reset_Handler literal pool not found by pattern.", file=sys.stderr)
        print("Falling back to GDB ELF comparison...", file=sys.stderr)

        if os.path.isfile(elf_path):
            try:
                gdb = find_tool('arm-none-eabi-gdb')
                r = subprocess.run(
                    [gdb, '-batch',
                     '-ex', f'file {elf_path}',
                     '-ex', 'disassemble Reset_Handler',
                     '-ex', 'quit'],
                    capture_output=True, text=True, timeout=30
                )
                for line in r.stdout.split('\n'):
                    if 'ldr' in line and 'pc' in line and ';' in line:
                        parts = line.split(';')
                        if len(parts) >= 2:
                            addr_str = parts[-1].strip().rstrip()
                            try:
                                addr = int(addr_str, 16)
                                vma = addr
                                bin_off = vma - TEXT_VMA
                                if 0 <= bin_off <= len(data) - 16:
                                    pool = struct.unpack('<4I', data[bin_off:bin_off+16])
                                    pool_actual = list(pool)
                                    pool_offset = bin_off
                                    elf_vals = read_elf_literal_pool(elf_path, vma)
                                    if elf_vals:
                                        pool_expected = elf_vals
                                    break
                            except ValueError:
                                continue
            except Exception as e:
                print(f"GDB error: {e}", file=sys.stderr)

    if pool_expected is None or pool_actual is None:
        print("ERROR: Could not determine expected/actual literal pool values", file=sys.stderr)
        return False

    # Compare
    match = pool_expected == pool_actual
    vma = TEXT_VMA + (pool_offset or 0)

    print(f"Reset_Handler literal pool (VMA 0x{vma:08X}, bin offset 0x{pool_offset:X}):")
    print(f"  Expected: [{', '.join(f'0x{v:08x}' for v in pool_expected)}]")
    print(f"  Actual:   [{', '.join(f'0x{v:08x}' for v in pool_actual)}]")

    if match:
        print("✅ Binary integrity check PASSED — Reset_Handler literal pool correct")
        return True
    else:
        print("❌ Binary integrity check FAILED — Reset_Handler literal pool mismatch!")
        print(f"   VTOR value mismatch: expected 0x{TEXT_VMA:08X}, got 0x{pool_actual[1]:08X}")
        print(f"")
        print(f"   TROUBLESHOOTING:")
        print(f"   1. Ensure .bin is burned at 0x{TEXT_VMA:08X} (not 0x08000000)")
        print(f"   2. Check if linker script ORIGIN matches expected VMA")
        print(f"   3. Verify objcopy produces contiguous sections without gaps")
        return False


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <rtthread.bin> [rt-thread.elf]", file=sys.stderr)
        sys.exit(1)

    bin_path = os.path.abspath(sys.argv[1])
    elf_path = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else ''

    ok = verify_binary(bin_path, elf_path)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
