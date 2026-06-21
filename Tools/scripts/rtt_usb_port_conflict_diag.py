#!/usr/bin/env python3
"""Diagnose Linux USB CDC port ownership conflicts for RTT ArduPilot.

This host-side diagnostic is intentionally read-only.  It helps explain cases
where QGC, Mission Planner under Wine, pymavlink, slcand, ModemManager, brltty,
or a leftover OpenOCD session prevents MAVLink CDC / SLCAN CDC from opening.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


TARGET_VID = "1209"
TARGET_PID = "5740"
LEGACY_PID = "5741"
TTY_PATTERNS = ("/dev/ttyACM*", "/dev/ttyUSB*")
INTERFERER_PATTERNS = (
    ("openocd", ["pgrep", "-af", "openocd"]),
    ("ModemManager", ["pgrep", "-af", "ModemManager"]),
    ("brltty", ["pgrep", "-af", "brltty"]),
    ("QGroundControl", ["pgrep", "-af", "QGroundControl|qgroundcontrol"]),
    ("MissionPlanner", ["pgrep", "-af", "MissionPlanner|Mission Planner|mono.*MissionPlanner"]),
    ("slcand", ["pgrep", "-af", "slcand"]),
)


RunCommand = Callable[[list[str], float], dict[str, Any]]


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def run_command(argv: list[str], timeout_s: float = 2.0) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            argv,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
            check=False,
        )
        return {
            "argv": argv,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "available": True,
        }
    except FileNotFoundError as exc:
        return {"argv": argv, "available": False, "error": f"not_found:{exc.filename}"}
    except subprocess.TimeoutExpired as exc:
        return {
            "argv": argv,
            "available": True,
            "timeout": True,
            "stdout": exc.stdout or "",
            "stderr": exc.stderr or "",
        }


def readlink(path: Path) -> str | None:
    try:
        return os.readlink(path)
    except OSError:
        return None


def realpath(path: str) -> str:
    try:
        return os.path.realpath(path)
    except OSError:
        return path


def by_id_links() -> dict[str, list[str]]:
    by_id = Path("/dev/serial/by-id")
    links: dict[str, list[str]] = {}
    if not by_id.is_dir():
        return links
    for item in sorted(by_id.iterdir()):
        target = realpath(str(item))
        links.setdefault(target, []).append(str(item))
    return links


def pyserial_ports() -> dict[str, dict[str, Any]]:
    try:
        from serial.tools import list_ports
    except Exception as exc:  # noqa: BLE001
        return {"__error__": {"error": f"pyserial_unavailable:{exc!r}"}}

    ports: dict[str, dict[str, Any]] = {}
    for port in list_ports.comports():
        device = str(getattr(port, "device", "") or "")
        if not device:
            continue
        ports[device] = {
            "device": device,
            "description": str(getattr(port, "description", "") or ""),
            "hwid": str(getattr(port, "hwid", "") or ""),
            "vid": format(getattr(port, "vid", 0) or 0, "04x") if getattr(port, "vid", None) is not None else None,
            "pid": format(getattr(port, "pid", 0) or 0, "04x") if getattr(port, "pid", None) is not None else None,
            "serial_number": str(getattr(port, "serial_number", "") or ""),
            "manufacturer": str(getattr(port, "manufacturer", "") or ""),
            "product": str(getattr(port, "product", "") or ""),
            "interface": str(getattr(port, "interface", "") or ""),
            "location": str(getattr(port, "location", "") or ""),
        }
    return ports


def pyserial_port_is_relevant(device: str, info: dict[str, Any]) -> bool:
    if device.startswith("/dev/ttyACM") or device.startswith("/dev/ttyUSB"):
        return True
    vid = str(info.get("vid") or "").lower()
    pid = str(info.get("pid") or "").lower()
    return vid == TARGET_VID and pid in (TARGET_PID, LEGACY_PID)


def tty_sysfs_info(device: str) -> dict[str, Any]:
    tty = os.path.basename(realpath(device))
    sys_tty = Path("/sys/class/tty") / tty
    info: dict[str, Any] = {"tty": tty}
    dev_link = readlink(sys_tty / "device")
    if dev_link:
        dev_path = (sys_tty / "device").resolve()
        info["sysfs_device"] = str(dev_path)
        driver_link = readlink(dev_path / "driver")
        if driver_link:
            info["driver"] = os.path.basename(driver_link)
        # USB attributes usually live one or more parents above the tty node.
        for parent in [dev_path, *dev_path.parents]:
            vid_path = parent / "idVendor"
            pid_path = parent / "idProduct"
            if vid_path.is_file() and pid_path.is_file():
                info["idVendor"] = vid_path.read_text(errors="replace").strip().lower()
                info["idProduct"] = pid_path.read_text(errors="replace").strip().lower()
                product = parent / "product"
                manufacturer = parent / "manufacturer"
                serial = parent / "serial"
                if product.is_file():
                    info["usb_product"] = product.read_text(errors="replace").strip()
                if manufacturer.is_file():
                    info["usb_manufacturer"] = manufacturer.read_text(errors="replace").strip()
                if serial.is_file():
                    info["usb_serial"] = serial.read_text(errors="replace").strip()
                break
    return info


def parse_fuser_stdout(stdout: str) -> list[int]:
    pids: list[int] = []
    for token in stdout.replace("\n", " ").split():
        try:
            pids.append(int(token))
        except ValueError:
            continue
    return sorted(set(pids))


def parse_lsof_field_output(stdout: str) -> list[dict[str, Any]]:
    processes: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for line in stdout.splitlines():
        if not line:
            continue
        tag, value = line[0], line[1:]
        if tag == "p":
            if current:
                processes.append(current)
            current = {"pid": int(value) if value.isdigit() else value}
        elif current is not None:
            if tag == "c":
                current["command"] = value
            elif tag == "u":
                current["uid"] = value
            elif tag == "n":
                current["name"] = value
    if current:
        processes.append(current)
    return processes


def lsof_owners(device: str, runner: RunCommand = run_command) -> dict[str, Any]:
    if shutil.which("lsof") is None:
        return {"available": False, "reason": "lsof_not_installed", "processes": []}
    proc = runner(["lsof", "-nP", "-F", "pcun", "--", device], 2.0)
    proc["processes"] = parse_lsof_field_output(proc.get("stdout", ""))
    return proc


def fuser_owners(device: str, runner: RunCommand = run_command) -> dict[str, Any]:
    if shutil.which("fuser") is None:
        return {"available": False, "reason": "fuser_not_installed", "pids": []}
    proc = runner(["fuser", device], 2.0)
    proc["pids"] = parse_fuser_stdout(proc.get("stdout", "") + " " + proc.get("stderr", ""))
    return proc


def classify_port(port: dict[str, Any]) -> str:
    text = " ".join(
        str(port.get(key, "") or "")
        for key in (
            "device",
            "realpath",
            "by_id",
            "description",
            "hwid",
            "interface",
            "product",
            "manufacturer",
            "usb_product",
            "usb_manufacturer",
            "idVendor",
            "idProduct",
        )
    ).lower()
    vid = (port.get("vid") or port.get("idVendor") or "").lower()
    pid = (port.get("pid") or port.get("idProduct") or "").lower()
    is_target = (
        (vid == TARGET_VID and pid == TARGET_PID)
        or "vid:pid=1209:5740" in text
        or ("vid_1209" in text and "pid_5740" in text)
    )
    is_legacy = (
        (vid == TARGET_VID and pid == LEGACY_PID)
        or "vid:pid=1209:5741" in text
        or ("vid_1209" in text and "pid_5741" in text)
    )
    if is_target and ("if02" in text or "slcan" in text):
        return "rtt_slcan_cdc"
    if is_target and ("if00" in text or "mavlink" in text):
        return "rtt_mavlink_cdc"
    if is_target:
        return "rtt_unknown_interface"
    if is_legacy:
        return "rtt_legacy_5741"
    if "1a86" in text or "ch340" in text or "usb_single_serial" in text:
        return "external_usb_serial_or_can_debugger"
    if "stlink" in text or "st-link" in text:
        return "stlink_debugger"
    return "other_serial"


def collect_ports(runner: RunCommand = run_command) -> list[dict[str, Any]]:
    py_ports = pyserial_ports()
    by_id = by_id_links()
    device_names = set()
    for pattern in TTY_PATTERNS:
        device_names.update(glob.glob(pattern))
    for device, info in py_ports.items():
        if device != "__error__" and pyserial_port_is_relevant(device, info):
            device_names.add(device)
    for target in by_id:
        device_names.add(target)

    ports: list[dict[str, Any]] = []
    for device in sorted(device_names):
        resolved = realpath(device)
        py_info = py_ports.get(device) or py_ports.get(resolved) or {}
        sys_info = tty_sysfs_info(device)
        port: dict[str, Any] = {
            "device": device,
            "realpath": resolved,
            "exists": os.path.exists(device),
            "by_id": by_id.get(resolved, []),
            **py_info,
            **sys_info,
        }
        port["role"] = classify_port(port)
        port["lsof"] = lsof_owners(device, runner)
        port["fuser"] = fuser_owners(device, runner)
        port["owned"] = bool(port["lsof"].get("processes") or port["fuser"].get("pids"))
        ports.append(port)
    return ports


def collect_interferers(runner: RunCommand = run_command) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for name, argv in INTERFERER_PATTERNS:
        proc = runner(argv, 2.0)
        matches = []
        if proc.get("returncode") == 0:
            matches = [line for line in proc.get("stdout", "").splitlines() if line.strip()]
        items.append({
            "name": name,
            "argv": argv,
            "available": proc.get("available", True),
            "returncode": proc.get("returncode"),
            "matches": matches,
            "active": bool(matches),
        })
    return items


def classify_payload(payload: dict[str, Any]) -> tuple[str, list[str]]:
    reasons: list[str] = []
    ports = payload.get("ports", [])
    rtt_ports = [p for p in ports if str(p.get("role", "")).startswith("rtt_")]
    owned_rtt = [p for p in rtt_ports if p.get("owned")]
    active_interferers = [i for i in payload.get("interferers", []) if i.get("active")]
    openocd_active = [i for i in active_interferers if i.get("name") == "openocd"]

    if owned_rtt:
        reasons.append("rtt_usb_port_owned")
    if openocd_active:
        reasons.append("openocd_process_active")
    if any(i.get("name") in ("ModemManager", "brltty") for i in active_interferers):
        reasons.append("host_service_may_probe_serial")
    if not rtt_ports:
        reasons.append("no_rtt_usb_ports_found")

    missing_tools = [
        tool for tool in ("lsof", "fuser")
        if shutil.which(tool) is None
    ]
    if missing_tools:
        reasons.append("missing_owner_tools:" + ",".join(missing_tools))

    if owned_rtt or openocd_active:
        return "RED", reasons
    if reasons:
        return "YELLOW", reasons
    return "GREEN", ["rtt_usb_ports_present_and_unowned"]


def collect(runner: RunCommand = run_command) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "host": {
            "platform": os.uname().sysname if hasattr(os, "uname") else "unknown",
            "release": os.uname().release if hasattr(os, "uname") else "unknown",
        },
        "ports": collect_ports(runner),
        "interferers": collect_interferers(runner),
        "tools": {
            "lsof": shutil.which("lsof"),
            "fuser": shutil.which("fuser"),
            "pgrep": shutil.which("pgrep"),
        },
    }
    verdict, reasons = classify_payload(payload)
    payload["verdict"] = verdict
    payload["reasons"] = reasons
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=None, help="write JSON report to this path")
    parser.add_argument("--pretty", action="store_true", help="pretty-print JSON")
    args = parser.parse_args()

    payload = collect()
    data = json.dumps(payload, indent=2 if args.pretty else None, sort_keys=True)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(data + "\n", encoding="utf-8")
    print(data)
    return 0 if payload["verdict"] in ("GREEN", "YELLOW") else 2


if __name__ == "__main__":
    raise SystemExit(main())
