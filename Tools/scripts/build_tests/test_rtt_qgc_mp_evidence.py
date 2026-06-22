#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_qgc_mp_evidence.py"


def load_module():
    spec = importlib.util.spec_from_file_location("rtt_qgc_mp_evidence", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class QgcMpEvidenceTest(unittest.TestCase):
    def test_parse_wmctrl_filters_qgc_and_missionplanner(self):
        module = load_module()
        windows = module.parse_wmctrl(
            "0x01200003  0 123 QGroundControl.QGroundControl host QGroundControl\n"
            "0x01400003  0 456 chrome.Chrome host Browser\n"
            "0x01600003  0 789 MissionPlanner.MissionPlanner host Mission Planner\n"
        )
        self.assertEqual([w["app"] for w in windows], ["QGroundControl", "MissionPlanner"])
        self.assertEqual(windows[0]["window_id"], "0x01200003")
        self.assertEqual(windows[1]["pid"], 789)

    def test_collect_processes_filters_self_pgrep_noise(self):
        module = load_module()

        def fake_runner(argv, timeout_s):
            if "QGroundControl" in argv[-1]:
                return {
                    "available": True,
                    "returncode": 0,
                    "stdout": "111 /opt/QGroundControl.AppImage\n222 pgrep -af QGroundControl\n",
                    "stderr": "",
                }
            return {"available": True, "returncode": 1, "stdout": "", "stderr": ""}

        processes = module.collect_processes(fake_runner)
        qgc = processes[0]
        self.assertTrue(qgc["active"])
        self.assertEqual(qgc["matches"], ["111 /opt/QGroundControl.AppImage"])

    def test_usb_red_makes_payload_red(self):
        module = load_module()
        verdict, reasons = module.classify_payload({
            "usb": {"verdict": "RED"},
            "processes": [],
            "windows": {"wmctrl": {"windows": []}},
            "screenshots": [],
        })
        self.assertEqual(verdict, "RED")
        self.assertIn("usb_port_conflict", reasons)

    def test_qgc_owning_rtt_mavlink_port_is_green_connection_evidence(self):
        module = load_module()
        verdict, reasons = module.classify_payload({
            "usb": {
                "verdict": "RED",
                "ports": [{
                    "role": "rtt_mavlink_cdc",
                    "owned": True,
                    "lsof": {
                        "processes": [{
                            "pid": 123,
                            "command": "QGroundControl",
                            "name": "/dev/ttyACM1",
                        }]
                    },
                }],
            },
            "processes": [{"name": "QGroundControl", "active": True}],
            "windows": {
                "wmctrl": {"windows": [{"app": "QGroundControl", "pid": 123}]},
                "wayland_display": "",
                "display": ":1",
            },
            "screenshots": [],
        })
        self.assertEqual(verdict, "GREEN")
        self.assertEqual(reasons, ["qgc_window_visible_and_owns_rtt_mavlink_cdc"])

    def test_no_gui_process_is_yellow_evidence_not_failure(self):
        module = load_module()
        verdict, reasons = module.classify_payload({
            "usb": {"verdict": "GREEN"},
            "processes": [{"name": "QGroundControl", "active": False}],
            "windows": {"wmctrl": {"windows": []}, "wayland_display": "", "display": ":1"},
            "screenshots": [],
        })
        self.assertEqual(verdict, "YELLOW")
        self.assertIn("no_qgc_or_missionplanner_process", reasons)
        self.assertIn("no_qgc_or_missionplanner_window", reasons)


if __name__ == "__main__":
    unittest.main()
