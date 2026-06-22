#!/usr/bin/env python3

import argparse
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_param_persist_gate.py"


def load_module():
    script_dir = str(SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    spec = importlib.util.spec_from_file_location("rtt_param_persist_gate", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class FakeParamValue:
    def __init__(self, name, value, count=1):
        self.param_id = name
        self.param_value = value
        self.param_count = count


class FakeMav:
    def __init__(self):
        self.set_calls = []
        self.read_calls = []
        self.list_requests = 0

    def param_set_send(self, *args):
        self.set_calls.append(args)

    def param_request_read_send(self, *args):
        self.read_calls.append(args)

    def param_request_list_send(self, *args):
        self.list_requests += 1


class FakeConnection:
    def __init__(self, messages=None):
        self.target_system = 1
        self.target_component = 1
        self.mav = FakeMav()
        self.messages = list(messages or [])
        self.closed = False

    def wait_heartbeat(self, timeout):
        return types.SimpleNamespace(to_dict=lambda: {"system_status": 3})

    def recv_match(self, type=None, blocking=False, timeout=0):
        if not self.messages:
            return None
        return self.messages.pop(0)

    def close(self):
        self.closed = True


class ParamPersistGateTest(unittest.TestCase):
    def args(self, **overrides):
        values = {
            "port": "auto",
            "outdir": "/tmp/rtt-param-persist-test",
            "param": "LOG_DISARMED",
            "save_wait": 0.01,
            "tolerance": 0.01,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_module_import_does_not_require_pymavlink(self):
        module = load_module()
        self.assertIsNone(module._MAVUTIL)
        self.assertTrue(callable(module.load_mavutil))

    def test_alternate_value_is_small_and_reversible(self):
        module = load_module()
        self.assertEqual(module.alternate_value(0.0), 1.0)
        self.assertEqual(module.alternate_value(1.0), 2.0)
        self.assertEqual(module.alternate_value(12.0), 1.0)

    def test_param_name_accepts_bytes_and_strings(self):
        module = load_module()
        self.assertEqual(module.param_name(FakeParamValue(b"LOG_DISARMED\x00\x00", 1)), "LOG_DISARMED")
        self.assertEqual(module.param_name(FakeParamValue("SR0_RAW_SENS\x00", 1)), "SR0_RAW_SENS")

    def test_set_param_uses_real32_and_returns_ack_value(self):
        module = load_module()
        conn = FakeConnection(messages=[FakeParamValue("LOG_DISARMED", 2.0)])
        fake_mavutil = types.SimpleNamespace(
            mavlink=types.SimpleNamespace(MAV_PARAM_TYPE_REAL32=9),
        )
        with mock.patch.object(module, "load_mavutil", return_value=fake_mavutil), \
                mock.patch.object(module, "drain"):
            value = module.set_param(conn, "LOG_DISARMED", 2.0)
        self.assertEqual(value, 2.0)
        self.assertEqual(conn.mav.set_calls[0][0:4], (1, 1, b"LOG_DISARMED", 2.0))
        self.assertEqual(conn.mav.set_calls[0][4], 9)

    def test_run_gate_green_writes_resets_verifies_and_restores(self):
        module = load_module()
        first_conn = FakeConnection()
        reset_conn = FakeConnection()
        restore_conn = FakeConnection()
        with mock.patch.object(module, "resolve_port", return_value="/dev/ttyACM1"), \
                mock.patch.object(module, "connect", return_value=first_conn) as connect, \
                mock.patch.object(module, "choose_param", return_value=("LOG_DISARMED", 0.0)), \
                mock.patch.object(module, "set_param", side_effect=[1.0, 0.0]) as set_param, \
                mock.patch.object(module, "openocd_reset", side_effect=[0, 0]) as openocd_reset, \
                mock.patch.object(module, "wait_cdc") as wait_cdc, \
                mock.patch.object(module, "connect_retry", side_effect=[reset_conn, restore_conn]) as connect_retry, \
                mock.patch.object(module, "request_param", side_effect=[1.0, 0.0]) as request_param, \
                mock.patch.object(module.time, "sleep"):
            payload = module.run_gate(self.args())

        self.assertEqual(payload["verdict"], "GREEN", payload.get("reason"))
        self.assertEqual(payload["reason"], "param_persist_restore_ok")
        self.assertEqual(payload["after_reset"], 1.0)
        self.assertEqual(payload["restored_after_reset"], 0.0)
        connect.assert_called_once_with("/dev/ttyACM1")
        self.assertEqual(connect_retry.call_count, 2)
        self.assertEqual(openocd_reset.call_count, 2)
        self.assertEqual(wait_cdc.call_count, 2)
        self.assertEqual(request_param.call_args_list[0].args, (reset_conn, "LOG_DISARMED"))
        self.assertEqual(request_param.call_args_list[1].args, (restore_conn, "LOG_DISARMED"))
        self.assertEqual(set_param.call_args_list[0].args, (first_conn, "LOG_DISARMED", 1.0))
        self.assertEqual(set_param.call_args_list[1].args, (reset_conn, "LOG_DISARMED", 0.0))
        self.assertTrue(first_conn.closed)
        self.assertTrue(reset_conn.closed)
        self.assertTrue(restore_conn.closed)

    def test_run_gate_failure_records_payload_and_best_effort_restore(self):
        module = load_module()
        first_conn = FakeConnection()
        reset_conn = FakeConnection()
        restore_payload = {"param": "LOG_DISARMED", "original": 0.0, "set_restore": 0.0}
        with mock.patch.object(module, "resolve_port", return_value="/dev/ttyACM1"), \
                mock.patch.object(module, "connect", return_value=first_conn), \
                mock.patch.object(module, "choose_param", return_value=("LOG_DISARMED", 0.0)), \
                mock.patch.object(module, "set_param", return_value=1.0), \
                mock.patch.object(module, "openocd_reset", return_value=0), \
                mock.patch.object(module, "wait_cdc"), \
                mock.patch.object(module, "connect_retry", return_value=reset_conn), \
                mock.patch.object(module, "request_param", return_value=2.0), \
                mock.patch.object(module, "try_restore", return_value=restore_payload) as try_restore, \
                mock.patch.object(module.time, "sleep"):
            with self.assertRaises(module.GateError) as raised:
                module.run_gate(self.args())

        payload = raised.exception.payload
        self.assertEqual(payload["verdict"], "RED")
        self.assertIn("persist_mismatch", payload["reason"])
        self.assertEqual(payload["best_effort_restore"], restore_payload)
        try_restore.assert_called_once_with("/dev/ttyACM1", "LOG_DISARMED", 0.0, 0.01)
        self.assertTrue(first_conn.closed)
        self.assertTrue(reset_conn.closed)


if __name__ == "__main__":
    unittest.main()
