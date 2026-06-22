#!/usr/bin/env python3
"""Validate RTT hardware capability manifests.

The manifest is a machine-readable contract between board hwdef, host-side
gates, and release documentation.  This checker keeps the contract from drifting
away from the actual CUAV V5 RTT hwdef and from the minimum replacement gates.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


DEFAULT_MANIFEST = "docs/rtt-porting/manifests/cuav_v5_rtt_capabilities.json"
VALID_STATUS = {"proven", "partial", "boundary", "open", "blocked"}
REQUIRED_CAPABILITIES = {
    "scons_full_build",
    "l0_boot_build",
    "main_loop_400hz",
    "usb_mavlink_cdc_if00",
    "usb_param_download_fast",
    "mavlink_ftp_sdcard",
    "usb_slcan_cdc_if02",
    "socketcan_standard_and_extended",
    "dronecan_node_status",
    "imu_ins_driver_streams",
    "compass_driver_streams",
    "barometer_driver_streams",
    "sdcard_dataflash_logging",
    "qgc_missionplanner_host_evidence",
    "driver_level_peripheral_health",
    "prearm_flight_readiness",
    "workspace_artifact_audit",
}
REQUIRED_PROVEN = {
    "scons_full_build",
    "l0_boot_build",
    "main_loop_400hz",
    "usb_mavlink_cdc_if00",
    "usb_param_download_fast",
    "mavlink_ftp_sdcard",
    "usb_slcan_cdc_if02",
    "socketcan_standard_and_extended",
    "dronecan_node_status",
    "imu_ins_driver_streams",
    "compass_driver_streams",
    "barometer_driver_streams",
    "sdcard_dataflash_logging",
    "driver_level_peripheral_health",
    "workspace_artifact_audit",
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as infile:
        data = json.load(infile)
    if not isinstance(data, dict):
        raise ValueError("manifest_root_not_object")
    return data


def parse_hwdef(path: Path) -> dict[str, Any]:
    values: dict[str, Any] = {}
    with path.open("r", encoding="utf-8") as infile:
        for raw_line in infile:
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            key = parts[0]
            if key == "define" and len(parts) >= 3:
                values[parts[1]] = " ".join(parts[2:])
            elif len(parts) >= 2 and key in {
                "BOARD_NAME",
                "MCU",
                "FLASH_SIZE_KB",
                "FLASH_RESERVE_START_KB",
                "SERIAL_ORDER",
                "APJ_BOARD_ID",
            }:
                values[key] = parts[1:] if key in ("MCU", "SERIAL_ORDER") else parts[1]
    return values


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else [value]


def validate_manifest(manifest: dict[str, Any], root: Path) -> tuple[str, list[str]]:
    errors: list[str] = []

    if manifest.get("schema") != "ardupilot-rtt-capability-manifest-v1":
        errors.append("schema_mismatch")

    board = manifest.get("board", {})
    if board.get("rtt_board_name") != "cuav_v5":
        errors.append("board.rtt_board_name_mismatch")
    if board.get("target") != "cuav-v5":
        errors.append("board.target_mismatch")

    build = manifest.get("build", {})
    if build.get("system") != "SCons":
        errors.append("build.system_must_be_SCons")
    if "waf" not in build.get("forbidden_systems", []):
        errors.append("build.forbidden_systems_missing_waf")

    usb = manifest.get("usb", {})
    functions = usb.get("functions", [])
    if not isinstance(functions, list):
        errors.append("usb.functions_not_list")
        functions = []
    interfaces = {item.get("interface"): item for item in functions if isinstance(item, dict)}
    if "if00" not in interfaces:
        errors.append("usb.missing_mavlink_if00")
    if "if02" not in interfaces:
        errors.append("usb.missing_slcan_if02")
    if interfaces.get("if00", {}).get("mission_planner_role") != "MAVLink":
        errors.append("usb.if00_not_mavlink")
    if interfaces.get("if02", {}).get("mission_planner_role") != "SLCAN":
        errors.append("usb.if02_not_slcan")

    caps = manifest.get("capabilities", [])
    if not isinstance(caps, list):
        errors.append("capabilities_not_list")
        caps = []
    cap_ids = [cap.get("id") for cap in caps if isinstance(cap, dict)]
    cap_id_set = set(cap_ids)
    duplicates = sorted({cap_id for cap_id in cap_ids if cap_ids.count(cap_id) > 1})
    for cap_id in duplicates:
        errors.append(f"capability.duplicate:{cap_id}")
    missing = sorted(REQUIRED_CAPABILITIES - cap_id_set)
    for cap_id in missing:
        errors.append(f"capability.missing:{cap_id}")
    for cap in caps:
        if not isinstance(cap, dict):
            errors.append("capability.entry_not_object")
            continue
        cap_id = str(cap.get("id", ""))
        status = cap.get("status")
        if status not in VALID_STATUS:
            errors.append(f"capability.invalid_status:{cap_id}:{status}")
        if cap_id in REQUIRED_PROVEN and status != "proven":
            errors.append(f"capability.not_proven:{cap_id}:{status}")
        if not cap.get("gate"):
            errors.append(f"capability.missing_gate:{cap_id}")
        evidence = cap.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            errors.append(f"capability.missing_evidence:{cap_id}")

    hwdef_contract = manifest.get("hwdef_contract", {})
    hwdef_path = root / str(hwdef_contract.get("path", ""))
    if not hwdef_path.is_file():
        errors.append(f"hwdef.missing:{hwdef_path}")
    else:
        hwdef = parse_hwdef(hwdef_path)
        expected = hwdef_contract.get("expected", {})
        if not isinstance(expected, dict):
            errors.append("hwdef.expected_not_object")
            expected = {}
        for key, expected_value in expected.items():
            actual = hwdef.get(key)
            if key in ("MCU", "SERIAL_ORDER"):
                if [str(v) for v in as_list(actual)] != [str(v) for v in as_list(expected_value)]:
                    errors.append(f"hwdef.mismatch:{key}:actual={actual}:expected={expected_value}")
            elif str(actual) != str(expected_value):
                errors.append(f"hwdef.mismatch:{key}:actual={actual}:expected={expected_value}")

    return ("GREEN" if not errors else "RED"), errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--root", default=".")
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    manifest_path = (root / args.manifest).resolve() if not Path(args.manifest).is_absolute() else Path(args.manifest)
    manifest = load_json(manifest_path)
    verdict, errors = validate_manifest(manifest, root)
    payload = {
        "manifest": str(manifest_path),
        "verdict": verdict,
        "errors": errors,
        "capability_count": len(manifest.get("capabilities", [])),
    }
    output = json.dumps(payload, indent=2 if args.pretty else None, sort_keys=True)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0 if verdict == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
