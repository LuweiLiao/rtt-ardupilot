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


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_acceptance_suite.py"


def load_module():
    script_dir = str(SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    spec = importlib.util.spec_from_file_location("rtt_acceptance_suite", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload) + "\n", encoding="utf-8")


class AcceptanceSuiteTest(unittest.TestCase):
    def args(self, outdir: Path, **overrides):
        values = {
            "port": "auto",
            "slcan_port": "auto",
            "outdir": str(outdir),
            "manifest": "docs/rtt-porting/manifests/cuav_v5_rtt_capabilities.json",
            "full_chain": True,
            "rounds": 1,
            "include_socketcan": True,
            "socketcan_iface": "can_rtt0",
            "sudo_socketcan": False,
            "param_benchmark": True,
            "param_benchmark_rounds": 3,
            "allow_boundary_yellow": True,
            "include_param_persist": False,
            "persist_param": "LOG_DISARMED",
            "strict_prearm_health": False,
            "allow_strict_prearm_red": False,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def fake_run_factory(self):
        def fake_run(argv, stdout, stderr, check):
            outdir = Path(stdout.name).parent
            name = outdir.name
            if name == "manifest_check":
                write_json(outdir / "capability_manifest_check.json", {
                    "verdict": "GREEN",
                    "capability_count": 17,
                    "errors": [],
                })
            elif name == "qgc_mp_host_evidence":
                write_json(outdir / "qgc_mp_evidence.json", {
                    "verdict": "YELLOW",
                    "reasons": ["no_qgc_or_missionplanner_window"],
                })
            elif name == "loop_rate":
                write_json(outdir / "loop_rate_gate.json", {"verdict": "GREEN", "reason": "loop_rate_ok"})
            elif name == "param_download":
                write_json(outdir / "param_download.json", {
                    "verdict": "GREEN",
                    "reason": "complete_fast",
                    "elapsed_s": 1.2,
                    "missing_count": 0,
                })
            elif name == "param_download_benchmark":
                write_json(outdir / "param_download_benchmark.json", {
                    "verdict": "GREEN",
                    "reason": "param_download_benchmark_ok",
                    "rounds_completed": 3,
                    "failed_rounds": [],
                    "metrics": {
                        "elapsed_s": {"p50": 1.2, "p95": 1.3, "max": 1.3},
                        "rate_params_s": {"p50": 700.0},
                    },
                })
            elif name == "mavftp":
                write_json(outdir / "mavftp_gate.json", {"verdict": "GREEN", "reason": "mavftp_ok"})
            elif name == "peripheral_driver":
                write_json(outdir / "peripheral_health.json", {
                    "verdict": "GREEN",
                    "driver_verdict": "GREEN",
                    "calibration_verdict": "RED",
                })
            elif name == "log_download":
                write_json(outdir / "log_download_gate.json", {"verdict": "GREEN", "reason": "log_ok"})
            elif name == "socketcan_dronecan":
                write_json(outdir / "socketcan_dronecan_gate.json", {"verdict": "GREEN", "reason": "can_ok"})
            elif name == "source_artifact_audit":
                write_json(outdir / "source_artifact_audit.json", {
                    "tracked_count": 0,
                    "untracked_count": 0,
                })
            else:
                self.fail(f"unexpected step {name}")
            return subprocess.CompletedProcess(argv, 0)
        return fake_run

    def test_full_chain_matrix_covers_manifest_required_gates(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=self.fake_run_factory()), \
                    mock.patch.object(module, "cleanup_openocd", return_value=None):
                payload = module.run_suite(self.args(Path(tmp)))

        self.assertEqual(payload["verdict"], "GREEN", payload.get("reason"))
        self.assertEqual(payload["manifest_matrix"]["missing_required"], [])
        self.assertEqual(payload["manifest_matrix"]["failed_required"], [])
        statuses = {entry["id"]: entry["coverage_status"] for entry in payload["manifest_matrix"]["entries"]}
        self.assertEqual(statuses["main_loop_400hz"], "covered_passed")
        self.assertEqual(statuses["usb_slcan_cdc_if02"], "covered_passed")
        self.assertEqual(statuses["prearm_flight_readiness"], "manifest_boundary")

    def test_full_chain_without_socketcan_reports_missing_required_can(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=self.fake_run_factory()), \
                    mock.patch.object(module, "cleanup_openocd", return_value=None):
                payload = module.run_suite(self.args(Path(tmp), include_socketcan=False))

        self.assertEqual(payload["verdict"], "RED")
        self.assertEqual(payload["reason"], "manifest_required_coverage_failed")
        self.assertIn("usb_slcan_cdc_if02", payload["manifest_matrix"]["missing_required"])
        self.assertIn("dronecan_node_status", payload["manifest_matrix"]["missing_required"])

    def test_rounds_run_serially(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module.subprocess, "run", side_effect=self.fake_run_factory()), \
                    mock.patch.object(module, "cleanup_openocd", return_value=None):
                payload = module.run_suite(self.args(Path(tmp), rounds=2))

        self.assertEqual(payload["verdict"], "GREEN")
        self.assertEqual(len(payload["rounds"]), 2)
        self.assertEqual(payload["rounds"][0]["round"], 1)
        self.assertEqual(payload["rounds"][1]["round"], 2)


if __name__ == "__main__":
    unittest.main()
