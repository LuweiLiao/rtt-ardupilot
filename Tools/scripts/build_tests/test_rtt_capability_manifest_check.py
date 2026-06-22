#!/usr/bin/env python3

import copy
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "rtt_capability_manifest_check.py"
ROOT = SCRIPT.parents[2]
MANIFEST = ROOT / "docs/rtt-porting/manifests/cuav_v5_rtt_capabilities.json"


def load_module():
    spec = importlib.util.spec_from_file_location("rtt_capability_manifest_check", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CapabilityManifestCheckTest(unittest.TestCase):
    def test_current_manifest_is_green(self):
        module = load_module()
        manifest = module.load_json(MANIFEST)
        verdict, errors = module.validate_manifest(manifest, ROOT)
        self.assertEqual(verdict, "GREEN", errors)
        self.assertEqual(errors, [])

    def test_missing_required_capability_is_red(self):
        module = load_module()
        manifest = module.load_json(MANIFEST)
        edited = copy.deepcopy(manifest)
        edited["capabilities"] = [
            cap for cap in edited["capabilities"]
            if cap["id"] != "usb_param_download_fast"
        ]
        verdict, errors = module.validate_manifest(edited, ROOT)
        self.assertEqual(verdict, "RED")
        self.assertIn("capability.missing:usb_param_download_fast", errors)

    def test_hwdef_contract_mismatch_is_red(self):
        module = load_module()
        manifest = module.load_json(MANIFEST)
        edited = copy.deepcopy(manifest)
        edited["hwdef_contract"]["expected"]["DEFAULT_SERIAL0_BAUD"] = "115200"
        verdict, errors = module.validate_manifest(edited, ROOT)
        self.assertEqual(verdict, "RED")
        self.assertTrue(any(error.startswith("hwdef.mismatch:DEFAULT_SERIAL0_BAUD") for error in errors))

    def test_usb_roles_are_strict(self):
        module = load_module()
        manifest = module.load_json(MANIFEST)
        edited = copy.deepcopy(manifest)
        edited["usb"]["functions"][0]["mission_planner_role"] = "SLCAN"
        verdict, errors = module.validate_manifest(edited, ROOT)
        self.assertEqual(verdict, "RED")
        self.assertIn("usb.if00_not_mavlink", errors)


if __name__ == "__main__":
    unittest.main()
