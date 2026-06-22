#!/usr/bin/env python3

import argparse
import importlib.util
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_param_request_read_benchmark.py"


def load_module():
    script_dir = str(SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    spec = importlib.util.spec_from_file_location("rtt_param_request_read_benchmark", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class FakeValue:
    def __init__(self, name, value=1.0, count=1):
        self.param_id = name
        self.param_value = value
        self.param_count = count


class FakeMav:
    def __init__(self):
        self.read_calls = 0

    def param_request_read_send(self, *args):
        self.read_calls += 1

    def param_request_list_send(self, *args):
        self.read_calls += 100


class FakeConnection:
    def __init__(self, responses=None):
        self.target_system = 1
        self.target_component = 1
        self.mav = FakeMav()
        self.responses = list(responses or [])
        self.closed = False

    def wait_heartbeat(self, timeout):
        return types.SimpleNamespace(system_status=3)

    def recv_match(self, type=None, blocking=False, timeout=0):
        if not self.responses:
            return None
        if not blocking:
            return None
        if self.responses[0] is None:
            self.responses.pop(0)
            return None
        if type is None:
            return self.responses.pop(0)
        for index, item in enumerate(self.responses):
            if item is not None:
                return self.responses.pop(index)
        return None

    def close(self):
        self.closed = True


class ParamRequestReadBenchmarkTest(unittest.TestCase):
    def args(self, outdir: Path, **overrides):
        values = {
            "port": "auto",
            "outdir": str(outdir),
            "param": "LOG_DISARMED",
            "rounds": 3,
            "reads_per_round": 4,
            "inter_round_sleep": 0.0,
            "round_drain_seconds": 0.0,
            "read_timeout": 8.0,
            "round_timeout_s": 120.0,
            "max_p95_read_s": 0.05,
            "max_max_read_s": 0.10,
            "max_p95_gap_s": 0.05,
            "max_max_gap_s": 0.15,
            "source_system": 244,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_read_param_prefers_direct_path(self):
        module = load_module()
        conn = FakeConnection([
            None,
            FakeValue("LOG_DISARMED", 1.0),
        ])
        value = module.read_param(conn, "LOG_DISARMED", timeout_s=0.1)
        self.assertEqual(value, 1.0)
        self.assertEqual(conn.mav.read_calls, 1)

    def test_read_param_falls_back_to_list(self):
        module = load_module()
        conn = FakeConnection([
            FakeValue("OTHER", 0.0, 2),
            FakeValue("LOG_DISARMED", 2.0, 2),
        ])
        with mock.patch.object(module, "request_param_direct", side_effect=RuntimeError("timeout")):
            value = module.read_param(conn, "LOG_DISARMED", timeout_s=0.1)
        self.assertEqual(value, 2.0)

    def test_run_benchmark_green(self):
        module = load_module()
        conn = FakeConnection([
            FakeValue("LOG_DISARMED", 1.0),
            FakeValue("LOG_DISARMED", 1.0),
            FakeValue("LOG_DISARMED", 1.0),
            FakeValue("LOG_DISARMED", 1.0),
        ] * 12)
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module, "connect_retry", return_value=conn), \
                    mock.patch.object(module.time, "sleep"):
                payload = module.run_benchmark(self.args(
                    Path(tmp),
                    max_p95_read_s=10.0,
                    max_max_read_s=10.0,
                    max_p95_gap_s=10.0,
                    max_max_gap_s=10.0,
                    round_drain_seconds=0.0,
                ))
        self.assertEqual(payload["verdict"], "GREEN", payload.get("reason"))
        self.assertEqual(payload["failed_rounds"], [])
        self.assertEqual(payload["metrics"]["read_latency_s"]["max"], payload["metrics"]["read_latency_s"]["max"])
        self.assertEqual(payload["rounds_completed"], 3)

    def test_classify_red_on_slow_reads(self):
        module = load_module()
        payload = {
            "failed_rounds": [],
            "metrics": {
                "read_latency_s": {"p95": 0.2, "max": 0.3},
                "max_gap_s_observed": {"p95": 0.01, "max": 0.02},
            },
        }
        verdict, reason = module.classify(payload, self.args(Path("/tmp"), max_p95_read_s=0.05))
        self.assertEqual(verdict, "RED")
        self.assertEqual(reason, "p95_read_slow")


if __name__ == "__main__":
    unittest.main()
