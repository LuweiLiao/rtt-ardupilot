#!/usr/bin/env python3
"""Windows/Linux MAVLink acceptance gate for RTT USB CDC MI_00.

The Windows target is the ArduPilot composite CDC app identity:
USB\\VID_1209&PID_5740&MI_00.  MI_02 is SLCAN and must not be used for
Mission Planner or this MAVLink gate.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import platform
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pymavlink import mavutil

from rtt_usb_port_select import MAVLINK_PORT

DEFAULT_LINUX_PORT = MAVLINK_PORT
LINUX_PORT_PATTERNS = (
    DEFAULT_LINUX_PORT,
    "/dev/serial/by-id/usb-ArduPilot_*_RTT5740*-if00",
    "/dev/serial/by-id/*VID_1209*PID_5740*if00",
    "/dev/serial/by-id/*ArduPilot*CUAV*if00",
    "/dev/serial/by-id/*CUAV*if00",
    "/dev/serial/by-id/*MAVLink*SLCAN*if00",
    "/dev/serial/by-id/usb-ArduPilot_CUAV_V5_MAVLink_CDC_RTT5741M-if00",
    "/dev/serial/by-id/usb-ArduPilot_*_RTT5741M-if00",
)
TARGET_VID = "1209"
TARGET_PID = "5740"
LEGACY_PID = "5741"
TARGET_MI = "MI_00"
SLCAN_MI = "MI_02"
WINDOWS_FC_NAME_MARKERS = ("ARDUPILOT", "APM", "CUAV", "PX4")
MAVLINK_NAME_MARKERS = ("MAVLINK",)
SLCAN_NAME_MARKERS = ("SLCAN",)


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def find_windows_ports() -> list[dict[str, Any]]:
    ports: list[dict[str, Any]] = []

    try:
        from serial.tools import list_ports
    except Exception as exc:  # noqa: BLE001
        ports.append({
            "source": "pyserial",
            "error": f"pyserial_list_ports_unavailable:{exc!r}",
            "device": None,
            "hwid": "",
            "description": "",
        })
    else:
        for port in list_ports.comports():
            hwid = str(getattr(port, "hwid", "") or "")
            desc = str(getattr(port, "description", "") or "")
            device = str(getattr(port, "device", "") or "")
            location = str(getattr(port, "location", "") or "")
            manufacturer = str(getattr(port, "manufacturer", "") or "")
            ports.append(normalize_windows_port({
                "source": "pyserial",
                "device": device,
                "description": desc,
                "hwid": hwid,
                "pnp_device_id": hwid,
                "location": location,
                "manufacturer": manufacturer,
            }))

    ports.extend(find_windows_ports_cim())
    return dedupe_ports(ports)


def normalize_windows_port(port: dict[str, Any]) -> dict[str, Any]:
    text = " ".join(
        str(port.get(key, "") or "")
        for key in (
            "hwid",
            "pnp_device_id",
            "description",
            "device",
            "location",
            "manufacturer",
            "name",
            "bus_reported_device_desc",
            "location_info",
            "parent",
            "hardware_ids",
            "compatible_ids",
        )
    ).upper()
    is_target = (
        f"VID:PID={TARGET_VID}:{TARGET_PID}".upper() in text
        or f"VID_{TARGET_VID}&PID_{TARGET_PID}".upper() in text
    )
    is_legacy = (
        f"VID:PID={TARGET_VID}:{LEGACY_PID}".upper() in text
        or f"VID_{TARGET_VID}&PID_{LEGACY_PID}".upper() in text
    )
    port["is_target_vidpid"] = is_target
    port["is_legacy_5741"] = is_legacy
    text_says_mavlink = any(marker in text for marker in MAVLINK_NAME_MARKERS)
    text_says_slcan = any(marker in text for marker in SLCAN_NAME_MARKERS)
    port["is_mavlink_mi00"] = is_target and (TARGET_MI in text or text_says_mavlink)
    port["is_slcan_mi02"] = is_target and (SLCAN_MI in text or text_says_slcan)
    port["has_mavlink_interface_string"] = is_target and text_says_mavlink
    port["has_slcan_interface_string"] = is_target and text_says_slcan
    port["is_fc_named"] = any(marker in text for marker in WINDOWS_FC_NAME_MARKERS)
    return port


def find_windows_ports_cim() -> list[dict[str, Any]]:
    ps = r"""
function Get-DevicePropertyData {
    param(
        [string]$InstanceId,
        [string]$KeyName
    )
    try {
        $props = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop
        return $props.Data
    } catch {
        return $null
    }
}
function Get-ComName {
    param($Device)
    $name = ""
    if ($null -ne $Device.FriendlyName) {
        $name = [string]$Device.FriendlyName
    } elseif ($null -ne $Device.Name) {
        $name = [string]$Device.Name
    }
    if ($name -match "\(COM[0-9]+\)") {
        return $matches[0].Trim("(", ")")
    }
    return $null
}
function Convert-PortRecord {
    param(
        [string]$DeviceID,
        [string]$Name,
        [string]$Description,
        [string]$Manufacturer,
        [string]$PNPDeviceID,
        [string]$Status,
        [string]$Source
    )
    [pscustomobject]@{
        DeviceID = $DeviceID
        Name = $Name
        Description = $Description
        Manufacturer = $Manufacturer
        PNPDeviceID = $PNPDeviceID
        Status = $Status
        Source = $Source
        HardwareIds = @(Get-DevicePropertyData -InstanceId $PNPDeviceID -KeyName "DEVPKEY_Device_HardwareIds")
        CompatibleIds = @(Get-DevicePropertyData -InstanceId $PNPDeviceID -KeyName "DEVPKEY_Device_CompatibleIds")
        BusReportedDeviceDesc = Get-DevicePropertyData -InstanceId $PNPDeviceID -KeyName "DEVPKEY_Device_BusReportedDeviceDesc"
        LocationInfo = Get-DevicePropertyData -InstanceId $PNPDeviceID -KeyName "DEVPKEY_Device_LocationInfo"
        Parent = Get-DevicePropertyData -InstanceId $PNPDeviceID -KeyName "DEVPKEY_Device_Parent"
    }
}
$ports = @()
$ports += @(Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue | ForEach-Object {
    Convert-PortRecord -DeviceID $_.DeviceID -Name $_.Name -Description $_.Description -Manufacturer $_.Manufacturer -PNPDeviceID $_.PNPDeviceID -Status $_.Status -Source "Win32_SerialPort"
})
$ports += @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
    $_.Class -eq "Ports" -and (Get-ComName -Device $_)
} | ForEach-Object {
    $com = Get-ComName -Device $_
    Convert-PortRecord -DeviceID $com -Name $_.FriendlyName -Description (Get-DevicePropertyData -InstanceId $_.InstanceId -KeyName "DEVPKEY_Device_BusReportedDeviceDesc") -Manufacturer $_.Manufacturer -PNPDeviceID $_.InstanceId -Status $_.Status -Source "PnpDevicePorts"
})
$ports | ConvertTo-Json -Depth 8 -Compress
"""
    for exe in ("powershell", "pwsh"):
        try:
            proc = subprocess.run(
                [exe, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=10,
            )
        except FileNotFoundError:
            continue
        except Exception as exc:  # noqa: BLE001
            return [{
                "source": "cim",
                "error": f"cim_query_exception:{exc!r}",
                "device": None,
            }]

        if proc.returncode != 0:
            return [{
                "source": "cim",
                "error": f"cim_query_failed:{proc.stderr.strip()}",
                "device": None,
            }]
        raw = proc.stdout.strip()
        if not raw:
            return []
        try:
            data = json.loads(raw)
        except json.JSONDecodeError as exc:
            return [{
                "source": "cim",
                "error": f"cim_json_decode_failed:{exc!r}",
                "raw": raw[:500],
                "device": None,
            }]
        if isinstance(data, dict):
            data = [data]
        ports: list[dict[str, Any]] = []
        for item in data:
            if not isinstance(item, dict):
                continue
            ports.append(normalize_windows_port({
                "source": "cim",
                "device": str(item.get("DeviceID") or ""),
                "description": str(item.get("Description") or ""),
                "name": str(item.get("Name") or ""),
                "manufacturer": str(item.get("Manufacturer") or ""),
                "pnp_device_id": str(item.get("PNPDeviceID") or ""),
                "status": str(item.get("Status") or ""),
                "hardware_ids": item.get("HardwareIds") or [],
                "compatible_ids": item.get("CompatibleIds") or [],
                "bus_reported_device_desc": str(item.get("BusReportedDeviceDesc") or ""),
                "location_info": str(item.get("LocationInfo") or ""),
                "parent": str(item.get("Parent") or ""),
                "cim_source": str(item.get("Source") or "cim"),
            }))
        return ports
    return [{
        "source": "cim",
        "error": "powershell_not_found",
        "device": None,
    }]


def dedupe_ports(ports: list[dict[str, Any]]) -> list[dict[str, Any]]:
    merged: dict[str, dict[str, Any]] = {}
    extras: list[dict[str, Any]] = []
    for port in ports:
        device = str(port.get("device") or "")
        if not device:
            extras.append(port)
            continue
        key = device.upper()
        if key not in merged:
            merged[key] = dict(port)
            continue
        old = merged[key]
        sources = set(str(old.get("source", "")).split("+"))
        sources.add(str(port.get("source", "")))
        old["source"] = "+".join(sorted(s for s in sources if s))
        for k, v in port.items():
            if k.startswith("is_"):
                old[k] = bool(old.get(k)) or bool(v)
            elif v and not old.get(k):
                old[k] = v
    return list(merged.values()) + extras


def comparable_port_name(device: str) -> str:
    text = str(device or "").strip().upper()
    if text.startswith("\\\\.\\"):
        text = text[4:]
    return text


def find_port_by_device(ports: list[dict[str, Any]], device: str) -> dict[str, Any] | None:
    want = comparable_port_name(device)
    for port in ports:
        if comparable_port_name(str(port.get("device") or "")) == want:
            return port
    return None


def classify_manual_windows_port(
    device: str,
    ports: list[dict[str, Any]],
    *,
    allow_diagnostic_5741: bool = False,
) -> tuple[bool, str, dict[str, Any] | None]:
    """Validate a user-specified COM port against Windows USB identity data."""
    port = find_port_by_device(ports, device)
    if port is None:
        return True, "manual_port_not_listed_by_windows_allow_probe", None
    if port.get("is_slcan_mi02"):
        return False, "manual_port_is_slcan_mi02_not_mavlink", port
    if port.get("is_legacy_5741"):
        if allow_diagnostic_5741:
            return True, "manual_port_is_diagnostic_5741_single_cdc", port
        return False, "manual_port_is_legacy_5741_not_current_app", port
    if port.get("is_target_vidpid"):
        if port.get("is_mavlink_mi00"):
            return True, "manual_port_is_mavlink_mi00", port
        if port.get("is_slcan_mi02"):
            return False, "manual_port_is_target_5740_slcan_only_not_mavlink", port
        return True, "manual_port_is_target_5740_without_mi_tag_requires_raw_gate", port
    if port.get("is_fc_named"):
        return False, "manual_port_name_match_only_not_target_5740", port
    return False, "manual_port_not_target_ardupilot_usb", port


def auto_port(*, allow_diagnostic_5741: bool = False) -> tuple[str | None, list[dict[str, Any]], str]:
    system = platform.system().lower()
    if system == "windows":
        ports = find_windows_ports()
        port, reason = choose_windows_mavlink_port(ports, allow_diagnostic_5741=allow_diagnostic_5741)
        return port, ports, reason

    for pattern in LINUX_PORT_PATTERNS:
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0], [], "linux_by_id_pattern"
    return None, [], "linux_default_missing"


def choose_windows_mavlink_port(
    ports: list[dict[str, Any]],
    *,
    allow_diagnostic_5741: bool = False,
) -> tuple[str | None, str]:
    mi00 = [p for p in ports if p.get("is_mavlink_mi00") and p.get("device")]
    if mi00:
        return str(mi00[0]["device"]), "windows_mi00"

    target = [
        p for p in ports
        if p.get("is_target_vidpid") and not p.get("is_slcan_mi02") and p.get("device")
    ]
    if len(target) == 1:
        return str(target[0]["device"]), "windows_single_target_without_mi_tag_raw_gate"
    if len(target) > 1:
        return None, "windows_ambiguous_target_without_mi_tag"
    if allow_diagnostic_5741:
        diagnostic = [
            p for p in ports
            if p.get("is_legacy_5741") and p.get("device")
        ]
        if len(diagnostic) == 1:
            return str(diagnostic[0]["device"]), "windows_diagnostic_5741_single_cdc"
        if len(diagnostic) > 1:
            return None, "windows_ambiguous_diagnostic_5741_single_cdc"
    return None, "windows_no_target_mi00"


def choose_windows_slcan_port(ports: list[dict[str, Any]]) -> tuple[str | None, str]:
    mi02 = [p for p in ports if p.get("is_slcan_mi02") and p.get("device")]
    if mi02:
        return str(mi02[0]["device"]), "windows_mi02"
    target = [
        p for p in ports
        if p.get("is_target_vidpid") and p.get("device")
        and (p.get("has_slcan_interface_string") or p.get("is_fc_named"))
    ]
    if len(target) == 1:
        return str(target[0]["device"]), "windows_single_target_slcan_fallback"
    if len(target) > 1:
        return None, "windows_ambiguous_target_slcan"
    return None, "windows_no_target_mi02"


def usb_acceptance_identity(
    port: str,
    ports: list[dict[str, Any]],
    *,
    auto_reason: str,
    allow_diagnostic_5741: bool = False,
    platform_system: str | None = None,
) -> dict[str, Any]:
    """Describe which USB identity this run is proving.

    The final target is the dual-CDC app identity
    VID_1209&PID_5740&MI_00.  VID_1209&PID_5741 is accepted only when the
    explicit MAVLink-only diagnostic firmware is being used to isolate Windows
    composite CDC/MI binding from the MAVLink data path.
    """
    info = find_port_by_device(ports, port)
    is_windows = (platform_system or platform.system()).lower() == "windows"
    port_text = str(port).upper()
    if (
        allow_diagnostic_5741
        and (
            auto_reason == "windows_diagnostic_5741_single_cdc"
            or (info is not None and info.get("is_legacy_5741"))
            or "RTT5741M" in port_text
        )
    ):
        return {
            "acceptance_mode": "DIAGNOSTIC_SINGLE_CDC_5741_MAVLINK_ONLY",
            "accepted_usb_identity": f"VID_{TARGET_VID}&PID_{LEGACY_PID}",
            "final_dual_cdc_identity": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{TARGET_MI}",
            "is_final_dual_cdc_acceptance": False,
            "acceptance_note": (
                "This proves only the explicit RTT_USB_MAVLINK_ONLY diagnostic "
                "firmware. Restore the default 1209:5740 dual CDC build for "
                "final MAVLink+SLCAN acceptance."
            ),
        }
    if info is not None and info.get("is_target_vidpid") and not info.get("is_mavlink_mi00"):
        return {
            "acceptance_mode": "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE",
            "accepted_usb_identity": f"VID_{TARGET_VID}&PID_{TARGET_PID}",
            "final_dual_cdc_identity": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{TARGET_MI}",
            "is_final_dual_cdc_acceptance": False,
            "acceptance_note": (
                "Windows did not expose MI_00/MI_02 for this COM. Raw bytes, "
                "heartbeat, and fast parameter download prove this COM data "
                "path, but final acceptance still needs explicit MI_00 or an "
                "equivalent MAVLink interface identity."
            ),
        }
    if is_windows and info is None:
        return {
            "acceptance_mode": "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE",
            "accepted_usb_identity": "UNLISTED_WINDOWS_COM",
            "final_dual_cdc_identity": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{TARGET_MI}",
            "is_final_dual_cdc_acceptance": False,
            "acceptance_note": (
                "The manually selected Windows COM was not present in pyserial "
                "or CIM/PnP port enumeration. Raw bytes, heartbeat, and fast "
                "parameter download may prove a data path, but this is not a "
                "final MI_00/Mission Planner identity proof."
            ),
        }
    return {
        "acceptance_mode": "FINAL_DUAL_CDC_5740_MAVLINK_MI00",
        "accepted_usb_identity": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{TARGET_MI}",
        "final_dual_cdc_identity": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{TARGET_MI}",
        "is_final_dual_cdc_acceptance": True,
        "acceptance_note": "This run targets the default dual-CDC MAVLink interface.",
    }


def windows_probe_candidates(ports: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Return only ports worth touching during Windows diagnosis.

    We avoid probing arbitrary COM ports.  The goal is to answer the user's
    exact symptom: a visible ArduPilot/CUAV/APM COM port exists but Mission
    Planner cannot connect.
    """
    candidates: list[dict[str, Any]] = []
    seen: set[str] = set()
    for port in ports:
        device = str(port.get("device") or "")
        if not device:
            continue
        if not (
            port.get("is_target_vidpid")
            or port.get("is_legacy_5741")
            or port.get("is_fc_named")
        ):
            continue
        key = device.upper()
        if key in seen:
            continue
        seen.add(key)
        candidates.append(port)
    return candidates


def heartbeat_probe_port(device: str, args: argparse.Namespace) -> dict[str, Any]:
    result: dict[str, Any] = {
        "port": device,
        "verdict": "RED",
    }
    conn = None
    try:
        conn = mavutil.mavlink_connection(device, baud=args.baud, autoreconnect=False)
        result["heartbeat"] = wait_heartbeat(conn, args.probe_timeout)
        result["verdict"] = "GREEN"
        result["reason"] = "heartbeat"
    except Exception as exc:  # noqa: BLE001
        result["reason"] = repr(exc)
    finally:
        close_conn(conn)
    return result


def raw_mavlink_probe_port(device: str, args: argparse.Namespace, *, force_dtr_rts: bool) -> dict[str, Any]:
    """Open a serial port like a GCS and look for raw MAVLink framing bytes.

    This is intentionally a byte-level diagnostic, not an acceptance criterion.
    If Mission Planner cannot connect, this separates "no MAVLink bytes leave
    the CDC port" from "MAVLink bytes exist but the higher-level parser/GCS
    path failed".
    """
    result: dict[str, Any] = {
        "port": device,
        "duration_s": args.raw_probe_duration,
        "baud": args.baud,
        "mode": "forced_dtr_rts" if force_dtr_rts else "natural_open",
        "force_dtr_rts": force_dtr_rts,
        "verdict": "RED",
    }
    try:
        import serial
    except Exception as exc:  # noqa: BLE001
        result["reason"] = f"pyserial_unavailable:{exc!r}"
        return result

    ser = None
    try:
        ser = serial.Serial(
            device,
            args.baud,
            timeout=0,
            write_timeout=1,
            dsrdtr=False,
            rtscts=False,
            xonxoff=False,
        )
        if force_dtr_rts:
            ser.dtr = True
            ser.rts = True
        ser.reset_input_buffer()
        deadline = time.monotonic() + args.raw_probe_duration
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            waiting = int(getattr(ser, "in_waiting", 0) or 0)
            if waiting > 0:
                chunks.append(ser.read(waiting))
            else:
                time.sleep(0.01)
        data = b"".join(chunks)
        mavlink_v1 = data.count(b"\xfe")
        mavlink_v2 = data.count(b"\xfd")
        result.update({
            "bytes": len(data),
            "mavlink_v1_magic_count": mavlink_v1,
            "mavlink_v2_magic_count": mavlink_v2,
            "first_32_hex": data[:32].hex(),
        })
        if mavlink_v1 or mavlink_v2:
            result["verdict"] = "GREEN"
            result["reason"] = "mavlink_magic_seen"
        elif data:
            result["reason"] = "bytes_seen_without_mavlink_magic"
        else:
            result["reason"] = "no_bytes_seen"
    except Exception as exc:  # noqa: BLE001
        result["reason"] = repr(exc)
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
    return result


def raw_mavlink_probe_suite(device: str, args: argparse.Namespace) -> dict[str, Any]:
    """Run raw-byte probes for both natural COM opens and explicit DTR opens."""
    natural = raw_mavlink_probe_port(device, args, force_dtr_rts=False)
    forced = raw_mavlink_probe_port(device, args, force_dtr_rts=True)
    probes = [natural, forced]
    green = [p for p in probes if p.get("verdict") == "GREEN"]
    magic_count = sum(
        int(p.get("mavlink_v1_magic_count") or 0) + int(p.get("mavlink_v2_magic_count") or 0)
        for p in probes
    )
    return {
        "verdict": "GREEN" if green else "RED",
        "reason": "mavlink_magic_seen" if green else "no_mavlink_magic_seen",
        "probes": probes,
        "natural_open": natural,
        "forced_dtr_rts": forced,
        "mavlink_magic_count": magic_count,
    }


def probe_windows_named_ports(ports: list[dict[str, Any]], args: argparse.Namespace) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for port in windows_probe_candidates(ports):
        device = str(port.get("device") or "")
        probe = heartbeat_probe_port(device, args)
        probe["port_info"] = port
        results.append(probe)
    return results


def self_test_selection() -> dict[str, Any]:
    cases = [
        {
            "name": "select_explicit_mi00",
            "ports": [
                {"device": "COM7", "is_target_vidpid": True, "is_mavlink_mi00": True, "is_slcan_mi02": False},
                {"device": "COM8", "is_target_vidpid": True, "is_mavlink_mi00": False, "is_slcan_mi02": True},
            ],
            "want_port": "COM7",
            "want_reason": "windows_mi00",
        },
        {
            "name": "reject_only_slcan",
            "ports": [
                {"device": "COM8", "is_target_vidpid": True, "is_mavlink_mi00": False, "is_slcan_mi02": True},
            ],
            "want_port": None,
            "want_reason": "windows_no_target_mi00",
        },
        {
            "name": "single_target_without_mi_requires_manual_port",
            "ports": [
                {"device": "COM7", "is_target_vidpid": True, "is_mavlink_mi00": False, "is_slcan_mi02": False},
            ],
            "want_port": "COM7",
            "want_reason": "windows_single_target_without_mi_tag_raw_gate",
        },
        {
            "name": "select_mavlink_interface_string_without_mi",
            "ports": [
                normalize_windows_port({
                    "device": "COM7",
                    "pnp_device_id": "USB\\VID_1209&PID_5740\\RTT5740L",
                    "description": "ArduPilot RTT CUAV V5 MAVLink CDC",
                }),
            ],
            "want_port": "COM7",
            "want_reason": "windows_mi00",
        },
        {
            "name": "reject_slcan_interface_string_without_mi",
            "ports": [
                normalize_windows_port({
                    "device": "COM8",
                    "pnp_device_id": "USB\\VID_1209&PID_5740\\RTT5740L",
                    "description": "ArduPilot RTT CUAV V5 SLCAN CDC",
                }),
            ],
            "want_port": None,
            "want_reason": "windows_no_target_mi00",
        },
        {
            "name": "multiple_targets_without_mi_ambiguous",
            "ports": [
                {"device": "COM7", "is_target_vidpid": True, "is_mavlink_mi00": False, "is_slcan_mi02": False},
                {"device": "COM8", "is_target_vidpid": True, "is_mavlink_mi00": False, "is_slcan_mi02": False},
            ],
            "want_port": None,
            "want_reason": "windows_ambiguous_target_without_mi_tag",
        },
        {
            "name": "do_not_select_legacy_5741",
            "ports": [
                {"device": "COM4", "is_target_vidpid": False, "is_mavlink_mi00": False, "is_slcan_mi02": False, "is_legacy_5741": True},
            ],
            "want_port": None,
            "want_reason": "windows_no_target_mi00",
        },
        {
            "name": "select_diagnostic_5741_only_when_allowed",
            "ports": [
                {"device": "COM4", "is_target_vidpid": False, "is_mavlink_mi00": False, "is_slcan_mi02": False, "is_legacy_5741": True},
            ],
            "want_port": "COM4",
            "want_reason": "windows_diagnostic_5741_single_cdc",
            "allow_diagnostic_5741": True,
        },
    ]
    results: list[dict[str, Any]] = []
    ok = True
    for case in cases:
        got_port, got_reason = choose_windows_mavlink_port(
            case["ports"],
            allow_diagnostic_5741=case.get("allow_diagnostic_5741", False),
        )
        passed = got_port == case["want_port"] and got_reason == case["want_reason"]
        ok = ok and passed
        results.append({
            "name": case["name"],
            "passed": passed,
            "got_port": got_port,
            "got_reason": got_reason,
            "want_port": case["want_port"],
            "want_reason": case["want_reason"],
        })
    return {
        "verdict": "GREEN" if ok else "RED",
        "tests": results,
    }


def self_test_manual_windows_failure() -> dict[str, Any]:
    """Exercise the Windows manual-port diagnostic branch without hardware."""
    old_system = platform.system
    old_find = globals()["find_windows_ports"]
    old_mavlink_connection = mavutil.mavlink_connection
    old_probe = globals()["probe_windows_named_ports"]

    args = argparse.Namespace(
        port="COM99",
        baud=115200,
        heartbeat_timeout=0.01,
        accept_status=[3, 4, 5],
        settle=0.0,
        param_timeout=0.01,
        max_gap=0.01,
        max_total=12.0,
        min_rate=90.0,
        raw_probe=False,
        raw_probe_duration=0.01,
        probe_candidates=True,
        probe_timeout=0.01,
        allow_diagnostic_5741=False,
    )

    def fake_ports() -> list[dict[str, Any]]:
        return [
            normalize_windows_port({
                "device": "COM7",
                "pnp_device_id": "USB\\VID_1209&PID_5740&MI_00\\RTT5740L",
                "description": "MAVLink CDC",
            })
        ]

    def fake_connection(*_args: Any, **_kwargs: Any) -> Any:
        raise RuntimeError("manual_open_failed")

    def fake_probe(ports: list[dict[str, Any]], _args: argparse.Namespace) -> list[dict[str, Any]]:
        return [{"port": str(ports[0].get("device")), "verdict": "GREEN", "reason": "heartbeat"}]

    try:
        platform.system = lambda: "Windows"  # type: ignore[assignment]
        globals()["find_windows_ports"] = fake_ports
        mavutil.mavlink_connection = fake_connection
        globals()["probe_windows_named_ports"] = fake_probe
        payload = run(args)
    finally:
        platform.system = old_system  # type: ignore[assignment]
        globals()["find_windows_ports"] = old_find
        mavutil.mavlink_connection = old_mavlink_connection
        globals()["probe_windows_named_ports"] = old_probe

    passed = (
        payload.get("verdict") == "RED"
        and payload.get("auto_reason") == "manual"
        and payload.get("windows_ports")
        and payload.get("candidate_heartbeat_probes")
    )
    return {
        "verdict": "GREEN" if passed else "RED",
        "payload": payload,
    }


def self_test_manual_port_classification() -> dict[str, Any]:
    cases = [
        {
            "name": "manual_accept_mi00",
            "device": "COM7",
            "ports": [
                normalize_windows_port({
                    "device": "COM7",
                    "pnp_device_id": "USB\\VID_1209&PID_5740&MI_00\\RTT5740L",
                    "description": "MAVLink CDC",
                }),
            ],
            "want_ok": True,
            "want_reason": "manual_port_is_mavlink_mi00",
        },
        {
            "name": "manual_reject_slcan",
            "device": "COM8",
            "ports": [
                normalize_windows_port({
                    "device": "COM8",
                    "pnp_device_id": "USB\\VID_1209&PID_5740&MI_02\\RTT5740L",
                    "description": "SLCAN CDC",
                }),
            ],
            "want_ok": False,
            "want_reason": "manual_port_is_slcan_mi02_not_mavlink",
        },
        {
            "name": "manual_reject_legacy_5741",
            "device": "\\\\.\\COM4",
            "ports": [
                normalize_windows_port({
                    "device": "COM4",
                    "pnp_device_id": "USB\\VID_1209&PID_5741\\00001",
                    "description": "ArduPilot",
                }),
            ],
            "want_ok": False,
            "want_reason": "manual_port_is_legacy_5741_not_current_app",
        },
        {
            "name": "manual_accept_diagnostic_5741_when_allowed",
            "device": "\\\\.\\COM4",
            "ports": [
                normalize_windows_port({
                    "device": "COM4",
                    "pnp_device_id": "USB\\VID_1209&PID_5741\\RTT5741M",
                    "description": "CUAV V5 MAVLink CDC",
                }),
            ],
            "want_ok": True,
            "want_reason": "manual_port_is_diagnostic_5741_single_cdc",
            "allow_diagnostic_5741": True,
        },
        {
            "name": "manual_reject_name_only",
            "device": "COM9",
            "ports": [
                normalize_windows_port({
                    "device": "COM9",
                    "pnp_device_id": "USB\\VID_0483&PID_5740\\ABC",
                    "description": "ArduPilot",
                }),
            ],
            "want_ok": False,
            "want_reason": "manual_port_name_match_only_not_target_5740",
        },
        {
            "name": "manual_accept_target_without_mi_requires_raw_gate",
            "device": "COM11",
            "ports": [
                normalize_windows_port({
                    "device": "COM11",
                    "pnp_device_id": "USB\\VID_1209&PID_5740\\RTT5740L",
                    "description": "ArduPilot RTT CUAV V5",
                }),
            ],
            "want_ok": True,
            "want_reason": "manual_port_is_target_5740_without_mi_tag_requires_raw_gate",
        },
        {
            "name": "manual_allow_unlisted",
            "device": "COM10",
            "ports": [],
            "want_ok": True,
            "want_reason": "manual_port_not_listed_by_windows_allow_probe",
        },
    ]
    results: list[dict[str, Any]] = []
    ok = True
    for case in cases:
        got_ok, got_reason, got_info = classify_manual_windows_port(
            case["device"],
            case["ports"],
            allow_diagnostic_5741=case.get("allow_diagnostic_5741", False),
        )
        passed = got_ok == case["want_ok"] and got_reason == case["want_reason"]
        ok = ok and passed
        results.append({
            "name": case["name"],
            "passed": passed,
            "got_ok": got_ok,
            "got_reason": got_reason,
            "got_info": got_info,
            "want_ok": case["want_ok"],
            "want_reason": case["want_reason"],
        })
    return {
        "verdict": "GREEN" if ok else "RED",
        "tests": results,
    }


def self_test_usb_acceptance_identity() -> dict[str, Any]:
    cases = [
        {
            "name": "windows_unlisted_manual_is_raw_gate_only",
            "port": "COM10",
            "ports": [],
            "auto_reason": "manual",
            "platform_system": "windows",
            "want_mode": "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE",
            "want_final": False,
        },
        {
            "name": "windows_mi00_is_final",
            "port": "COM7",
            "ports": [
                normalize_windows_port({
                    "device": "COM7",
                    "pnp_device_id": "USB\\VID_1209&PID_5740&MI_00\\RTT5740N",
                    "description": "MAVLink CDC",
                }),
            ],
            "auto_reason": "windows_mi00",
            "platform_system": "windows",
            "want_mode": "FINAL_DUAL_CDC_5740_MAVLINK_MI00",
            "want_final": True,
        },
        {
            "name": "windows_no_mi_target_is_raw_gate_only",
            "port": "COM11",
            "ports": [
                normalize_windows_port({
                    "device": "COM11",
                    "pnp_device_id": "USB\\VID_1209&PID_5740\\RTT5740N",
                    "description": "ArduPilot RTT CUAV V5",
                }),
            ],
            "auto_reason": "windows_single_target_without_mi_tag_raw_gate",
            "platform_system": "windows",
            "want_mode": "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE",
            "want_final": False,
        },
        {
            "name": "linux_by_id_remains_final_local_gate",
            "port": "/dev/serial/by-id/usb-ArduPilot_CUAV_V5_RTT_RTT5740N-if00",
            "ports": [],
            "auto_reason": "linux_by_id_pattern",
            "platform_system": "linux",
            "want_mode": "FINAL_DUAL_CDC_5740_MAVLINK_MI00",
            "want_final": True,
        },
    ]
    results: list[dict[str, Any]] = []
    ok = True
    for case in cases:
        got = usb_acceptance_identity(
            case["port"],
            case["ports"],
            auto_reason=case["auto_reason"],
            platform_system=case["platform_system"],
        )
        passed = (
            got.get("acceptance_mode") == case["want_mode"]
            and bool(got.get("is_final_dual_cdc_acceptance")) == case["want_final"]
        )
        ok = ok and passed
        results.append({
            "name": case["name"],
            "passed": passed,
            "got_mode": got.get("acceptance_mode"),
            "got_final": got.get("is_final_dual_cdc_acceptance"),
            "want_mode": case["want_mode"],
            "want_final": case["want_final"],
        })
    return {
        "verdict": "GREEN" if ok else "RED",
        "tests": results,
    }


def self_test_all() -> dict[str, Any]:
    tests = {
        "selection": self_test_selection(),
        "manual_failure": self_test_manual_windows_failure(),
        "manual_port_classification": self_test_manual_port_classification(),
        "usb_acceptance_identity": self_test_usb_acceptance_identity(),
    }
    ok = all(item.get("verdict") == "GREEN" for item in tests.values())
    return {
        "verdict": "GREEN" if ok else "RED",
        "tests": tests,
    }


def close_conn(conn: Any | None) -> None:
    if conn is None:
        return
    try:
        conn.close()
    except Exception:
        pass


def wait_heartbeat(conn: Any, timeout_s: float) -> dict[str, Any]:
    start = time.monotonic()
    msg = conn.wait_heartbeat(timeout=timeout_s)
    if msg is None:
        raise RuntimeError("heartbeat_timeout")
    return {
        "elapsed_s": round(time.monotonic() - start, 3),
        "type": int(msg.type),
        "autopilot": int(msg.autopilot),
        "system_status": int(msg.system_status),
        "target_system": int(conn.target_system),
        "target_component": int(conn.target_component),
    }


def drain(conn: Any, duration_s: float) -> None:
    deadline = time.monotonic() + duration_s
    while time.monotonic() < deadline:
        conn.recv_match(blocking=True, timeout=0.1)


def download_params(conn: Any, args: argparse.Namespace) -> dict[str, Any]:
    start = time.monotonic()
    first = None
    last_msg = None
    indices: set[int] = set()
    names: set[str] = set()
    duplicate_indices = 0
    invalid_indices = 0
    gaps: list[float] = []
    last_rx = start
    reported_count = None

    conn.mav.param_request_list_send(conn.target_system, conn.target_component)
    deadline = start + args.param_timeout

    while time.monotonic() < deadline:
        msg = conn.recv_match(type=["PARAM_VALUE", "HEARTBEAT"], blocking=True, timeout=0.5)
        now = time.monotonic()
        if msg is None:
            if now - last_rx > args.max_gap:
                break
            continue

        if msg.get_type() == "HEARTBEAT":
            continue

        gap = now - last_rx
        gaps.append(gap)
        last_rx = now
        if first is None:
            first = now

        idx = int(msg.param_index)
        count = int(msg.param_count)
        reported_count = count
        param_id = str(msg.param_id).rstrip("\x00")
        last_msg = {
            "param_id": param_id,
            "param_index": idx,
            "param_count": count,
        }

        if idx < 0 or idx >= count:
            invalid_indices += 1
            continue
        if idx in indices:
            duplicate_indices += 1
        indices.add(idx)
        names.add(param_id)
        if reported_count is not None and len(indices) >= reported_count:
            break

    elapsed = time.monotonic() - start
    missing: list[int] = []
    if reported_count is not None:
        missing = [i for i in range(reported_count) if i not in indices]
    gaps_sorted = sorted(gaps)
    p95_gap = None
    if gaps_sorted:
        p95_gap = gaps_sorted[min(len(gaps_sorted) - 1, int(len(gaps_sorted) * 0.95))]

    complete = reported_count is not None and len(indices) == reported_count and not missing
    fast = elapsed <= args.max_total and (len(indices) / max(elapsed, 0.001)) >= args.min_rate
    verdict = "GREEN" if complete and fast else "RED"
    reason = "complete_fast" if verdict == "GREEN" else (
        f"complete={complete},fast={fast},reported_count={reported_count},unique={len(indices)}"
    )
    return {
        "verdict": verdict,
        "reason": reason,
        "reported_count": reported_count,
        "unique_indices": len(indices),
        "unique_names": len(names),
        "missing_count": len(missing),
        "missing_first_indices": missing[:20],
        "duplicate_indices": duplicate_indices,
        "invalid_indices": invalid_indices,
        "elapsed_s": round(elapsed, 3),
        "rate_params_s": round(len(indices) / max(elapsed, 0.001), 1),
        "first_response_latency_s": None if first is None else round(first - start, 3),
        "max_gap_s_observed": None if not gaps else round(max(gaps), 3),
        "p95_gap_s": None if p95_gap is None else round(p95_gap, 3),
        "last_msg": last_msg,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    system = platform.system().lower()
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "platform": platform.platform(),
        "requested_port": args.port,
        "baud": args.baud,
        "target_usb": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{TARGET_MI}",
        "slcan_usb": f"VID_{TARGET_VID}&PID_{TARGET_PID}&{SLCAN_MI}",
        "diagnostic_5741_allowed": bool(args.allow_diagnostic_5741),
        "verdict": "RED",
    }

    if args.port == "auto":
        port, ports, auto_reason = auto_port(allow_diagnostic_5741=args.allow_diagnostic_5741)
        payload["auto_reason"] = auto_reason
        payload["windows_ports"] = ports
        if auto_reason == "windows_single_target_without_mi_tag_raw_gate":
            payload["auto_warning"] = (
                "Windows exposes exactly one current VID/PID COM port but does "
                "not expose MI_00/MI_02 identity. This run will probe that "
                "single port as a data-path diagnostic. It is not final dual-CDC "
                "acceptance unless Windows also reports MI_00 or an equivalent "
                "MAVLink interface identity."
            )
        if port is None:
            if system == "windows" and args.probe_candidates:
                payload["candidate_heartbeat_probes"] = probe_windows_named_ports(ports, args)
            if auto_reason == "windows_single_target_without_mi_tag_raw_gate":
                payload["reason"] = "single_1209_5740_com_without_mi_tag_use_manual_port_and_raw_gate"
            elif auto_reason == "windows_ambiguous_target_without_mi_tag":
                payload["reason"] = "multiple_1209_5740_com_ports_without_mi_tag_use_manual_port"
            else:
                payload["reason"] = "mavlink_mi00_port_not_found"
            if auto_reason == "windows_ambiguous_diagnostic_5741_single_cdc":
                payload["reason"] = "multiple_1209_5741_diagnostic_com_ports_use_manual_port"
            return payload
    else:
        port = args.port
        payload["auto_reason"] = "manual"
        ports = find_windows_ports() if system == "windows" else []
        payload["windows_ports"] = ports
        if system == "windows":
            manual_ok, manual_reason, manual_info = classify_manual_windows_port(
                port,
                ports,
                allow_diagnostic_5741=args.allow_diagnostic_5741,
            )
            payload["manual_port_check"] = {
                "verdict": "GREEN" if manual_ok else "RED",
                "reason": manual_reason,
                "port_info": manual_info,
            }
            if not manual_ok:
                payload["reason"] = manual_reason
                if args.probe_candidates:
                    payload["candidate_heartbeat_probes"] = probe_windows_named_ports(ports, args)
                return payload

    payload["port"] = port
    payload.update(usb_acceptance_identity(
        port,
        ports,
        auto_reason=str(payload.get("auto_reason") or ""),
        allow_diagnostic_5741=args.allow_diagnostic_5741,
        platform_system=system,
    ))
    if SLCAN_MI in str(port).upper():
        payload["reason"] = "refusing_slcan_mi02_for_mavlink"
        if system == "windows" and args.probe_candidates:
            payload["candidate_heartbeat_probes"] = probe_windows_named_ports(ports, args)
        return payload

    conn = None
    try:
        if args.raw_probe:
            payload["raw_mavlink_probe"] = raw_mavlink_probe_suite(port, args)
        conn = mavutil.mavlink_connection(port, baud=args.baud, autoreconnect=False)
        payload["heartbeat"] = wait_heartbeat(conn, args.heartbeat_timeout)
        if payload["heartbeat"]["system_status"] not in args.accept_status:
            payload["reason"] = "heartbeat_status_not_accepted"
            return payload
        drain(conn, args.settle)
        payload["param_download"] = download_params(conn, args)
        payload["verdict"] = payload["param_download"]["verdict"]
        payload["reason"] = payload["param_download"]["reason"]
        return payload
    except Exception as exc:  # noqa: BLE001
        payload["reason"] = repr(exc)
        if system == "windows" and args.probe_candidates:
            payload["candidate_heartbeat_probes"] = probe_windows_named_ports(ports, args)
        return payload
    finally:
        close_conn(conn)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto", help="COMx, Linux device path, or auto")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--outdir", default="rtt_windows_mavlink_evidence")
    parser.add_argument("--heartbeat-timeout", type=float, default=15.0)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--param-timeout", type=float, default=45.0)
    parser.add_argument("--max-gap", type=float, default=1.0)
    parser.add_argument("--max-total", type=float, default=12.0)
    parser.add_argument("--min-rate", type=float, default=90.0)
    parser.add_argument("--accept-status", type=int, action="append", default=[3, 4, 5])
    parser.add_argument("--probe-timeout", type=float, default=3.0,
                        help="per-port heartbeat timeout for Windows named-port diagnosis")
    parser.add_argument("--raw-probe-duration", type=float, default=2.0,
                        help="seconds to read raw bytes before pymavlink connects")
    parser.add_argument("--no-raw-probe", dest="raw_probe", action="store_false",
                        help="skip the raw MAVLink byte probe")
    parser.add_argument("--no-probe-candidates", dest="probe_candidates", action="store_false",
                        help="do not probe ArduPilot/CUAV/APM named Windows COM ports when auto fails")
    parser.add_argument("--allow-diagnostic-5741", action="store_true",
                        help=("allow the explicit RTT_USB_MAVLINK_ONLY diagnostic firmware "
                              "identity VID_1209&PID_5741 as a MAVLink port; do not use for "
                              "final dual-CDC acceptance"))
    parser.set_defaults(probe_candidates=True, raw_probe=True)
    parser.add_argument("--self-test-selection", action="store_true",
                        help="run built-in Windows COM selection tests and exit")
    parser.add_argument("--self-test", action="store_true",
                        help="run all built-in Windows MAVLink acceptance rule tests and exit")
    parser.add_argument("--self-test-manual-failure", action="store_true",
                        help="run built-in Windows manual-port failure diagnostic test and exit")
    parser.add_argument("--self-test-manual-port-classification", action="store_true",
                        help="run built-in Windows manual COM identity classification tests and exit")
    parser.add_argument("--self-test-usb-acceptance-identity", action="store_true",
                        help="run USB identity acceptance-mode tests and exit")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        payload = self_test_all()
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0 if payload.get("verdict") == "GREEN" else 2
    if args.self_test_selection:
        payload = self_test_selection()
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0 if payload.get("verdict") == "GREEN" else 2
    if args.self_test_manual_failure:
        payload = self_test_manual_windows_failure()
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0 if payload.get("verdict") == "GREEN" else 2
    if args.self_test_manual_port_classification:
        payload = self_test_manual_port_classification()
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0 if payload.get("verdict") == "GREEN" else 2
    if args.self_test_usb_acceptance_identity:
        payload = self_test_usb_acceptance_identity()
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0 if payload.get("verdict") == "GREEN" else 2

    outdir = Path(args.outdir) / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    outdir.mkdir(parents=True, exist_ok=True)
    payload = run(args)
    payload["json_path"] = str(outdir / "mavlink_acceptance.json")
    with (outdir / "mavlink_acceptance.json").open("w", encoding="utf-8") as fp:
        json.dump(payload, fp, indent=2, sort_keys=True)
        fp.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    print(f"Evidence directory: {outdir}")
    return 0 if payload.get("verdict") == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
