#!/usr/bin/env python3
# encoding: utf-8
"""
Deploy RTT BSP from ArduPilot repo to build/rtt_deploy/<target>.
Used by root SConstruct when building with --target=<cuav_v5>.

Two deployment modes:
  1. hwdef mode (preferred): Copy hwdef/common/ template + run rtt_hwdef.py
     to generate board-specific files (hwdef.h, HAL MSP, rtconfig.h, linker).
     Adding a new board = adding a hwdef/<board>/hwdef.dat file.
  2. Legacy mode: Copy full BSP directory (for boards not yet migrated).

Usage: python3 rtt_bsp_deploy.py <AP_ROOT> <TARGET>
  On success prints BSP deploy absolute path (bsp_deploy_abspath) to stdout.
  Exit 0 on success, non-zero on error.
"""

import argparse
import contextlib
import os
import pickle
import shutil
import subprocess
import sys


# Target definitions.
# 'hwdef' key → new mode (common template + parser generation)
# 'bsp_src_rel' key → legacy mode (full BSP directory copy)
RTT_TARGETS = {
    'cuav_v5': {
        'hwdef': 'libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat',
        'common': 'libraries/AP_HAL_RTT/hwdef/common',
        'msp_src_rel': 'modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.c',
        'ports_rel': 'modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports',
    },
    # Legacy targets (not yet migrated to hwdef/common; archived full-tree BSP):
    'pixhawk6c_mini': {
        'bsp_src_rel': 'libraries/AP_HAL_RTT/archive/stray-bsp/rtt_bsp_pixhawk6c_mini',
    },
}

RTT_TARGET_ALIASES = {
    'pixhawk6c_mini': 'pixhawk6c_mini',
    'pixhawk6c-mini': 'pixhawk6c_mini',
    'cuav_v5': 'cuav_v5',
    'cuav-v5': 'cuav_v5',
    'cuav v5': 'cuav_v5',
    'rtt_cuav_v5': 'cuav_v5',
    'rtt-cuav-v5': 'cuav_v5',
}

# Parser script (relative to AP root)
HWDEF_PARSER = 'libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py'

# Packages required per target (used for both modes)
REQUIRED_PACKAGES = {
    'cuav_v5': ['CMSIS-Core-latest', 'stm32f7_cmsis_driver-latest', 'stm32f7_hal_driver-latest'],
    'pixhawk6c_mini': ['CMSIS-Core-latest', 'stm32h7_cmsis_driver-latest', 'stm32h7_hal_driver-latest'],
}


def _norm(s):
    return s.replace('/', os.sep)


def normalize_target(target):
    if not target:
        return ''
    key = str(target).strip().lower().replace('\\', '/')
    key = key.replace('/', ' ')
    key = ' '.join(key.split())
    variants = {
        key,
        key.replace('-', '_'),
        key.replace('-', ' '),
        key.replace('_', ' '),
        key.replace(' ', '_'),
    }
    for variant in variants:
        if variant in RTT_TARGET_ALIASES:
            return RTT_TARGET_ALIASES[variant]
    return ''


@contextlib.contextmanager
def _target_deploy_lock(ap_root, target):
    """
    Serialize deploy-directory updates for one target.

    Root SCons holds this same lock across deploy + BSP SCons + packaging, so
    the deploy helper must detect that parent-held state to avoid self-locking.
    """
    if os.environ.get('RTT_TARGET_LOCK_HELD') == target:
        yield
        return

    lock_dir = os.path.join(ap_root, 'build', 'rtt_deploy')
    os.makedirs(lock_dir, exist_ok=True)
    lock_path = os.path.join(lock_dir, '%s.lock' % target)
    fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o644)
    try:
        import fcntl
        print('RTT deploy lock: waiting for %s' % lock_path, file=sys.stderr)
        fcntl.flock(fd, fcntl.LOCK_EX)
        os.ftruncate(fd, 0)
        os.write(fd, ('pid=%s target=%s\n' % (os.getpid(), target)).encode('ascii'))
        print('RTT deploy lock: acquired %s' % lock_path, file=sys.stderr)
        yield
    finally:
        try:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_UN)
        finally:
            os.close(fd)


def deploy(ap_root, target):
    """
    Deploy BSP for target to build/rtt_deploy/<canonical_target>.
    Returns (bsp_deploy_abspath, None) on success, (None, error_msg) on failure.
    """
    ap_root = os.path.abspath(ap_root)
    canonical = normalize_target(target)
    if canonical not in RTT_TARGETS:
        return None, "Unknown target: %s (supported: %s)" % (target, ', '.join(sorted(RTT_TARGETS.keys())))

    with _target_deploy_lock(ap_root, canonical):
        return _deploy_locked(ap_root, canonical)


def _deploy_locked(ap_root, canonical):
    tinfo = RTT_TARGETS[canonical]
    deploy_dir = os.path.join(ap_root, 'build', 'rtt_deploy', canonical)
    rtt_root = os.path.join(ap_root, 'modules', 'rt-thread')

    if not os.path.isdir(ap_root):
        return None, "AP_ROOT not a directory: %s" % ap_root
    if not os.path.isdir(rtt_root):
        return None, "RTT_ROOT not a directory: %s" % rtt_root

    # Incremental deploy: keep deploy_dir so in-flight scons (cwd=deploy) stays valid.
    try:
        os.makedirs(os.path.dirname(deploy_dir), exist_ok=True)
        os.makedirs(deploy_dir, exist_ok=True)
    except Exception as e:
        return None, "Deploy dir setup failed: %s" % e

    # Dispatch by mode
    if 'hwdef' in tinfo:
        err = _deploy_hwdef(ap_root, tinfo, deploy_dir)
    else:
        err = _deploy_legacy(ap_root, tinfo, deploy_dir)
    if err:
        return None, err

    _ensure_packages(deploy_dir, canonical)
    return deploy_dir, None


def _deploy_hwdef(ap_root, tinfo, deploy_dir):
    """New mode: copy common template + run hwdef parser to generate board files."""
    common_dir = os.path.join(ap_root, _norm(tinfo['common']))
    hwdef_path = os.path.join(ap_root, _norm(tinfo['hwdef']))

    if not os.path.isdir(common_dir):
        return "Common BSP template not found: %s" % common_dir
    if not os.path.isfile(hwdef_path):
        return "hwdef.dat not found: %s" % hwdef_path

    # 1. Copy/sync common template (dirs_exist_ok: refresh without deleting deploy root)
    try:
        shutil.copytree(common_dir, deploy_dir, dirs_exist_ok=True)
    except Exception as e:
        return "Copy common template failed: %s" % e

    # Copy any board-specific source overrides that are still needed while the
    # common hwdef path migrates away from legacy BSP files.
    err = _copy_hwdef_board_overrides(ap_root, tinfo, deploy_dir)
    if err:
        return err

    # 2. Generate base rtconfig.h from .config using RTT's mk_rtconfig
    #    (the .config in the template has the full RTT kernel config in Kconfig format)
    _generate_rtconfig(deploy_dir, ap_root)

    # 3. Run hwdef parser → generates hwdef.h, rt_pin_config.c, link.lds
    #    and APPENDS peripheral enables to the existing rtconfig.h
    gen_dir = os.path.join(deploy_dir, '_hwdef_gen')
    os.makedirs(gen_dir, exist_ok=True)

    # Copy the base rtconfig.h (from step 2) into gen_dir so the parser
    # can APPEND peripheral enables to it rather than creating a minimal one
    base_rtconfig = os.path.join(deploy_dir, 'rtconfig.h')
    if os.path.isfile(base_rtconfig):
        shutil.copy2(base_rtconfig, os.path.join(gen_dir, 'rtconfig.h'))

    parser = os.path.join(ap_root, _norm(HWDEF_PARSER))
    cmd = [sys.executable, parser, '-D', gen_dir]
    default_param = os.environ.get('RTT_DEFAULT_PARAM', '').strip()
    if default_param:
        cmd.extend(['--params', default_param])
    cmd.append(hwdef_path)
    extra_hwdef = os.environ.get('RTT_EXTRA_HWDEF', '').strip()
    if extra_hwdef:
        cmd.append(extra_hwdef)

    try:
        subprocess.check_call(cmd, cwd=ap_root, timeout=30, stdout=sys.stderr)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as e:
        return "hwdef parser failed: %s" % e

    # 4. Copy generated files into deploy directory
    _copy_generated(gen_dir, deploy_dir)

    # 5. Clean up temp gen dir
    shutil.rmtree(gen_dir, ignore_errors=True)

    return None


def _copy_hwdef_board_overrides(ap_root, tinfo, deploy_dir):
    """Copy optional board-specific source files required by hwdef targets.

    Copies:
      - MSP source (CubeMX MSP init) if msp_src_rel is set
      - board/ports/ overlay files if ports_rel is set.  Files from the
        source BSP's ports/ directory are copied on top of the common
        template's board/ports/ — this allows board-specific port files
        (e.g. sdcard_port.c, custom SConscript) to override or extend
        the common defaults.
    """
    msp_src_rel = tinfo.get('msp_src_rel')
    if msp_src_rel:
        msp_src = os.path.join(ap_root, _norm(msp_src_rel))
        if not os.path.isfile(msp_src):
            print("Warning: board MSP source not found, using hwdef-generated MSP: %s" % msp_src,
                  file=sys.stderr)
        else:
            msp_dst = os.path.join(
                deploy_dir, 'board', 'CubeMX_Config', 'Src', 'stm32f7xx_hal_msp.c')
            _safe_copy(msp_src, msp_dst)

    # Overlay board/ports/ files from source BSP onto the deployed board/ports/
    ports_rel = tinfo.get('ports_rel')
    if ports_rel:
        ports_src = os.path.join(ap_root, _norm(ports_rel))
        ports_dst = os.path.join(deploy_dir, 'board', 'ports')
        if os.path.isdir(ports_src):
            for stale in ('sdcard_port.c', 'sdcard_port.o', 'sdcard_port.d', 'sdcard_port.cmd'):
                try:
                    os.remove(os.path.join(ports_dst, stale))
                except FileNotFoundError:
                    pass
            for root, dirs, files in os.walk(ports_src):
                rel = os.path.relpath(root, ports_src)
                for f in files:
                    # Skip .o and other build artifacts
                    if f.endswith(('.o', '.d', '.cmd')):
                        continue
                    # The hwdef/common board owns SD mounting at "/" and
                    # creates /APM.  The legacy CUAV V5 ports overlay still
                    # contains an INIT_APP_EXPORT sdcard_port.c that mounts
                    # the same card at /sdcard and can race DFS readiness.
                    if rel == '.' and f in ('SConscript', 'sdcard_port.c'):
                        continue
                    src_file = os.path.join(root, f)
                    dst_file = os.path.join(ports_dst, rel, f) if rel != '.' else os.path.join(ports_dst, f)
                    _safe_copy(src_file, dst_file)

    return None


def _copy_generated(gen_dir, deploy_dir):
    """Copy generated files from parser output into the BSP deploy directory."""
    # hwdef.h → deploy root
    _safe_copy(os.path.join(gen_dir, 'hwdef.h'),
               os.path.join(deploy_dir, 'hwdef.h'))

    # rt_pin_config.c → board/CubeMX_Config/Src/stm32f7xx_hal_msp.c
    # Only use the generated MSP file when the deploy BSP does not already
    # provide a board-specific implementation. Some migrated boards still rely
    # on checked-in MSP code for SPI/UART/SD/TIM setup.
    msp_dir = os.path.join(deploy_dir, 'board', 'CubeMX_Config', 'Src')
    msp_dst = os.path.join(msp_dir, 'stm32f7xx_hal_msp.c')
    if not os.path.isfile(msp_dst):
        _safe_copy(os.path.join(gen_dir, 'rt_pin_config.c'), msp_dst)

    # rtconfig.h → deploy root (parser APPENDS peripheral enables to existing)
    _safe_copy(os.path.join(gen_dir, 'rtconfig.h'),
               os.path.join(deploy_dir, 'rtconfig.h'))

    # link.lds → patch MEMORY region in the template's linker script
    # The template has full SECTIONS layout; the generated MEMORY block carries
    # board-specific flash reserves and RAM_STACK/RAM_DMA/RAM_APP placement.
    _patch_linker_memory(gen_dir, deploy_dir)

    # ldscript.ld → deploy root (for waf compatibility)
    _safe_copy(os.path.join(gen_dir, 'ldscript.ld'),
               os.path.join(deploy_dir, 'ldscript.ld'))

    # romfs.pickle → deploy root
    _safe_copy(os.path.join(gen_dir, 'romfs.pickle'),
               os.path.join(deploy_dir, 'romfs.pickle'))

    # env.py → deploy root (SIM_ENABLED and ROMFS metadata mirror waf hwdef env)
    _safe_copy(os.path.join(gen_dir, 'env.py'),
               os.path.join(deploy_dir, 'env.py'))

    # processed_defaults.parm → deploy root, and make ROMFS metadata point at
    # the stable deploy path instead of the temporary _hwdef_gen directory.
    defaults_src = os.path.join(gen_dir, 'processed_defaults.parm')
    defaults_dst = os.path.join(deploy_dir, 'processed_defaults.parm')
    if os.path.isfile(defaults_src):
        _safe_copy(defaults_src, defaults_dst)
        _rewrite_romfs_defaults_path(deploy_dir, defaults_dst)
        _rewrite_env_defaults_path(deploy_dir, defaults_dst)

    # ap_romfs_embedded.h → deploy root (generated by rtt_hwdef.py from ROMFS directive)
    _safe_copy(os.path.join(gen_dir, 'ap_romfs_embedded.h'),
               os.path.join(deploy_dir, 'ap_romfs_embedded.h'))


def _safe_copy(src, dst):
    """Copy file if source exists."""
    if os.path.isfile(src):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)


def _rewrite_romfs_defaults_path(deploy_dir, defaults_dst):
    pickle_path = os.path.join(deploy_dir, 'romfs.pickle')
    if not os.path.isfile(pickle_path):
        return
    try:
        with open(pickle_path, 'rb') as pf:
            romfs = pickle.load(pf)
        rewritten = []
        changed = False
        for name, path in romfs:
            if name == 'defaults.parm':
                rewritten.append((name, defaults_dst))
                changed = True
            else:
                rewritten.append((name, path))
        if changed:
            with open(pickle_path, 'wb') as pf:
                pickle.dump(rewritten, pf)
    except Exception as e:
        print("Warning: failed to rewrite ROMFS defaults path: %s" % e,
              file=sys.stderr)


def _rewrite_env_defaults_path(deploy_dir, defaults_dst):
    env_path = os.path.join(deploy_dir, 'env.py')
    if not os.path.isfile(env_path):
        return
    try:
        with open(env_path, 'rb') as pf:
            env_data = pickle.load(pf)
        romfs = env_data.get('ROMFS_FILES')
        if not isinstance(romfs, list):
            return
        rewritten = []
        changed = False
        for name, path in romfs:
            if name == 'defaults.parm':
                rewritten.append((name, defaults_dst))
                changed = True
            else:
                rewritten.append((name, path))
        if changed:
            env_data['ROMFS_FILES'] = rewritten
            with open(env_path, 'wb') as pf:
                pickle.dump(env_data, pf)
    except Exception as e:
        print("Warning: failed to rewrite env.py defaults path: %s" % e,
              file=sys.stderr)


def _patch_linker_memory(gen_dir, deploy_dir):
    """Patch MEMORY ROM LENGTH in the deployed linker script.

    The hwdef parser generates link.lds with correct MEMORY values derived
    from FLASH_RESERVE_START_KB and FLASH_RESERVE_END_KB. We extract just
    the MEMORY block from the generated file and splice it into the
    template's full linker script (which has the complete SECTIONS layout).
    """
    import re
    gen_link = os.path.join(gen_dir, 'link.lds')
    dst_link = os.path.join(deploy_dir, 'board', 'linker_scripts', 'link.lds')

    if not os.path.isfile(gen_link) or not os.path.isfile(dst_link):
        return

    with open(gen_link) as f:
        gen_content = f.read()
    with open(dst_link) as f:
        dst_content = f.read()

    # Extract the MEMORY block from the generated linker
    m = re.search(r'MEMORY\s*\{([^}]+)\}', gen_content)
    if not m:
        return

    new_memory = 'MEMORY\n{\n' + m.group(1) + '}\n'

    # Replace MEMORY block in the destination
    dst_new = re.sub(r'MEMORY\s*\{[^}]+\}', new_memory, dst_content, count=1)
    if dst_new != dst_content:
        with open(dst_link, 'w') as f:
            f.write(dst_new)
        print("Patched linker MEMORY: %s" % dst_link, file=sys.stderr)


def _generate_rtconfig(deploy_dir, ap_root):
    """Generate rtconfig.h from .config using RT-Thread's mk_rtconfig tool.

    RT-Thread's .config (Kconfig format) needs to be converted to rtconfig.h (C header).
    If RTT's mk_rtconfig is not available, uses a simple fallback converter.
    """
    config_file = os.path.join(deploy_dir, '.config')
    rtconfig_path = os.path.join(deploy_dir, 'rtconfig.h')

    if not os.path.isfile(config_file):
        # No .config — skip, hwdef parser will create a minimal rtconfig.h
        return

    # Always use the simple converter — it's reliable and doesn't depend on
    # RTT's mk_rtconfig which needs the building.py module context.
    _simple_config_to_header(config_file, rtconfig_path)


def _simple_config_to_header(config_path, header_path):
    """Simple fallback: convert .config Kconfig format to C header rtconfig.h."""
    with open(config_path) as f:
        lines = f.readlines()

    with open(header_path, 'w') as out:
        out.write('/* rtconfig.h — generated from .config */\n')
        out.write('#ifndef RTCONFIG_H\n#define RTCONFIG_H\n\n')
        for line in lines:
            line = line.strip()
            if not line or line.startswith('#') or line.startswith('$'):
                continue
            # CONFIG_RT_FOO=y → #define RT_FOO
            # CONFIG_RT_BAR=123 → #define RT_BAR 123
            # CONFIG_RT_BAZ="string" → #define RT_BAZ "string"
            if line.startswith('CONFIG_'):
                line = line[7:]  # strip CONFIG_
                if '=' in line:
                    key, val = line.split('=', 1)
                    if val == 'y':
                        out.write('#define %s\n' % key)
                    else:
                        out.write('#define %s %s\n' % (key, val))
                elif line.startswith('CONFIG_'):
                    out.write('#define %s\n' % line[7:])
        out.write('\n#endif /* RTCONFIG_H */\n')


def _deploy_legacy(ap_root, tinfo, deploy_dir):
    """Legacy mode: copy full BSP directory."""
    src_dir = os.path.join(ap_root, _norm(tinfo['bsp_src_rel']))
    if not os.path.isdir(src_dir):
        return "BSP source not found: %s" % src_dir
    try:
        shutil.copytree(src_dir, deploy_dir)
    except Exception as e:
        return "Deploy copytree failed: %s" % e
    return None


def _ensure_packages(deploy_dir, target):
    """If required packages are missing, try to copy from existing RTT BSP, then download."""
    pkgs = REQUIRED_PACKAGES.get(target, [])
    if not pkgs:
        return
    # Check if packages have actual content (not just empty dirs or SConscript-only)
    need = False
    for p in pkgs:
        pkg_dir = os.path.join(deploy_dir, 'packages', p)
        if not os.path.isdir(pkg_dir):
            need = True
            break
        # Check for at least one .c or .h file
        has_content = False
        for root, dirs, files in os.walk(pkg_dir):
            for f in files:
                if f.endswith(('.c', '.h', '.s', '.S')):
                    has_content = True
                    break
            if has_content:
                break
        if not has_content:
            need = True
            break
    if not need:
        return
    # Try to copy from existing RTT BSP deploy in modules/rt-thread
    ap_root = os.path.dirname(os.path.dirname(os.path.dirname(deploy_dir)))
    rtt_bsp_cache = _find_rtt_bsp_cache(ap_root, target)
    if rtt_bsp_cache:
        print("Copying packages from cache: %s" % rtt_bsp_cache, file=sys.stderr)
        for p in pkgs:
            src = os.path.join(rtt_bsp_cache, 'packages', p)
            dst = os.path.join(deploy_dir, 'packages', p)
            if os.path.isdir(src) and not os.path.isdir(dst):
                try:
                    shutil.copytree(src, dst)
                    print("  Copied: %s" % p, file=sys.stderr)
                except Exception as e:
                    print("  Warning: copy %s failed: %s" % (p, e), file=sys.stderr)
        # Re-check
        all_ok = True
        for p in pkgs:
            if not os.path.isdir(os.path.join(deploy_dir, 'packages', p)):
                all_ok = False
                break
        if all_ok:
            return
    # Fall back to downloading
    script = os.path.join(deploy_dir, 'pkgs_update_manual.sh')
    if not os.path.isfile(script):
        _write_pkgs_script(script, pkgs)
    print("Downloading required packages for %s ..." % target, file=sys.stderr)
    try:
        subprocess.check_call(['bash', script], cwd=deploy_dir, timeout=300,
                              stdout=sys.stderr)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as e:
        print("Warning: package download failed: %s" % e, file=sys.stderr)


def _find_rtt_bsp_cache(ap_root, target):
    """Find existing RTT BSP with packages in modules/rt-thread/bsp/."""
    rtt_root = os.path.join(ap_root, 'modules', 'rt-thread')
    if not os.path.isdir(rtt_root):
        return None
    # Map target → possible BSP paths in modules/rt-thread/bsp/
    bsp_paths = {
        'cuav_v5': ['stm32/stm32f765-cuav-v5'],
        'pixhawk6c_mini': ['stm32/stm32h743-pixhawk6c-mini'],
    }
    for rel in bsp_paths.get(target, []):
        full = os.path.join(rtt_root, 'bsp', rel)
        if os.path.isdir(full) and os.path.isdir(os.path.join(full, 'packages')):
            return full
    common = os.path.join(ap_root, 'libraries', 'AP_HAL_RTT', 'hwdef', 'common')
    if os.path.isdir(os.path.join(common, 'packages')):
        return common
    return None


def _write_pkgs_script(script_path, pkgs):
    """Write a minimal pkgs_update_manual.sh for the common template."""
    with open(script_path, 'w') as f:
        f.write('#!/bin/bash\n')
        f.write('# Auto-generated package update script\n')
        f.write('set -e\n')
        f.write('if command -v envision &> /dev/null; then\n')
        f.write('    envision --update\n')
        f.write('elif command -v pkgs &> /dev/null; then\n')
        f.write('    pkgs --update\n')
        f.write('else\n')
        f.write('    echo "Warning: neither envision nor pkgs found, skipping package update"\n')
        f.write('fi\n')
    os.chmod(script_path, 0o755)


def main():
    parser = argparse.ArgumentParser(description='Deploy RTT BSP for ArduPilot scons build')
    parser.add_argument('ap_root', help='ArduPilot repository root')
    parser.add_argument('target', help='Target board name or alias')
    args = parser.parse_args()

    bsp_path, err = deploy(args.ap_root, args.target)
    if err:
        print(err, file=sys.stderr)
        return 1
    print(bsp_path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
