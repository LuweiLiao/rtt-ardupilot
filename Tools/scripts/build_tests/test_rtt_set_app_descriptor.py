#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[3] / "libraries" / "AP_HAL_RTT" / "scripts" / "set_app_descriptor.py"


def load_module():
    spec = importlib.util.spec_from_file_location("set_app_descriptor", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GitHashFallbackTest(unittest.TestCase):
    def test_git_hash_uses_private_home_and_marks_repo_safe(self):
        module = load_module()
        calls = []

        def fake_run(cmd, capture_output, text, timeout, env):
            calls.append((cmd, env))
            if cmd[:4] == ["git", "config", "--global", "--add"]:
                self.assertEqual(cmd[-2:], ["safe.directory", "/tmp/repo"])
                self.assertIn("HOME", env)
                return subprocess.CompletedProcess(cmd, 0, "", "")
            self.assertEqual(cmd[:3], ["git", "-C", "/tmp/repo"])
            self.assertIn("HOME", env)
            return subprocess.CompletedProcess(cmd, 0, "1234abc\n", "")

        with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
            git_hash, error = module.git_short_hash("/tmp/repo")

        self.assertEqual(git_hash, "1234abc")
        self.assertIsNone(error)
        self.assertEqual(len(calls), 2)


if __name__ == "__main__":
    unittest.main()
