#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_openocd_guard.py"


def load_module():
    spec = importlib.util.spec_from_file_location("rtt_openocd_guard", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class OpenOCDGuardTest(unittest.TestCase):
    def test_cleanup_runs_exact_and_pattern_guards(self):
        module = load_module()
        calls = []

        def fake_run(argv, check, text, stdout, stderr, timeout):
            calls.append(argv)
            return subprocess.CompletedProcess(argv, 1, "", "")

        with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
            results = module.cleanup_openocd()

        self.assertEqual([item["name"] for item in results], [
            "exact_openocd",
            "exact_openoccd_typo",
            "filtered_pattern_openocd",
            "filtered_pattern_openoccd_typo",
        ])
        self.assertIn(["pkill", "-9", "-x", "openocd"], calls)
        self.assertIn(["pkill", "-9", "-x", "openoccd"], calls)
        self.assertIn(["pgrep", "-af", "[o]penocd"], calls)
        self.assertIn(["pgrep", "-af", "[o]penoccd"], calls)

    def test_cleanup_records_missing_pkill(self):
        module = load_module()

        def fake_run(argv, check, text, stdout, stderr, timeout):
            raise FileNotFoundError(argv[0])

        with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
            results = module.cleanup_openocd()

        self.assertTrue(all(item["error"] in ("not_found:pkill", "not_found:pgrep") for item in results))

    def test_filtered_pattern_does_not_kill_guard_script(self):
        module = load_module()
        killed = []

        def fake_run(argv, check, text, stdout, stderr, timeout):
            self.assertEqual(argv, ["pgrep", "-af", "[o]penocd"])
            return subprocess.CompletedProcess(
                argv,
                0,
                "111 openocd -f interface/stlink.cfg\n"
                "222 python3 Tools/scripts/rtt_openocd_guard.py\n"
                "333 timeout 20 openocd -f target/stm32f7x.cfg\n",
                "",
            )

        def fake_kill(pid, sig):
            killed.append(pid)

        with mock.patch.object(module.subprocess, "run", side_effect=fake_run), \
                mock.patch.object(module.os, "getpid", return_value=999), \
                mock.patch.object(module.os, "getppid", return_value=998), \
                mock.patch.object(module.os, "kill", side_effect=fake_kill):
            result = module._filtered_pattern_kill(["pgrep", "-af", "[o]penocd"], 2.0)

        self.assertEqual(killed, [111, 333])
        self.assertIn("rtt_openocd_guard.py", result.stderr)


if __name__ == "__main__":
    unittest.main()
