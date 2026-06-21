# -*- coding: utf-8 -*-
# Root SConstruct for pogo-apm: RTT BSP staging + scons full build by --target.
# Usage: scons --v=ArduCopter --target=pixhawk6c-mini | scons --v=ArduCopter --target=cuav-v5 [scons options]
#        aliases like pixhawk6c_mini, cuav_v5, and quoted forms like "cuav v5" are accepted.
#        scons -c --target=cuav-v5  => clean staged BSP build.
# Product: build/rtt_deploy/<target>/rtthread.bin, rt-thread.elf; copied to build/rtt_<target>/rtthread.bin

import os
import subprocess
import sys
import shutil

from SCons.Script import AddOption, GetOption, ARGUMENTS

# --- Options (Agent 2 style) ---
AddOption('--target',
          dest='target',
          type='string',
          default='',
          help='RTT BSP target alias: pixhawk6c-mini/pixhawk6c_mini, cuav-v5/cuav_v5')
AddOption('--v',
          dest='vehicle',
          type='string',
          default='ArduCopter',
          help='Vehicle (e.g. ArduCopter), for future use')
AddOption('--test',
          dest='test_name',
          type='string',
          default='',
          help='Build a standalone module test instead of ArduPilot (e.g. l0_boot, l2_spi)')
AddOption('--extra-hwdef',
          dest='extra_hwdef',
          type='string',
          default='',
          help='Extra hwdef.dat fragment to append after the board hwdef')
AddOption('--default-param',
          dest='default_param',
          type='string',
          default='',
          help='Default parameter file to embed via hwdef ROMFS/defaults support')
AddOption('--upload',
          dest='upload',
          action='store_true',
          default=False,
          help='After build, upload firmware (optional)')
AddOption('--port',
          dest='upload_port',
          type='string',
          default=None,
          help='Serial port for upload (e.g. /dev/ttyACM0)')

RTT_TARGETS = {
    'pixhawk6c_mini': {'board': 'rtt_pixhawk6c_mini'},
    'cuav_v5': {'board': 'rtt_cuav_v5'},
}

RTT_TARGET_ALIASES = {
    'pixhawk6c_mini': 'pixhawk6c_mini',
    'pixhawk6c-mini': 'pixhawk6c_mini',
    'pixhawk6c mini': 'pixhawk6c_mini',
    'rtt_pixhawk6c_mini': 'pixhawk6c_mini',
    'rtt-pixhawk6c-mini': 'pixhawk6c_mini',
    'cuav_v5': 'cuav_v5',
    'cuav-v5': 'cuav_v5',
    'cuav v5': 'cuav_v5',
    'rtt_cuav_v5': 'cuav_v5',
    'rtt-cuav-v5': 'cuav_v5',
}


def _normalize_target(target):
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


def _rtt_detect_exec_path():
    """If arm-none-eabi-gcc is on PATH, return its directory for RTT_EXEC_PATH."""
    try:
        import shutil
        gcc = shutil.which('arm-none-eabi-gcc')
        if gcc:
            return os.path.dirname(gcc)
    except Exception:
        pass
    return None


def _abspath_from_root(ap_root, path):
    if not path:
        return ''
    if os.path.isabs(path):
        return path
    return os.path.abspath(os.path.join(ap_root, path))


def _apply_hwdef_option_env(ap_root, env):
    extra_hwdef = GetOption('extra_hwdef') or ''
    if extra_hwdef:
        env['RTT_EXTRA_HWDEF'] = _abspath_from_root(ap_root, extra_hwdef)
    default_param = GetOption('default_param') or ''
    if default_param:
        env['RTT_DEFAULT_PARAM'] = _abspath_from_root(ap_root, default_param)
    loop_diag = os.environ.get('HAL_RTT_LOOP_DIAG')
    if loop_diag:
        env['HAL_RTT_LOOP_DIAG'] = loop_diag


def _git_short_hash(ap_root):
    try:
        return subprocess.check_output(
            ['git', 'rev-parse', '--short=8', 'HEAD'],
            cwd=ap_root,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return 'unknown'


def _write_rtt_version_header(ap_root, board):
    """Generate the ap_version.h normally produced by waf for ArduPilot builds."""
    out_dir = os.path.join(ap_root, 'build', board)
    os.makedirs(out_dir, exist_ok=True)

    git_hash = os.environ.get('GIT_VERSION') or _git_short_hash(ap_root)
    try:
        git_int = int(os.environ.get('GIT_VERSION_INT') or git_hash, 16)
    except ValueError:
        git_int = 0

    lines = [
        '// auto-generated header, do not edit',
        '',
        '#pragma once',
        '',
        '#ifndef FORCE_VERSION_H_INCLUDE',
        '#error ap_version.h should never be included directly. You probably want to include AP_Common/AP_FWVersion.h',
        '#endif',
        '',
        '#define GIT_VERSION "%s"' % git_hash,
        '#define GIT_VERSION_INT %u' % git_int,
        '#define AP_BUILD_ROOT "%s"' % ap_root,
    ]

    path = os.path.join(out_dir, 'ap_version.h')
    data = '\n'.join(lines) + '\n'
    old = None
    if os.path.isfile(path):
        with open(path, 'r') as f:
            old = f.read()
    if old != data:
        with open(path, 'w') as f:
            f.write(data)
        print('Generated %s with GIT_VERSION %s' % (path, git_hash))


def _run_rtt_build(ap_root, target, bsp_deploy_abspath, scons_args, test_name=''):
    """Run scons (or scons -c) in bsp_deploy_abspath with ARDUPILOT_FULL=1, RTT_ROOT, RTT_EXEC_PATH."""
    rtt_root = os.environ.get('RTT_ROOT') or os.path.join(ap_root, 'modules', 'rt-thread')
    env = os.environ.copy()
    env['AP_ROOT'] = ap_root
    if test_name:
        env['TEST_NAME'] = test_name
    else:
        env['ARDUPILOT_FULL'] = '1'
    _apply_hwdef_option_env(ap_root, env)
    env['RTT_ROOT'] = rtt_root
    rtt_exec = _rtt_detect_exec_path()
    if rtt_exec:
        env['RTT_EXEC_PATH'] = rtt_exec
    cmd = [sys.executable, '-m', 'SCons'] + scons_args
    ret = subprocess.call(cmd, cwd=bsp_deploy_abspath, env=env)
    return ret


def _copy_bin_to_build(ap_root, target, bsp_deploy_abspath):
    """Copy rtthread.bin to build/rtt_<target>/ for upload convenience."""
    src = os.path.join(bsp_deploy_abspath, 'rtthread.bin')
    if not os.path.isfile(src):
        return
    out_dir = os.path.join(ap_root, 'build', 'rtt_%s' % target)
    try:
        os.makedirs(out_dir, exist_ok=True)
        dst = os.path.join(out_dir, 'rtthread.bin')
        import shutil
        shutil.copy2(src, dst)
        print('Copied %s -> %s' % (src, dst))
    except Exception as e:
        print('Copy rtthread.bin to build: %s' % e, file=sys.stderr)


def _verify_bin_integrity(ap_root, bsp_deploy_abspath):
    """Verify the built rtthread.bin has a valid Reset_Handler literal pool.
    Catches ELF vs .bin mismatch caused by objcopy/Load segment alignment issues.
    """
    bin_path = os.path.join(bsp_deploy_abspath, 'rtthread.bin')
    elf_path = os.path.join(bsp_deploy_abspath, 'rt-thread.elf')
    if not os.path.isfile(bin_path) or not os.path.isfile(elf_path):
        print('verify: bin/elf not yet available, skipping', file=sys.stderr)
        return
    script = os.path.join(ap_root, 'Tools', 'scripts', 'rtt_verify_bin.py')
    if not os.path.isfile(script):
        print('rtt_verify_bin.py not found at %s, skipping binary verification' % script, file=sys.stderr)
        return
    ret = subprocess.call([sys.executable, script, bin_path, elf_path], cwd=ap_root)
    if ret != 0:
        print('ERROR: Binary integrity check FAILED — see above for details.', file=sys.stderr)
        Exit(ret)


# --- When valid --target: deploy then scons in BSP (or scons -c for clean) ---
target = GetOption('target')
canonical_target = _normalize_target(target)
if target and not canonical_target:
    print('Unknown RTT target: %s (supported: pixhawk6c_mini, pixhawk6c-mini, cuav_v5, cuav-v5)' % target, file=sys.stderr)
    Exit(1)

if canonical_target:
    ap_root = Dir('#').abspath
    deploy_script = os.path.join(ap_root, 'Tools', 'scripts', 'rtt_bsp_deploy.py')
    if not os.path.isfile(deploy_script):
        print('rtt_bsp_deploy.py not found: %s' % deploy_script, file=sys.stderr)
        Exit(1)
    # 2) Build scons args: pass through command line but drop --target, --upload, --port (skip argv[0])
    test_name = GetOption('test_name') or ''
    scons_args = []
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a in ('--target', '--upload', '--port', '--v', '--test', '--extra-hwdef', '--default-param'):
            i += 1
            if i < len(sys.argv) and not sys.argv[i].startswith('-'):
                i += 1
            continue
        if a.startswith('--target=') or a.startswith('--upload=') or a.startswith('--port=') or a.startswith('--v=') or a.startswith('--test=') or a.startswith('--extra-hwdef=') or a.startswith('--default-param='):
            i += 1
            continue
        scons_args.append(a)
        i += 1
    bsp_deploy_abspath = os.path.join(ap_root, 'build', 'rtt_deploy', canonical_target)
    is_clean = '-c' in scons_args or '--clean' in scons_args
    # 1) Deploy BSP only for build; clean should not recreate staging area.
    if not is_clean:
        deploy_env = os.environ.copy()
        _apply_hwdef_option_env(ap_root, deploy_env)
        try:
            bsp_deploy_abspath = subprocess.check_output(
                [sys.executable, deploy_script, ap_root, canonical_target],
                cwd=ap_root,
                env=deploy_env,
                text=True
            ).strip()
        except subprocess.CalledProcessError as e:
            Exit(e.returncode)
        if not os.path.isdir(bsp_deploy_abspath):
            print('Deploy path not a directory: %s' % bsp_deploy_abspath, file=sys.stderr)
            Exit(1)
    # 2.5) Generate mavlink headers for scons (waf does this for waf build)
    if not is_clean:
        board = RTT_TARGETS[canonical_target]['board']
        _write_rtt_version_header(ap_root, board)
        dronecangen_script = os.path.join(ap_root, 'Tools', 'scripts', 'rtt_dronecangen.py')
        if os.path.isfile(dronecangen_script):
            ret_dronecan = subprocess.call([sys.executable, dronecangen_script, ap_root, board], cwd=ap_root)
            if ret_dronecan != 0:
                print('rtt_dronecangen.py failed', file=sys.stderr)
                Exit(ret_dronecan)
        mavgen_script = os.path.join(ap_root, 'Tools', 'scripts', 'rtt_mavgen.py')
        if os.path.isfile(mavgen_script):
            ret_mav = subprocess.call([sys.executable, mavgen_script, ap_root, board], cwd=ap_root)
            if ret_mav != 0:
                print('rtt_mavgen.py failed', file=sys.stderr)
                Exit(ret_mav)
    # 3) Run scons in BSP (build or clean)
    if is_clean and not os.path.isdir(bsp_deploy_abspath):
        shutil.rmtree(os.path.join(ap_root, 'build', 'rtt_%s' % canonical_target), ignore_errors=True)
        Exit(0)
    ret = _run_rtt_build(ap_root, canonical_target, bsp_deploy_abspath, scons_args, test_name=test_name)
    if ret != 0:
        Exit(ret)
    # 3.5) Post-build: verify rtthread.bin integrity (Reset_Handler literal pool)
    if not is_clean and not test_name:
        _verify_bin_integrity(ap_root, bsp_deploy_abspath)
    # 4) If build (not clean), optionally copy rtthread.bin to build/rtt_<target>/
    if not is_clean:
        _copy_bin_to_build(ap_root, canonical_target, bsp_deploy_abspath)
        # 5) Always refresh APJ packaging after a full app build.  --upload only
        #    controls whether uploader.py is run afterwards.
        if not test_name:
            rtthread_bin = os.path.join(bsp_deploy_abspath, 'rtthread.bin')
            if not os.path.isfile(rtthread_bin):
                print('error: rtthread.bin not found at %s' % rtthread_bin, file=sys.stderr)
                Exit(1)
            board_apj = RTT_TARGETS[canonical_target]['board']
            rtt_bin_to_apj = os.path.join(ap_root, 'Tools', 'scripts', 'rtt_bin_to_apj.py')
            apj_path = os.path.join(bsp_deploy_abspath, 'arducopter.apj')
            cmd = [sys.executable, rtt_bin_to_apj, rtthread_bin, '--board', board_apj, '-o', apj_path]
            if GetOption('upload'):
                cmd += ['--upload']
                if GetOption('upload_port'):
                    cmd += ['--port', GetOption('upload_port')]
            ret = subprocess.call(cmd)
            if ret != 0:
                Exit(ret)
    Exit(0)

# No --target or unknown target: no-op (root does not define default targets)
# Allow other uses of this SConstruct without --target by not calling Exit(0) when target is empty.
