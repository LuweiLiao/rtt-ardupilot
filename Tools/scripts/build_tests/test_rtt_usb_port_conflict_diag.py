#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_usb_port_conflict_diag.py"


def load_module():
    spec = importlib.util.spec_from_file_location("rtt_usb_port_conflict_diag", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class UsbPortConflictDiagTest(unittest.TestCase):
    def test_lsof_field_parser(self):
        module = load_module()
        parsed = module.parse_lsof_field_output("p123\ncQGroundControl\nu1000\nn/dev/ttyACM0\n")
        self.assertEqual(parsed, [{
            "pid": 123,
            "command": "QGroundControl",
            "uid": "1000",
            "name": "/dev/ttyACM0",
        }])

    def test_classifies_rtt_interfaces_from_by_id(self):
        module = load_module()
        mavlink = module.classify_port({
            "device": "/dev/ttyACM1",
            "by_id": ["/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B003900-if00"],
            "vid": "1209",
            "pid": "5740",
        })
        slcan = module.classify_port({
            "device": "/dev/ttyACM2",
            "by_id": ["/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B003900-if02"],
            "vid": "1209",
            "pid": "5740",
        })
        self.assertEqual(mavlink, "rtt_mavlink_cdc")
        self.assertEqual(slcan, "rtt_slcan_cdc")

    def test_owned_rtt_port_is_red(self):
        module = load_module()
        verdict, reasons = module.classify_payload({
            "ports": [{
                "role": "rtt_mavlink_cdc",
                "owned": True,
                "lsof": {"processes": [{"pid": 123, "command": "QGroundControl"}]},
                "fuser": {"pids": [123]},
            }],
            "interferers": [],
        })
        self.assertEqual(verdict, "RED")
        self.assertIn("rtt_usb_port_owned", reasons)


if __name__ == "__main__":
    unittest.main()
