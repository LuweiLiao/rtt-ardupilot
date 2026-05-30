#!/usr/bin/env python3
# encoding: utf-8
"""
Archive many object files into a static library without ARG_MAX limits.

Uses GNU ar's @response-file syntax (supported by arm-none-eabi-ar).  A single
ar invocation with all objects listed as explicit SCons prerequisites avoids the
parallel partial-archive races seen when appending to the same .a from multiple
commands.
"""

from __future__ import print_function

import os
import subprocess
import sys

import SCons.Builder


def _ar_flag_list(env):
    arflags = env.get('ARFLAGS', 'rc')
    if isinstance(arflags, (list, tuple)):
        return [str(x) for x in arflags if str(x)]
    return str(arflags).split()


def _node_abspath(node):
    if hasattr(node, 'get_abspath'):
        return node.get_abspath()
    return str(node)


def archive_action(target, source, env):
    """SCons action: $AR $ARFLAGS $TARGET @$RSP with one object path per line."""
    tgt = _node_abspath(target[0])
    ar = env.subst('$AR')
    flags = _ar_flag_list(env)
    rsp_path = tgt + '.ar.rsp'

    os.makedirs(os.path.dirname(tgt) or '.', exist_ok=True)
    with open(rsp_path, 'w', encoding='utf-8') as rsp:
        for node in source:
            rsp.write(_node_abspath(node) + '\n')

    cmd = [ar] + flags + [tgt, '@' + rsp_path]
    if env.GetOption('verbose'):
        print(' '.join(cmd))
    ret = subprocess.call(cmd)
    if ret != 0:
        return ret

    # Optional ranlib when 's' is not already in ARFLAGS.
    if 's' not in flags:
        ranlib = env.subst('${RANLIB}')
        if ranlib and ranlib != '${RANLIB}':
            ranlib_cmd = [ranlib, tgt]
            if env.GetOption('verbose'):
                print(' '.join(ranlib_cmd))
            ret = subprocess.call(ranlib_cmd)
    return ret


def register(env):
    """Register env.RttArArchive builder (idempotent)."""
    if 'RttArArchive' in env.get('BUILDERS', {}):
        return
    env.Append(BUILDERS={
        'RttArArchive': SCons.Builder.Builder(
            action=archive_action,
            suffix='.a',
            src_suffix='.o',
        ),
    })


def configure_tempfile_ar(env, max_line_length=8192, tempfile_dir=None):
    """
    Enable SCons TempFileMunge for AR/LINK on this Environment (other static libs).
    """
    from SCons.Platform import TempFileMunge

    env['TEMPFILE'] = TempFileMunge
    env['MAXLINELENGTH'] = max_line_length
    if tempfile_dir:
        env['TEMPFILEDIR'] = tempfile_dir

    # RT-Thread building.py may set ARCOM to '$AR --create ...'; override for @rsp.
    env['ARCOM'] = '${TEMPFILE("$AR $ARFLAGS $TARGET $SOURCES", "$ARCOMSTR")}'

    if 'TEMPFILE' not in str(env.get('LINKCOM', '')):
        env['LINKCOM'] = (
            '${TEMPFILE("$LINK $LINKFLAGS -o $TARGET $SOURCES '
            '$_LIBDIRFLAGS $_LIBFLAGS", "$LINKCOMSTR")}'
        )
