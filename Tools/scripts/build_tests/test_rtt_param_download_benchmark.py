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


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_param_download_benchmark.py"


def load_module():
    script_dir = str(SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    spec = importlib.util.spec_from_file_location("rtt_param_download_benchmark", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload) + "\n", encoding="utf-8")


class ParamDownloadBenchmarkTest(unittest.TestCase):
    def args(self, outdir: Path, **overrides):
        values = {
            "port": "auto",
            "outdir": str(outdir),
            "rounds": 3,
            "inter_round_sleep": 0.0,
            "round_timeout_s": 80.0,
            "timeout": 45.0,
            "settle_timeout": 30.0,
            "drain": 0.5,
            "round_max_total": 12.0,
            "round_max_gap": 1.0,
            "round_min_rate": 90.0,
            "max_p95_total": 4.0,
            "max_max_total": 6.0,
            "max_p95_gap": 0.2,
            "max_max_gap": 0.5,
            "min_p50_rate": 300.0,
            "source_system": 245,
            "accept_status": [3, 4, 5],
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_metric_summary_uses_expected_percentiles(self):
        module = load_module()
        rounds = [{"elapsed_s": v} for v in [1.0, 1.2, 1.4, 2.0, 3.0]]
        summary = module.metric_summary(rounds, "elapsed_s")
        self.assertEqual(summary["min"], 1.0)
        self.assertEqual(summary["p50"], 1.4)
        self.assertEqual(summary["p95"], 3.0)
        self.assertEqual(summary["max"], 3.0)

    def test_classify_red_on_failed_round(self):
        module = load_module()
        args = self.args(Path("/tmp"))
        payload = {
            "failed_rounds": [2],
            "metrics": {
                "elapsed_s": {"p95": 1.2, "max": 1.3},
                "max_gap_s_observed": {"p95": 0.01, "max": 0.02},
                "rate_params_s": {"p50": 600.0},
            },
        }
        self.assertEqual(module.classify(payload, args), ("RED", "round_failed"))

    def test_run_benchmark_aggregates_green_rounds(self):
        module = load_module()

        def fake_run(argv, stdout, stderr, check):
            round_dir = Path(stdout.name).parent
            round_index = int(round_dir.name.split("_")[-1])
            elapsed = [1.1, 1.2, 1.3][round_index - 1]
            write_json(round_dir / "param_download.json", {
                "verdict": "GREEN",
                "reason": "complete_fast",
                "reported_count": 947,
                "unique_indices": 947,
                "missing_count": 0,
                "elapsed_s": elapsed,
                "first_response_latency_s": 0.006,
                "max_gap_s_observed": 0.01,
                "p95_gap_s": 0.01,
                "rate_params_s": 700.0,
            })
            return subprocess.CompletedProcess(argv, 0)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
                payload = module.run_benchmark(self.args(Path(tmp)))

        self.assertEqual(payload["verdict"], "GREEN", payload.get("reason"))
        self.assertEqual(payload["rounds_completed"], 3)
        self.assertEqual(payload["failed_rounds"], [])
        self.assertEqual(payload["metrics"]["elapsed_s"]["p50"], 1.2)
        self.assertEqual(payload["metrics"]["elapsed_s"]["max"], 1.3)

    def test_run_benchmark_fails_on_slow_p95(self):
        module = load_module()

        def fake_run(argv, stdout, stderr, check):
            round_dir = Path(stdout.name).parent
            round_index = int(round_dir.name.split("_")[-1])
            elapsed = [1.0, 1.1, 5.0][round_index - 1]
            write_json(round_dir / "param_download.json", {
                "verdict": "GREEN",
                "reason": "complete_fast",
                "missing_count": 0,
                "elapsed_s": elapsed,
                "max_gap_s_observed": 0.01,
                "p95_gap_s": 0.01,
                "rate_params_s": 700.0,
            })
            return subprocess.CompletedProcess(argv, 0)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=fake_run):
                payload = module.run_benchmark(self.args(Path(tmp), max_p95_total=4.0))

        self.assertEqual(payload["verdict"], "RED")
        self.assertEqual(payload["reason"], "p95_total_slow")


if __name__ == "__main__":
    unittest.main()
