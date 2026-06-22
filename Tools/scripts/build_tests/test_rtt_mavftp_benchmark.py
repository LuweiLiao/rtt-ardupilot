#!/usr/bin/env python3

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_mavftp_benchmark.py"


def load_module():
    script_dir = str(SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    spec = importlib.util.spec_from_file_location("rtt_mavftp_benchmark", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload) + "\n", encoding="utf-8")


class MavftpBenchmarkTest(unittest.TestCase):
    def args(self, outdir: Path, **overrides):
        values = {
            "port": "auto",
            "outdir": str(outdir),
            "rounds": 3,
            "inter_round_sleep": 0.0,
            "round_timeout_s": 220.0,
            "heartbeat_timeout": 25.0,
            "require_sd": True,
            "min_param_bytes": 128,
            "min_param_count": 100,
            "test_file_bytes": 512,
            "remote_test_prefix": "/APM/rtt_mavftp_bench",
            "post_ftp_stability": 4.0,
            "min_post_ftp_heartbeats": 2,
            "max_p95_total": 45.0,
            "max_max_total": 60.0,
            "source_system": 241,
            "accept_status": [3, 4, 5],
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_metric_summary_uses_expected_percentiles(self):
        module = load_module()
        rounds = [{"wall_elapsed_s": v} for v in [10.0, 11.0, 12.0, 20.0, 30.0]]
        summary = module.metric_summary(rounds, "wall_elapsed_s")
        self.assertEqual(summary["min"], 10.0)
        self.assertEqual(summary["p50"], 12.0)
        self.assertEqual(summary["p95"], 30.0)
        self.assertEqual(summary["max"], 30.0)

    def test_run_benchmark_aggregates_green_rounds(self):
        module = load_module()

        def fake_run(argv, stdout, stderr, check):
            round_dir = Path(stdout.name).parent
            round_index = int(round_dir.name.split("_")[-1])
            write_json(round_dir / "mavftp_gate.json", {
                "verdict": "GREEN",
                "reason": "mavftp_ok",
                "steps": [
                    {"name": "list_/", "elapsed_s": 0.2},
                    {"name": "read_@PARAM/param.pck", "elapsed_s": 1.0 + round_index},
                    {"name": "put_/APM/test.tmp", "elapsed_s": 0.4},
                ],
                "read_@PARAM/param.pck": {"bytes": 4096},
                "param_pck_decode": {"decoded_count": 947},
                "post_ftp_stability": {"heartbeats": 4, "stable": True},
                "target_system": 1,
                "target_component": 1,
            })
            return subprocess.CompletedProcess(argv, 0)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
                payload = module.run_benchmark(self.args(Path(tmp)))

        self.assertEqual(payload["verdict"], "GREEN", payload.get("reason"))
        self.assertEqual(payload["rounds_completed"], 3)
        self.assertEqual(payload["failed_rounds"], [])
        self.assertEqual(payload["metrics"]["param_decoded_count"]["min"], 947.0)
        self.assertEqual(payload["metrics"]["post_ftp_heartbeats"]["p50"], 4.0)

    def test_run_benchmark_fails_on_failed_round(self):
        module = load_module()

        def fake_run(argv, stdout, stderr, check):
            round_dir = Path(stdout.name).parent
            round_index = int(round_dir.name.split("_")[-1])
            if round_index != 2:
                write_json(round_dir / "mavftp_gate.json", {
                    "verdict": "GREEN",
                    "reason": "mavftp_ok",
                    "read_@PARAM/param.pck": {"bytes": 4096},
                    "param_pck_decode": {"decoded_count": 947},
                    "post_ftp_stability": {"heartbeats": 4, "stable": True},
                })
                return subprocess.CompletedProcess(argv, 0)
            write_json(round_dir / "mavftp_gate.json", {
                "verdict": "RED",
                "reason": "ftp_read_empty:@PARAM/param.pck",
            })
            return subprocess.CompletedProcess(argv, 2)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
                payload = module.run_benchmark(self.args(Path(tmp)))

        self.assertEqual(payload["verdict"], "RED")
        self.assertEqual(payload["reason"], "round_failed")
        self.assertEqual(payload["failed_rounds"], [2])

    def test_run_benchmark_fails_on_slow_p95(self):
        module = load_module()

        def fake_run(argv, stdout, stderr, check):
            round_dir = Path(stdout.name).parent
            write_json(round_dir / "mavftp_gate.json", {
                "verdict": "GREEN",
                "reason": "mavftp_ok",
                "read_@PARAM/param.pck": {"bytes": 4096},
                "param_pck_decode": {"decoded_count": 947},
                "post_ftp_stability": {"heartbeats": 4, "stable": True},
            })
            return subprocess.CompletedProcess(argv, 0)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=fake_run), \
                    mock.patch.object(module.time, "monotonic", side_effect=[0.0, 10.0, 20.0, 32.0, 40.0, 95.0]):
                payload = module.run_benchmark(self.args(Path(tmp), max_p95_total=45.0))

        self.assertEqual(payload["verdict"], "RED")
        self.assertEqual(payload["reason"], "p95_total_slow")

    def test_run_benchmark_fails_on_unstable_heartbeat(self):
        module = load_module()

        def fake_run(argv, stdout, stderr, check):
            round_dir = Path(stdout.name).parent
            write_json(round_dir / "mavftp_gate.json", {
                "verdict": "GREEN",
                "reason": "mavftp_ok",
                "read_@PARAM/param.pck": {"bytes": 4096},
                "param_pck_decode": {"decoded_count": 947},
                "post_ftp_stability": {"heartbeats": 4, "stable": False},
            })
            return subprocess.CompletedProcess(argv, 0)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
                payload = module.run_benchmark(self.args(Path(tmp)))

        self.assertEqual(payload["verdict"], "RED")
        self.assertEqual(payload["reason"], "post_ftp_heartbeat_unstable")


if __name__ == "__main__":
    unittest.main()
