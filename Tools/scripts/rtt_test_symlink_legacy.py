#!/usr/bin/env python3
"""Replace hwdef/common/tests content with symlinks to libraries/AP_HAL_RTT/test/."""
from __future__ import print_function

import os
import shutil
import sys

AP_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
LEGACY_ROOT = os.path.join(AP_ROOT, 'libraries', 'AP_HAL_RTT', 'hwdef', 'common', 'tests')

sys.path.insert(0, os.path.dirname(__file__))
import rtt_test_manifest as manifest

LINKS = {
    'common': ('_common',),
}
for name, segs in manifest.TEST_LAYOUT.items():
    legacy = manifest.LEGACY_DIR_NAMES.get(name)
    if legacy:
        LINKS[legacy] = segs


def main():
    if not os.path.isdir(LEGACY_ROOT):
        print('missing', LEGACY_ROOT)
        return 1

    # Remove everything except README.md
    for entry in os.listdir(LEGACY_ROOT):
        if entry == 'README.md':
            continue
        path = os.path.join(LEGACY_ROOT, entry)
        if os.path.islink(path):
            os.unlink(path)
        elif os.path.isdir(path):
            shutil.rmtree(path)
        else:
            os.remove(path)

    for legacy_name, segs in sorted(LINKS.items()):
        # hwdef/common/tests -> ../../../ = AP_HAL_RTT root
        target = os.path.join('..', '..', '..', 'test', *segs)
        link = os.path.join(LEGACY_ROOT, legacy_name)
        os.symlink(target, link)
        print('symlink', legacy_name, '->', target)

    print('OK: legacy test symlinks under', LEGACY_ROOT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
