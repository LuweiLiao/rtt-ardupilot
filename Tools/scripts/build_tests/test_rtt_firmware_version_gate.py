#!/usr/bin/env python3

import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_firmware_version_gate.py"


def load_module():
    script_dir = str(SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    spec = importlib.util.spec_from_file_location("rtt_firmware_version_gate", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class FakeHeartbeat:
    def to_dict(self):
        return {"type": 2, "system_status": 3}


class FakeMav:
    def __init__(self):
        self.requests = 0

    def command_long_send(self, *args):
        self.requests += 1


class FakeConnection:
    def __init__(self, version_hash="1d9272fb"):
        self.target_system = 1
        self.target_component = 0
        self.mav = FakeMav()
        self.version_hash = version_hash
        self.closed = False

    def wait_heartbeat(self, timeout):
        return FakeHeartbeat()

    def recv_match(self, blocking, timeout):
        class Version:
            def __init__(self, value):
                self.value = value

            def get_type(self):
                return "AUTOPILOT_VERSION"

            def to_dict(self):
                values = [ord(ch) for ch in self.value]
                values.extend([0] * (8 - len(values)))
                return {"flight_custom_version": values[:8]}

        return Version(self.version_hash)

    def close(self):
        self.closed = True


class FirmwareVersionGateTest(unittest.TestCase):
    def args(self, **overrides):
        values = {
            "port": "auto",
            "outdir": "/tmp",
            "source_root": "/repo",
            "expected_hash": "1d9272fb",
            "source_system": 247,
            "heartbeat_timeout": 15.0,
            "version_timeout": 10.0,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_decode_custom_version_ignores_zero_padding(self):
        module = load_module()
        self.assertEqual(module.decode_custom_version([49, 100, 57, 50, 55, 50, 102, 98]), "1d9272fb")
        self.assertEqual(module.decode_custom_version([49, 50, 0, 0, 0, 0, 0, 0]), "12")

    def test_git_short_hash_uses_repo_head(self):
        module = load_module()
        proc = subprocess.CompletedProcess(["git"], 0, stdout="5fb85ddb46\n", stderr="")
        with mock.patch.object(module.subprocess, "run", return_value=proc) as run:
            self.assertEqual(module.git_short_hash(Path("/repo")), "5fb85ddb")
        run.assert_called_once()

    def test_run_gate_green_on_matching_hash(self):
        module = load_module()
        with mock.patch.object(module, "resolve_mavlink_port", return_value="/dev/ttyACM1"), \
                mock.patch.object(module.mavutil, "mavlink_connection", return_value=FakeConnection("1d9272fb")):
            payload = module.run_gate(self.args())
        self.assertEqual(payload["verdict"], "GREEN", payload.get("reason"))
        self.assertEqual(payload["actual_hash"], "1d9272fb")

    def test_run_gate_red_on_mismatch(self):
        module = load_module()
        with mock.patch.object(module, "resolve_mavlink_port", return_value="/dev/ttyACM1"), \
                mock.patch.object(module.mavutil, "mavlink_connection", return_value=FakeConnection("f2bfac77")):
            payload = module.run_gate(self.args(expected_hash="1d9272fb"))
        self.assertEqual(payload["verdict"], "RED")
        self.assertEqual(payload["reason"], "firmware_hash_mismatch")
        self.assertEqual(payload["actual_hash"], "f2bfac77")


if __name__ == "__main__":
    unittest.main()
