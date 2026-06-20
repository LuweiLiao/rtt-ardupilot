#!/usr/bin/env python3
"""Audit RTT Windows USB/Mission Planner acceptance evidence.

The script reads evidence produced by:

  - Tools/scripts/rtt_windows_acceptance_all.ps1
  - Tools/scripts/rtt_windows_usb_diag.ps1
  - Tools/scripts/rtt_windows_mavlink_acceptance.py

It intentionally does not touch hardware.  Its job is to prevent a stale or
partial Windows report from being mistaken for a completed Mission Planner gate.
"""

from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path
from typing import Any


REQUIRED_TARGET = "VID_1209&PID_5740&MI_00"
REQUIRED_TARGET_ALT = "VID_1209&PID_5740&MAVLINK_INTERFACE_STRING"
DIAGNOSTIC_TARGET = "VID_1209&PID_5741&DIAGNOSTIC_MAVLINK_SINGLE_CDC"
SLCAN_TARGET = "VID_1209&PID_5740&MI_02"
MISSING = object()


def read_json(path: Path | None) -> Any | None:
    if path is None or not path.exists():
        return None
    with path.open("r", encoding="utf-8-sig") as fp:
        return json.load(fp)


def raw_mavlink_magic_count(raw_probe: Any) -> int:
    if not isinstance(raw_probe, dict):
        return 0
    if raw_probe.get("mavlink_magic_count") is not None:
        return int(raw_probe.get("mavlink_magic_count") or 0)
    count = int(raw_probe.get("mavlink_v1_magic_count") or 0)
    count += int(raw_probe.get("mavlink_v2_magic_count") or 0)
    probes = raw_probe.get("probes")
    if isinstance(probes, list):
        count += sum(
            int(item.get("mavlink_v1_magic_count") or 0) +
            int(item.get("mavlink_v2_magic_count") or 0)
            for item in probes
            if isinstance(item, dict)
        )
    return count


def latest_file(root: Path, name: str) -> Path | None:
    matches = sorted(root.rglob(name), key=lambda p: p.stat().st_mtime, reverse=True)
    return matches[0] if matches else None


def find_acceptance_summary(root: Path) -> Path | None:
    direct = root / "summary.json"
    if direct.exists():
        return direct
    return latest_file(root, "summary.json")


def find_usb_summary(root: Path, acceptance: dict[str, Any] | None) -> Path | None:
    if acceptance:
        value = acceptance.get("UsbSummaryPath")
        if value:
            p = Path(str(value))
            if p.exists():
                return p
            q = root / p
            if q.exists():
                return q
            q = root / p.name
            if q.exists():
                return q
    usb_diag = root / "usb_diag"
    if usb_diag.exists():
        return latest_file(usb_diag, "summary.json")
    if (root / "target_pnp_devices.json").exists() or (root / "target_com_ports.json").exists():
        return root / "summary.json" if (root / "summary.json").exists() else None
    return None


def find_mavlink_summary(root: Path, acceptance: dict[str, Any] | None) -> Path | None:
    if acceptance:
        json_path = acceptance.get("MavlinkJsonPath") or acceptance.get("MavlinkSummaryPath")
        if json_path:
            p = Path(str(json_path))
            if p.exists():
                return p
            q = root / p
            if q.exists():
                return q
            q = root / p.name
            if q.exists():
                return q
        mavlink_dir = root / "mavlink"
        if mavlink_dir.exists():
            return latest_file(mavlink_dir, "mavlink_acceptance.json")
    mavlink_dir = root / "mavlink"
    if mavlink_dir.exists():
        return latest_file(mavlink_dir, "mavlink_acceptance.json")
    return latest_file(root, "mavlink_acceptance.json")


def find_mission_planner_evidence(root: Path) -> Path | None:
    for name in (
        "mission_planner_evidence.json",
        "missionplanner_evidence.json",
        "mission_planner.json",
    ):
        direct = root / name
        if direct.exists():
            return direct
    mp_dir = root / "mission_planner"
    if mp_dir.exists():
        for name in (
            "mission_planner_evidence.json",
            "missionplanner_evidence.json",
            "mission_planner.json",
        ):
            found = latest_file(mp_dir, name)
            if found:
                return found
    return None


def find_firmware_snapshot(root: Path) -> Path | None:
    for name in (
        "usb_debug_snapshot.json",
        "firmware_usb_debug_snapshot.json",
    ):
        direct = root / name
        if direct.exists():
            return direct
    for dirname in (
        "firmware_snapshot",
        "firmware_usb_snapshot",
        "usb_debug_snapshot",
    ):
        snap_dir = root / dirname
        if snap_dir.exists():
            found = latest_file(snap_dir, "usb_debug_snapshot.json")
            if found:
                return found
    return latest_file(root, "usb_debug_snapshot.json")


def find_firmware_descriptor_summary(root: Path, acceptance: dict[str, Any] | None) -> Path | None:
    if acceptance:
        value = acceptance.get("FirmwareDescriptorSummaryPath")
        if value:
            p = Path(str(value))
            if p.exists():
                return p
            q = root / p
            if q.exists():
                return q
            q = root / p.name
            if q.exists():
                return q
    descriptor_dir = root / "firmware_descriptor"
    if descriptor_dir.exists():
        direct = descriptor_dir / "descriptor.json"
        if direct.exists():
            return direct
        return latest_file(descriptor_dir, "descriptor.json")
    return None


def as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def passfail(name: str, passed: bool, evidence: Any = None, reason: str | None = None) -> dict[str, Any]:
    item: dict[str, Any] = {
        "name": name,
        "status": "PASS" if passed else "FAIL",
    }
    if evidence is not None:
        item["evidence"] = evidence
    if reason:
        item["reason"] = reason
    return item


def audit(root: Path) -> dict[str, Any]:
    root = root.resolve()
    acceptance_path = find_acceptance_summary(root)
    acceptance = read_json(acceptance_path)
    if acceptance_path and "EvidenceDir" not in (acceptance or {}) and root.name == "usb_diag":
        acceptance = None
        acceptance_path = None

    usb_path = find_usb_summary(root, acceptance if isinstance(acceptance, dict) else None)
    usb = read_json(usb_path)
    mav_path = find_mavlink_summary(root, acceptance if isinstance(acceptance, dict) else None)
    mav = read_json(mav_path)
    mp_path = find_mission_planner_evidence(root)
    mp = read_json(mp_path)
    firmware_snapshot_path = find_firmware_snapshot(root)
    firmware_snapshot = read_json(firmware_snapshot_path)
    firmware_descriptor_path = find_firmware_descriptor_summary(
        root,
        acceptance if isinstance(acceptance, dict) else None,
    )
    firmware_descriptor = read_json(firmware_descriptor_path)

    checks: list[dict[str, Any]] = []

    usb_verdict = usb.get("Verdict") if isinstance(usb, dict) else None
    mi00_count = int(usb.get("MI00ComCount") or 0) if isinstance(usb, dict) else 0
    mavlink_string_count = int(usb.get("MavlinkInterfaceStringComCount") or 0) if isinstance(usb, dict) else 0
    target_no_mi_count = int(usb.get("TargetNoMiComCount") or 0) if isinstance(usb, dict) else 0
    target_no_mi_usbser_count = int(usb.get("TargetNoMiUsbserComCount") or 0) if isinstance(usb, dict) else 0
    diagnostic_5741_count = int(usb.get("Diagnostic5741MavlinkComCount") or 0) if isinstance(usb, dict) else 0
    allow_diagnostic_5741 = bool(usb.get("AllowDiagnostic5741")) if isinstance(usb, dict) else False
    topology = usb.get("TargetTopology") if isinstance(usb, dict) else None
    parent_device_count = int(usb.get("TargetParentDeviceCount") or 0) if isinstance(usb, dict) else 0
    interface_device_count = int(usb.get("TargetInterfaceDeviceCount") or 0) if isinstance(usb, dict) else 0
    mission_ports = as_list(usb.get("MissionPlannerComCandidates") if isinstance(usb, dict) else None)
    slcan_ports = as_list(usb.get("SlcanComCandidates") if isinstance(usb, dict) else None)
    mavlink_candidate_present = (
        len(mission_ports) >= 1
        and (
            mi00_count >= 1
            or mavlink_string_count >= 1
            or (allow_diagnostic_5741 and diagnostic_5741_count == 1)
        )
    )

    checks.append(passfail(
        "windows_usb_summary_present",
        isinstance(usb, dict),
        str(usb_path) if usb_path else None,
        None if isinstance(usb, dict) else "missing_usb_diag_summary",
    ))
    checks.append(passfail(
        "windows_mavlink_com_present",
        mavlink_candidate_present,
        {
            "usb_verdict": usb_verdict,
            "mi00_com_count": mi00_count,
            "mavlink_interface_string_com_count": mavlink_string_count,
            "target_no_mi_com_count": target_no_mi_count,
            "target_no_mi_usbser_com_count": target_no_mi_usbser_count,
            "target_parent_device_count": parent_device_count,
            "target_interface_device_count": interface_device_count,
            "target_topology": topology,
            "allow_diagnostic_5741": allow_diagnostic_5741,
            "diagnostic_5741_mavlink_com_count": diagnostic_5741_count,
            "mission_planner_com_candidates": mission_ports,
            "target": REQUIRED_TARGET,
            "alternate_target": REQUIRED_TARGET_ALT,
            "diagnostic_target": DIAGNOSTIC_TARGET,
        },
        None if mavlink_candidate_present else "no_windows_mavlink_com",
    ))

    fw_descriptor_device = (
        firmware_descriptor.get("descriptor", {}).get("device", {})
        if isinstance(firmware_descriptor, dict) else {}
    )
    fw_descriptor_strings = (
        firmware_descriptor.get("descriptor", {}).get("strings", [])
        if isinstance(firmware_descriptor, dict) else []
    )
    fw_strings_by_index = {
        item.get("index"): item
        for item in fw_descriptor_strings
        if isinstance(item, dict)
    }
    fw_descriptor_is_chibios_dualcdc = (
        isinstance(firmware_descriptor, dict)
        and firmware_descriptor.get("verdict") == "GREEN"
        and firmware_descriptor.get("audit", {}).get("profile") == "chibios_dualcdc"
        and fw_descriptor_device.get("idVendor") == "0x1209"
        and fw_descriptor_device.get("idProduct") == "0x5740"
        and fw_descriptor_device.get("bcdDevice") == "0x0200"
        and fw_descriptor_device.get("bDeviceClass") == 0xEF
        and (fw_strings_by_index.get(1) or {}).get("text") == "ArduPilot"
        and (fw_strings_by_index.get(2) or {}).get("text") == "CUAVv5"
    )
    fw_descriptor_is_mavlink_only_diagnostic = (
        isinstance(firmware_descriptor, dict)
        and firmware_descriptor.get("verdict") == "GREEN"
        and firmware_descriptor.get("audit", {}).get("profile") == "mavlink_only"
        and fw_descriptor_device.get("idVendor") == "0x1209"
        and fw_descriptor_device.get("idProduct") == "0x5741"
    )
    diagnostic_only = allow_diagnostic_5741 and diagnostic_5741_count == 1
    fw_descriptor_ok_for_run = (
        fw_descriptor_is_mavlink_only_diagnostic
        if diagnostic_only else
        fw_descriptor_is_chibios_dualcdc
    )
    checks.append(passfail(
        "firmware_descriptor_final_or_diagnostic_green",
        fw_descriptor_ok_for_run,
        {
            "firmware_descriptor": str(firmware_descriptor_path) if firmware_descriptor_path else None,
            "verdict": firmware_descriptor.get("verdict") if isinstance(firmware_descriptor, dict) else None,
            "profile": firmware_descriptor.get("audit", {}).get("profile") if isinstance(firmware_descriptor, dict) else None,
            "device": fw_descriptor_device,
            "strings": fw_descriptor_strings,
            "chibios_dualcdc_ok": fw_descriptor_is_chibios_dualcdc,
            "mavlink_only_diagnostic_ok": fw_descriptor_is_mavlink_only_diagnostic,
            "diagnostic_only": diagnostic_only,
        },
        None if fw_descriptor_ok_for_run
        else "missing_or_wrong_firmware_descriptor_gate_for_run_mode",
    ))
    slcan_fallback_documented = (
        diagnostic_only
        or usb_verdict in {
            "RED_SINGLE_VISIBLE_SLCAN_NOT_MAVLINK",
        }
    )
    slcan_ok = bool(slcan_ports) or slcan_fallback_documented
    checks.append(passfail(
        "windows_slcan_mi02_classified_or_explicit_fallback",
        slcan_ok,
        {
            "usb_verdict": usb_verdict,
            "slcan_com_candidates": slcan_ports,
            "target": SLCAN_TARGET,
            "diagnostic_only": diagnostic_only,
            "fallback_documented": slcan_fallback_documented,
        },
        None if slcan_ok else "no_mi02_slcan_com_or_explicit_fallback",
    ))

    mav_verdict = mav.get("verdict") if isinstance(mav, dict) else None
    mav_port = mav.get("port") if isinstance(mav, dict) else None
    mav_acceptance_mode = mav.get("acceptance_mode") if isinstance(mav, dict) else None
    mav_accepted_usb_identity = mav.get("accepted_usb_identity") if isinstance(mav, dict) else None
    mav_is_final_dual_cdc = bool(mav.get("is_final_dual_cdc_acceptance")) if isinstance(mav, dict) else False
    mav_is_diagnostic_single_cdc = mav_acceptance_mode == "DIAGNOSTIC_SINGLE_CDC_5741_MAVLINK_ONLY"
    mav_is_no_mi_raw_gate_only = mav_acceptance_mode == "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE"
    param = mav.get("param_download") if isinstance(mav, dict) else None
    param_complete = (
        isinstance(param, dict)
        and param.get("verdict") == "GREEN"
        and int(param.get("missing_count") or 0) == 0
        and int(param.get("unique_indices") or 0) == int(param.get("reported_count") or -1)
    )
    checks.append(passfail(
        "windows_mavlink_acceptance_present",
        isinstance(mav, dict),
        str(mav_path) if mav_path else None,
        None if isinstance(mav, dict) else "missing_mavlink_acceptance_json",
    ))
    checks.append(passfail(
        "windows_mavlink_gate_green",
        mav_verdict == "GREEN" and param_complete,
        {
            "mavlink_verdict": mav_verdict,
            "port": mav_port,
            "acceptance_mode": mav_acceptance_mode,
            "accepted_usb_identity": mav_accepted_usb_identity,
            "is_final_dual_cdc_acceptance": mav_is_final_dual_cdc,
            "param_download": param,
        },
        None if mav_verdict == "GREEN" and param_complete else "mavlink_or_param_download_not_green",
    ))
    identity_ok = (
        mav_is_final_dual_cdc
        or (diagnostic_only and mav_is_diagnostic_single_cdc)
    )
    checks.append(passfail(
        "mavlink_usb_identity_final_or_diagnostic",
        identity_ok,
        {
            "acceptance_mode": mav_acceptance_mode,
            "accepted_usb_identity": mav_accepted_usb_identity,
            "is_final_dual_cdc_acceptance": mav_is_final_dual_cdc,
            "diagnostic_only": diagnostic_only,
            "no_mi_raw_gate_only": mav_is_no_mi_raw_gate_only,
            "required_target": REQUIRED_TARGET,
            "diagnostic_target": DIAGNOSTIC_TARGET,
        },
        None if identity_ok else "mavlink_gate_did_not_prove_final_mi00_or_explicit_diagnostic_identity",
    ))

    raw_probe = mav.get("raw_mavlink_probe") if isinstance(mav, dict) else None
    raw_magic_count = raw_mavlink_magic_count(raw_probe)
    raw_probe_green = (
        isinstance(raw_probe, dict)
        and raw_probe.get("verdict") == "GREEN"
        and raw_magic_count > 0
    )
    checks.append(passfail(
        "windows_raw_mavlink_bytes_seen",
        raw_probe_green,
        {
            "raw_mavlink_probe": raw_probe,
            "magic_count": raw_magic_count,
        },
        None if raw_probe_green else "raw_mavlink_probe_missing_or_no_magic",
    ))
    firmware_classification = (
        firmware_snapshot.get("classification")
        if isinstance(firmware_snapshot, dict)
        else None
    )
    firmware_values = (
        firmware_snapshot.get("values")
        if isinstance(firmware_snapshot, dict)
        else None
    )
    firmware_snapshot_needed = not raw_probe_green
    firmware_snapshot_actionable = (
        isinstance(firmware_snapshot, dict)
        and isinstance(firmware_classification, dict)
    )
    checks.append(passfail(
        "firmware_snapshot_present_when_raw_probe_red",
        (not firmware_snapshot_needed) or firmware_snapshot_actionable,
        {
            "needed": firmware_snapshot_needed,
            "snapshot": str(firmware_snapshot_path) if firmware_snapshot_path else None,
            "classification": firmware_classification,
            "selected_values": {
                key: firmware_values.get(key)
                for key in (
                    "rtt_dbg_usb_setup_stup",
                    "rtt_dbg_usbd_set_interface_calls",
                    "rtt_dbg_usbd_set_interface_same_alt_ack",
                    "rtt_dbg_cherry_get_line_coding_calls",
                    "rtt_dbg_cherry_set_line_coding_calls",
                    "rtt_dbg_cherry_set_dtr_calls",
                    "rtt_dbg_cherry_set_rts_calls",
                    "rtt_dbg_cherry_dtr_state",
                    "rtt_dbg_cherry_rts_state",
                    "rtt_dbg_cherry_bulk_out_calls",
                    "rtt_dbg_cherry_bulk_in_calls",
                    "rtt_dbg_cherry_tx_start_ok",
                )
                if isinstance(firmware_values, dict) and key in firmware_values
            },
        },
        None if ((not firmware_snapshot_needed) or firmware_snapshot_actionable)
        else "raw_probe_red_requires_openocd_firmware_snapshot",
    ))

    overall = acceptance.get("Overall") if isinstance(acceptance, dict) else None
    checks.append(passfail(
        "combined_acceptance_green_or_diagnostic",
        (
            overall == "GREEN_MAVLINK" and mav_is_final_dual_cdc
        ) or (
            diagnostic_only and overall == "GREEN_DIAGNOSTIC_MAVLINK" and mav_is_diagnostic_single_cdc
        ),
        {
            "overall": overall,
            "acceptance_summary": str(acceptance_path) if acceptance_path else None,
            "diagnostic_only": diagnostic_only,
            "mavlink_acceptance_mode": mav_acceptance_mode,
            "mavlink_is_final_dual_cdc": mav_is_final_dual_cdc,
        },
        None if (
            (overall == "GREEN_MAVLINK" and mav_is_final_dual_cdc) or
            (diagnostic_only and overall == "GREEN_DIAGNOSTIC_MAVLINK" and mav_is_diagnostic_single_cdc)
        ) else "combined_summary_not_green_or_missing",
    ))

    mp_green = (
        isinstance(mp, dict)
        and str(mp.get("verdict", "")).upper() == "GREEN"
        and bool(mp.get("mission_planner_connected"))
        and bool(mp.get("param_download_complete"))
    )
    mp_port = mp.get("port") if isinstance(mp, dict) else None
    mp_port_is_candidate = bool(mission_ports) and mp_port in mission_ports
    mp_port_matches_mavlink_gate = (
        isinstance(mp_port, str)
        and isinstance(mav_port, str)
        and mp_port.upper() == mav_port.upper()
    )
    mp_usb_identity = mp.get("usb_identity") if isinstance(mp, dict) else None
    mp_usb_identity_matches_mavlink_gate = (
        isinstance(mp_usb_identity, str)
        and isinstance(mav_accepted_usb_identity, str)
        and mp_usb_identity.upper() == mav_accepted_usb_identity.upper()
    )
    mp_connected_at = mp.get("connected_at") if isinstance(mp, dict) else None
    mp_connected_at_present = isinstance(mp_connected_at, str) and bool(mp_connected_at.strip())
    mav_param_reported_count = int(param.get("reported_count") or 0) if isinstance(param, dict) else 0
    mp_param_count_raw = None
    if isinstance(mp, dict):
        mp_param_count_raw = (
            mp.get("param_count")
            if mp.get("param_count") is not None
            else mp.get("param_reported_count")
        )
    mp_param_count_present = mp_param_count_raw is not None
    mp_param_count_matches = False
    mp_param_count = None
    if mp_param_count_raw is not None:
        try:
            mp_param_count = int(mp_param_count_raw)
            mp_param_count_matches = (mav_param_reported_count > 0 and mp_param_count == mav_param_reported_count)
        except (TypeError, ValueError):
            mp_param_count_matches = False
    mp_evidence_common = {
        "mission_planner_evidence": str(mp_path) if mp_path else None,
        "port": mp_port,
        "mavlink_gate_port": mav_port,
        "mission_planner_com_candidates": mission_ports,
        "verdict": mp.get("verdict") if isinstance(mp, dict) else None,
        "mission_planner_connected": mp.get("mission_planner_connected") if isinstance(mp, dict) else None,
        "param_download_complete": mp.get("param_download_complete") if isinstance(mp, dict) else None,
        "usb_identity": mp_usb_identity,
        "mavlink_gate_usb_identity": mav_accepted_usb_identity,
        "connected_at": mp_connected_at,
        "param_count_present": mp_param_count_present,
        "param_count": mp_param_count_raw,
        "mavlink_gate_reported_count": mav_param_reported_count,
    }
    checks.append(passfail(
        "mission_planner_evidence_green",
        mp_green,
        mp_evidence_common,
        None if mp_green else "missing_or_incomplete_mission_planner_evidence",
    ))
    checks.append(passfail(
        "mission_planner_port_is_mavlink_candidate",
        mp_port_is_candidate,
        mp_evidence_common,
        None if mp_port_is_candidate else "mission_planner_port_not_in_mavlink_candidates",
    ))
    checks.append(passfail(
        "mission_planner_port_matches_mavlink_gate",
        mp_port_matches_mavlink_gate,
        mp_evidence_common,
        None if mp_port_matches_mavlink_gate else "mission_planner_port_differs_from_mavlink_gate",
    ))
    checks.append(passfail(
        "mission_planner_usb_identity_matches_mavlink_gate",
        mp_usb_identity_matches_mavlink_gate,
        mp_evidence_common,
        None if mp_usb_identity_matches_mavlink_gate
        else "mission_planner_usb_identity_missing_or_mismatch",
    ))
    checks.append(passfail(
        "mission_planner_connected_at_present",
        mp_connected_at_present,
        mp_evidence_common,
        None if mp_connected_at_present else "mission_planner_connected_at_missing",
    ))
    checks.append(passfail(
        "mission_planner_param_count_present",
        mp_param_count_present,
        mp_evidence_common,
        None if mp_param_count_present else "mission_planner_param_count_missing",
    ))
    checks.append(passfail(
        "mission_planner_param_count_matches_mavlink_gate",
        mp_param_count_present and mp_param_count_matches,
        mp_evidence_common,
        None if (mp_param_count_present and mp_param_count_matches)
        else "mission_planner_param_count_missing_or_mismatch",
    ))

    passed = all(item["status"] == "PASS" for item in checks)
    final_dual_cdc = passed and not diagnostic_only
    diagnostic_complete = passed and diagnostic_only
    return {
        "evidence_root": str(root),
        "acceptance_summary": str(acceptance_path) if acceptance_path else None,
        "usb_summary": str(usb_path) if usb_path else None,
        "mavlink_summary": str(mav_path) if mav_path else None,
        "mission_planner_evidence": str(mp_path) if mp_path else None,
        "firmware_snapshot": str(firmware_snapshot_path) if firmware_snapshot_path else None,
        "firmware_descriptor": str(firmware_descriptor_path) if firmware_descriptor_path else None,
        "required_target": REQUIRED_TARGET,
        "required_target_alternate": REQUIRED_TARGET_ALT,
        "diagnostic_target": DIAGNOSTIC_TARGET,
        "slcan_target": SLCAN_TARGET,
        "diagnostic_only": diagnostic_only,
        "checks": checks,
        "verdict": "GREEN" if final_dual_cdc else "YELLOW_DIAGNOSTIC" if diagnostic_complete else "RED",
        "reason": (
            "windows_diagnostic_5741_mavlink_complete_not_final_dual_cdc"
            if diagnostic_complete else
            "windows_acceptance_complete"
            if final_dual_cdc else
            "windows_acceptance_incomplete"
        ),
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sample_param_download() -> dict[str, Any]:
    return {
        "verdict": "GREEN",
        "reported_count": 945,
        "unique_indices": 945,
        "missing_count": 0,
        "elapsed_s": 1.8,
    }


def sample_firmware_descriptor(*, pid: str = "0x5740", product: str = "CUAVv5",
                               profile: str = "chibios_dualcdc") -> dict[str, Any]:
    return {
        "verdict": "GREEN",
        "audit": {
            "profile": profile,
            "verdict": "GREEN",
        },
        "descriptor": {
            "device": {
                "bcdUSB": "0x0200",
                "bDeviceClass": 0xEF,
                "bDeviceSubClass": 0x02,
                "bDeviceProtocol": 0x01,
                "idVendor": "0x1209",
                "idProduct": pid,
                "bcdDevice": "0x0200",
                "iManufacturer": 1,
                "iProduct": 2,
                "iSerialNumber": 3,
            },
            "strings": [
                {"index": 0, "langids": ["0x0409"]},
                {"index": 1, "text": "ArduPilot"},
                {"index": 2, "text": product},
                {"index": 3, "text": "000000000000000000000000"},
            ],
        },
    }


def make_sample_evidence(root: Path, *, usb: dict[str, Any], port: str, overall: str = "GREEN_MAVLINK",
                         mp_green: bool = True, mav_green: bool = True,
                         mp_port: str | None = None,
                         mp_param_count: Any = 945,
                         mp_usb_identity: Any = None,
                         mp_connected_at: Any = None,
                         raw_green: bool = True, raw_suite: bool = False,
                         firmware_snapshot: bool = False,
                         firmware_descriptor: Any = None,
                         mav_acceptance_mode: str = "FINAL_DUAL_CDC_5740_MAVLINK_MI00",
                         mav_accepted_usb_identity: str = REQUIRED_TARGET,
                         mav_is_final_dual_cdc: bool = True) -> None:
    usb_path = root / "usb_diag" / "sample" / "summary.json"
    mav_path = root / "mavlink" / "sample" / "mavlink_acceptance.json"
    firmware_descriptor_path = root / "firmware_descriptor" / "descriptor.json"
    write_json(usb_path, usb)
    if firmware_descriptor is not MISSING:
        write_json(
            firmware_descriptor_path,
            firmware_descriptor if isinstance(firmware_descriptor, dict)
            else sample_firmware_descriptor(),
        )
    if raw_suite:
        raw_probe: dict[str, Any] = {
            "verdict": "GREEN" if raw_green else "RED",
            "reason": "mavlink_magic_seen" if raw_green else "no_mavlink_magic_seen",
            "mavlink_magic_count": 2 if raw_green else 0,
            "natural_open": {
                "verdict": "GREEN" if raw_green else "RED",
                "reason": "mavlink_magic_seen" if raw_green else "no_bytes_seen",
                "bytes": 42 if raw_green else 0,
                "mavlink_v1_magic_count": 0,
                "mavlink_v2_magic_count": 2 if raw_green else 0,
            },
            "forced_dtr_rts": {
                "verdict": "GREEN" if raw_green else "RED",
                "reason": "mavlink_magic_seen" if raw_green else "no_bytes_seen",
                "bytes": 42 if raw_green else 0,
                "mavlink_v1_magic_count": 0,
                "mavlink_v2_magic_count": 2 if raw_green else 0,
            },
        }
    else:
        raw_probe = {
            "verdict": "GREEN" if raw_green else "RED",
            "reason": "mavlink_magic_seen" if raw_green else "no_bytes_seen",
            "bytes": 42 if raw_green else 0,
            "mavlink_v1_magic_count": 0,
            "mavlink_v2_magic_count": 2 if raw_green else 0,
        }

    write_json(mav_path, {
        "verdict": "GREEN" if mav_green else "RED",
        "port": port,
        "acceptance_mode": mav_acceptance_mode,
        "accepted_usb_identity": mav_accepted_usb_identity,
        "is_final_dual_cdc_acceptance": mav_is_final_dual_cdc,
        "raw_mavlink_probe": raw_probe,
        "param_download": sample_param_download() if mav_green else {
            "verdict": "RED",
            "reported_count": 945,
            "unique_indices": 0,
            "missing_count": 945,
        },
    })
    write_json(root / "summary.json", {
        "EvidenceDir": str(root),
        "UsbSummaryPath": str(usb_path),
        "MavlinkSummaryPath": str(mav_path),
        "FirmwareDescriptorSummaryPath": str(firmware_descriptor_path),
        "MissionPlannerComCandidates": usb.get("MissionPlannerComCandidates", []),
        "SlcanComCandidates": usb.get("SlcanComCandidates", []),
        "Overall": overall,
    })
    mp_payload = {
        "verdict": "GREEN" if mp_green else "RED",
        "source": "Mission Planner",
        "port": mp_port if mp_port is not None else port,
        "mission_planner_connected": mp_green,
        "param_download_complete": mp_green,
    }
    usb_identity_value = mav_accepted_usb_identity if mp_usb_identity is None else mp_usb_identity
    if usb_identity_value is not MISSING:
        mp_payload["usb_identity"] = usb_identity_value
    connected_at_value = "2026-06-19T00:00:00Z" if mp_connected_at is None else mp_connected_at
    if connected_at_value is not MISSING:
        mp_payload["connected_at"] = connected_at_value
    if mp_param_count is not MISSING:
        mp_payload["param_count"] = mp_param_count
    write_json(root / "mission_planner_evidence.json", mp_payload)
    if firmware_snapshot:
        write_json(root / "firmware_snapshot" / "sample" / "usb_debug_snapshot.json", {
            "classification": {
                "verdict": "RED_NO_CDC_CLASS_REQUESTS",
                "reason": "host_has_not_completed_cdc_class_handshake",
            },
            "values": {
                "rtt_dbg_usb_setup_stup": 0,
                "rtt_dbg_usbd_set_interface_calls": 0,
                "rtt_dbg_cherry_get_line_coding_calls": 0,
                "rtt_dbg_cherry_set_line_coding_calls": 0,
                "rtt_dbg_cherry_set_dtr_calls": 0,
                "rtt_dbg_cherry_set_rts_calls": 0,
                "rtt_dbg_cherry_bulk_out_calls": 0,
                "rtt_dbg_cherry_bulk_in_calls": 0,
                "rtt_dbg_cherry_tx_start_ok": 0,
            },
        })


def self_test() -> dict[str, Any]:
    cases = [
        {
            "name": "explicit_mi00_green",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "want": "GREEN",
        },
        {
            "name": "mavlink_interface_string_green",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 1,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "want": "GREEN",
        },
        {
            "name": "mavlink_only_without_slcan_red",
            "usb": {
                "Verdict": "YELLOW_MAVLINK_ONLY",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": [],
            },
            "port": "COM7",
            "want": "RED",
        },
        {
            "name": "single_target_no_mi_red_even_after_mavlink_gate",
            "usb": {
                "Verdict": "RED_TARGET_DEVICE_NO_MAVLINK_COM",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "TargetNoMiComCount": 1,
                "TargetNoMiUsbserComCount": 1,
                "MissionPlannerComCandidates": [],
                "SlcanComCandidates": [],
            },
            "port": "COM7",
            "mav_acceptance_mode": "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE",
            "mav_accepted_usb_identity": "VID_1209&PID_5740",
            "mav_is_final_dual_cdc": False,
            "want": "RED",
        },
        {
            "name": "diagnostic_5741_green_not_final_dual_cdc",
            "usb": {
                "Verdict": "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY",
                "AllowDiagnostic5741": True,
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "Diagnostic5741MavlinkComCount": 1,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": [],
            },
            "port": "COM7",
            "overall": "GREEN_DIAGNOSTIC_MAVLINK",
            "mav_acceptance_mode": "DIAGNOSTIC_SINGLE_CDC_5741_MAVLINK_ONLY",
            "mav_accepted_usb_identity": "VID_1209&PID_5741",
            "mav_is_final_dual_cdc": False,
            "firmware_descriptor": sample_firmware_descriptor(
                pid="0x5741",
                product="CUAV V5 MAVLink CDC",
                profile="mavlink_only",
            ),
            "want": "YELLOW_DIAGNOSTIC",
        },
        {
            "name": "multiple_target_no_mi_red",
            "usb": {
                "Verdict": "RED_TARGET_DEVICE_NO_MAVLINK_COM",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "TargetNoMiComCount": 2,
                "TargetNoMiUsbserComCount": 2,
                "MissionPlannerComCandidates": ["COM7", "COM8"],
                "SlcanComCandidates": [],
            },
            "port": "COM7",
            "mav_acceptance_mode": "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE",
            "mav_accepted_usb_identity": "VID_1209&PID_5740",
            "mav_is_final_dual_cdc": False,
            "want": "RED",
        },
        {
            "name": "slcan_only_red",
            "usb": {
                "Verdict": "RED_TARGET_DEVICE_NO_MAVLINK_COM",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": [],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM8",
            "want": "RED",
        },
        {
            "name": "single_visible_slcan_not_mission_planner_red",
            "usb": {
                "Verdict": "RED_SINGLE_VISIBLE_SLCAN_NOT_MAVLINK",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "NamedFlightControllerComCount": 1,
                "SingleNamedFlightControllerPort": True,
                "SingleNamedFlightControllerPortKind": "SLCAN_MI02",
                "SinglePortDiagnosis": "SINGLE_VISIBLE_PORT_IS_SLCAN_NOT_MAVLINK",
                "MissionPlannerComCandidates": [],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM8",
            "want": "RED",
        },
        {
            "name": "mission_planner_wrong_port_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM8",
            "mav_acceptance_mode": "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE",
            "mav_accepted_usb_identity": "VID_1209&PID_5740&MI_02",
            "mav_is_final_dual_cdc": False,
            "want": "RED",
        },
        {
            "name": "mission_planner_port_differs_from_mavlink_gate_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7", "COM9"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM9",
            "mp_port": "COM7",
            "want": "RED",
        },
        {
            "name": "mission_planner_param_count_mismatch_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "mp_param_count": 944,
            "want": "RED",
        },
        {
            "name": "mission_planner_param_count_missing_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "mp_param_count": MISSING,
            "want": "RED",
        },
        {
            "name": "mission_planner_usb_identity_missing_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "mp_usb_identity": MISSING,
            "want": "RED",
        },
        {
            "name": "mission_planner_connected_at_missing_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "mp_connected_at": MISSING,
            "want": "RED",
        },
        {
            "name": "no_mi_raw_gate_green_summary_not_final",
            "usb": {
                "Verdict": "YELLOW_MAVLINK_ONLY",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "TargetNoMiComCount": 1,
                "TargetNoMiUsbserComCount": 1,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": [],
            },
            "port": "COM7",
            "overall": "YELLOW_MAVLINK_DATA_PATH_NO_MI_TAG",
            "mav_acceptance_mode": "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE",
            "mav_accepted_usb_identity": "VID_1209&PID_5740",
            "mav_is_final_dual_cdc": False,
            "want": "RED",
        },
        {
            "name": "manual_unlisted_raw_gate_green_not_final",
            "usb": {
                "Verdict": "YELLOW_MAVLINK_ONLY",
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": [],
                "SlcanComCandidates": [],
            },
            "port": "COM10",
            "overall": "YELLOW_MAVLINK_DATA_PATH_UNLISTED_COM",
            "mav_acceptance_mode": "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE",
            "mav_accepted_usb_identity": "UNLISTED_WINDOWS_COM",
            "mav_is_final_dual_cdc": False,
            "want": "RED",
        },
        {
            "name": "raw_probe_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "raw_green": False,
            "want": "RED",
        },
        {
            "name": "raw_probe_red_with_firmware_snapshot_still_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "raw_green": False,
            "firmware_snapshot": True,
            "want": "RED",
        },
        {
            "name": "raw_probe_suite_green",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "raw_suite": True,
            "want": "GREEN",
        },
        {
            "name": "missing_firmware_descriptor_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "firmware_descriptor": MISSING,
            "want": "RED",
        },
        {
            "name": "wrong_firmware_descriptor_red",
            "usb": {
                "Verdict": "GREEN_TWO_COM_PORTS",
                "MI00ComCount": 1,
                "MavlinkInterfaceStringComCount": 0,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": ["COM8"],
            },
            "port": "COM7",
            "firmware_descriptor": sample_firmware_descriptor(pid="0x5741", product="CUAV V5 RTT"),
            "want": "RED",
        },
        {
            "name": "diagnostic_wrong_dualcdc_descriptor_red",
            "usb": {
                "Verdict": "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY",
                "AllowDiagnostic5741": True,
                "MI00ComCount": 0,
                "MavlinkInterfaceStringComCount": 0,
                "Diagnostic5741MavlinkComCount": 1,
                "MissionPlannerComCandidates": ["COM7"],
                "SlcanComCandidates": [],
            },
            "port": "COM7",
            "overall": "GREEN_DIAGNOSTIC_MAVLINK",
            "mav_acceptance_mode": "DIAGNOSTIC_SINGLE_CDC_5741_MAVLINK_ONLY",
            "mav_accepted_usb_identity": "VID_1209&PID_5741",
            "mav_is_final_dual_cdc": False,
            "firmware_descriptor": sample_firmware_descriptor(),
            "want": "RED",
        },
    ]
    results: list[dict[str, Any]] = []
    ok = True
    with tempfile.TemporaryDirectory(prefix="rtt_windows_audit_selftest_") as tmp:
        base = Path(tmp)
        for case in cases:
            root = base / case["name"]
            make_sample_evidence(
                root,
                usb=case["usb"],
                port=case["port"],
                overall=case.get("overall", "GREEN_MAVLINK"),
                raw_green=case.get("raw_green", True),
                raw_suite=case.get("raw_suite", False),
                firmware_snapshot=case.get("firmware_snapshot", False),
                firmware_descriptor=case.get("firmware_descriptor"),
                mp_port=case.get("mp_port"),
                mp_param_count=case.get("mp_param_count", 945),
                mp_usb_identity=case.get("mp_usb_identity"),
                mp_connected_at=case.get("mp_connected_at"),
                mav_acceptance_mode=case.get("mav_acceptance_mode", "FINAL_DUAL_CDC_5740_MAVLINK_MI00"),
                mav_accepted_usb_identity=case.get("mav_accepted_usb_identity", REQUIRED_TARGET),
                mav_is_final_dual_cdc=case.get("mav_is_final_dual_cdc", True),
            )
            got = audit(root)
            passed = got["verdict"] == case["want"]
            ok = ok and passed
            results.append({
                "name": case["name"],
                "passed": passed,
                "got": got["verdict"],
                "want": case["want"],
                "failed_checks": [
                    item["name"] for item in got["checks"] if item["status"] != "PASS"
                ],
            })
    return {
        "verdict": "GREEN" if ok else "RED",
        "tests": results,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence_root", nargs="?", help="Windows evidence directory to audit")
    parser.add_argument("--out", help="Optional JSON output path")
    parser.add_argument("--self-test", action="store_true", help="run built-in audit rule tests and exit")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        result = self_test()
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0 if result["verdict"] == "GREEN" else 2
    if not args.evidence_root:
        raise SystemExit("evidence_root is required unless --self-test is used")
    result = audit(Path(args.evidence_root))
    text = json.dumps(result, indent=2, sort_keys=True)
    print(text)
    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text + "\n", encoding="utf-8")
    return 0 if result["verdict"] == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
