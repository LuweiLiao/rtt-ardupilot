#!/usr/bin/env python3
"""Collect QGC / Mission Planner host-side evidence for RTT USB debugging.

This script is intentionally read-only.  It records whether QGroundControl or
Mission Planner is running, whether their windows are visible on X11, the USB
CDC ownership state, and a small summary of likely log/config locations.  When
requested, it also captures screenshots for visible windows.
"""

from __future__ import annotations

import argparse
import glob
import importlib.util
import json
import os
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


RunCommand = Callable[[list[str], float], dict[str, Any]]

QGC_PROCESS_RE = re.compile(r"QGroundControl|qgroundcontrol", re.IGNORECASE)
MP_PROCESS_RE = re.compile(r"MissionPlanner|Mission Planner|mono.*MissionPlanner", re.IGNORECASE)
QGC_WINDOW_RE = re.compile(r"QGroundControl|QGround Control|QGC", re.IGNORECASE)
MP_WINDOW_RE = re.compile(r"MissionPlanner|Mission Planner", re.IGNORECASE)

PROCESS_QUERIES = (
    ("QGroundControl", ["pgrep", "-af", "QGroundControl|qgroundcontrol"], QGC_PROCESS_RE),
    ("MissionPlanner", ["pgrep", "-af", "MissionPlanner|Mission Planner|mono.*MissionPlanner"], MP_PROCESS_RE),
)

LOG_DIRS = (
    ("qgc_config", "~/.config/QGroundControl.org"),
    ("qgc_config_alt", "~/.config/QGroundControl"),
    ("qgc_data", "~/.local/share/QGroundControl"),
    ("qgc_logs", "~/Documents/QGroundControl/Logs"),
    ("qgc_logs_alt", "~/QGroundControl/Logs"),
    ("missionplanner_wine", "~/.wine/drive_c/Program Files (x86)/Mission Planner"),
    ("missionplanner_wine_alt", "~/.wine/drive_c/Program Files/Mission Planner"),
    ("missionplanner_documents", "~/Documents/Mission Planner"),
)


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def run_command(argv: list[str], timeout_s: float = 2.0) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            argv,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
        )
        return {
            "argv": argv,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "available": True,
        }
    except FileNotFoundError as exc:
        return {"argv": argv, "available": False, "error": f"not_found:{exc.filename or argv[0]}"}
    except subprocess.TimeoutExpired as exc:
        return {
            "argv": argv,
            "available": True,
            "timeout": True,
            "stdout": exc.stdout or "",
            "stderr": exc.stderr or "",
        }


def load_usb_diag_module() -> Any | None:
    script = Path(__file__).with_name("rtt_usb_port_conflict_diag.py")
    if not script.is_file():
        return None
    spec = importlib.util.spec_from_file_location("rtt_usb_port_conflict_diag", script)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def collect_processes(runner: RunCommand = run_command) -> list[dict[str, Any]]:
    processes: list[dict[str, Any]] = []
    for name, argv, expected_re in PROCESS_QUERIES:
        proc = runner(argv, 2.0)
        matches = []
        if proc.get("returncode") == 0:
            matches = [
                line for line in proc.get("stdout", "").splitlines()
                if expected_re.search(line) and not _is_probe_process_line(line)
            ]
        processes.append({
            "name": name,
            "argv": argv,
            "available": proc.get("available", True),
            "returncode": proc.get("returncode"),
            "matches": matches,
            "active": bool(matches),
            "error": proc.get("error"),
        })
    return processes


def _is_probe_process_line(line: str) -> bool:
    command = line.strip().split(maxsplit=1)[1] if len(line.strip().split(maxsplit=1)) == 2 else line
    executable = os.path.basename(command.split(maxsplit=1)[0]) if command.split() else ""
    if executable in ("pgrep", "rg", "grep"):
        return True
    return "rtt_qgc_mp_evidence.py" in command


def parse_wmctrl(stdout: str) -> list[dict[str, Any]]:
    windows: list[dict[str, Any]] = []
    for line in stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split(maxsplit=4)
        if len(parts) < 5:
            continue
        win_id, desktop, pid, wm_class, title = parts
        app = None
        if QGC_WINDOW_RE.search(line):
            app = "QGroundControl"
        elif MP_WINDOW_RE.search(line):
            app = "MissionPlanner"
        if app is None:
            continue
        windows.append({
            "app": app,
            "window_id": win_id,
            "desktop": desktop,
            "pid": int(pid) if pid.isdigit() else pid,
            "wm_class": wm_class,
            "title": title,
        })
    return windows


def parse_xdotool(stdout: str, app: str) -> list[dict[str, Any]]:
    windows: list[dict[str, Any]] = []
    for line in stdout.splitlines():
        text = line.strip()
        if text:
            windows.append({"app": app, "window_id_decimal": text})
    return windows


def collect_windows(runner: RunCommand = run_command) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "display": os.environ.get("DISPLAY", ""),
        "xdg_session_type": os.environ.get("XDG_SESSION_TYPE", ""),
        "wayland_display": os.environ.get("WAYLAND_DISPLAY", ""),
        "wmctrl": {"available": shutil.which("wmctrl") is not None, "windows": []},
        "xdotool": {"available": shutil.which("xdotool") is not None, "windows": []},
    }
    wm = runner(["wmctrl", "-lpx"], 2.0)
    payload["wmctrl"].update({
        "returncode": wm.get("returncode"),
        "error": wm.get("error"),
        "stderr": wm.get("stderr", ""),
        "windows": parse_wmctrl(wm.get("stdout", "")) if wm.get("available", True) else [],
    })
    for app, query in (("QGroundControl", "QGroundControl|QGround Control|QGC"),
                       ("MissionPlanner", "MissionPlanner|Mission Planner")):
        xd = runner(["xdotool", "search", "--name", query], 2.0)
        payload["xdotool"].setdefault("queries", []).append({
            "app": app,
            "returncode": xd.get("returncode"),
            "error": xd.get("error"),
            "stderr": xd.get("stderr", ""),
            "windows": parse_xdotool(xd.get("stdout", ""), app) if xd.get("available", True) else [],
        })
        payload["xdotool"]["windows"].extend(
            parse_xdotool(xd.get("stdout", ""), app) if xd.get("available", True) else []
        )
    return payload


def summarize_file(path: Path) -> dict[str, Any]:
    st = path.stat()
    return {
        "path": str(path),
        "size": st.st_size,
        "mtime_utc": datetime.fromtimestamp(st.st_mtime, timezone.utc).isoformat(),
        "suffix": path.suffix,
    }


def collect_log_locations(max_files: int = 25) -> list[dict[str, Any]]:
    locations: list[dict[str, Any]] = []
    for label, pattern in LOG_DIRS:
        expanded = os.path.expanduser(pattern)
        matches = [Path(p) for p in glob.glob(expanded)]
        for path in matches:
            item: dict[str, Any] = {
                "label": label,
                "path": str(path),
                "exists": path.exists(),
                "is_dir": path.is_dir(),
            }
            files: list[dict[str, Any]] = []
            if path.is_dir():
                candidates = [
                    p for p in path.rglob("*")
                    if p.is_file() and p.suffix.lower() in (".log", ".txt", ".ulg", ".tlog", ".ini", ".conf")
                ]
                candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
                files = [summarize_file(p) for p in candidates[:max_files]]
            elif path.is_file():
                files = [summarize_file(path)]
            item["recent_files"] = files
            locations.append(item)
    return locations


def capture_screenshots(windows: dict[str, Any], out_dir: Path, runner: RunCommand = run_command) -> list[dict[str, Any]]:
    screenshots: list[dict[str, Any]] = []
    if shutil.which("import") is None:
        return [{"available": False, "error": "not_found:import"}]
    seen: set[str] = set()
    for window in windows.get("wmctrl", {}).get("windows", []):
        win_id = str(window.get("window_id") or "")
        if not win_id or win_id in seen:
            continue
        seen.add(win_id)
        out = out_dir / f"{window.get('app', 'window')}_{win_id}.png"
        proc = runner(["import", "-window", win_id, str(out)], 5.0)
        screenshots.append({
            "app": window.get("app"),
            "window_id": win_id,
            "path": str(out),
            "returncode": proc.get("returncode"),
            "error": proc.get("error"),
            "exists": out.is_file(),
            "size": out.stat().st_size if out.is_file() else 0,
        })
    return screenshots


def collect(
    out_dir: Path | None = None,
    screenshot: bool = False,
    runner: RunCommand = run_command,
) -> dict[str, Any]:
    usb_diag = load_usb_diag_module()
    usb_payload = usb_diag.collect(runner) if usb_diag is not None else {"error": "usb_diag_unavailable"}
    processes = collect_processes(runner)
    windows = collect_windows(runner)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "host": {
            "platform": os.uname().sysname if hasattr(os, "uname") else "unknown",
            "release": os.uname().release if hasattr(os, "uname") else "unknown",
        },
        "tools": {
            "wmctrl": shutil.which("wmctrl"),
            "xdotool": shutil.which("xdotool"),
            "import": shutil.which("import"),
            "pgrep": shutil.which("pgrep"),
        },
        "processes": processes,
        "windows": windows,
        "usb": usb_payload,
        "log_locations": collect_log_locations(),
    }
    screenshots: list[dict[str, Any]] = []
    if screenshot and out_dir is not None:
        out_dir.mkdir(parents=True, exist_ok=True)
        screenshots = capture_screenshots(windows, out_dir, runner)
    payload["screenshots"] = screenshots
    payload["verdict"], payload["reasons"] = classify_payload(payload)
    return payload


def classify_payload(payload: dict[str, Any]) -> tuple[str, list[str]]:
    reasons: list[str] = []
    usb = payload.get("usb", {})
    qgc_connected = qgc_owns_rtt_mavlink_cdc(payload)
    if isinstance(usb, dict) and usb.get("verdict") == "RED" and not qgc_connected:
        reasons.append("usb_port_conflict")
    if isinstance(usb, dict) and usb.get("verdict") == "YELLOW":
        reasons.append("usb_diag_yellow")
    active_apps = [p for p in payload.get("processes", []) if p.get("active")]
    visible_windows = payload.get("windows", {}).get("wmctrl", {}).get("windows", [])
    if active_apps and not visible_windows:
        reasons.append("gcs_process_running_without_visible_x11_window")
    if not active_apps:
        reasons.append("no_qgc_or_missionplanner_process")
    if not visible_windows:
        reasons.append("no_qgc_or_missionplanner_window")
    if payload.get("windows", {}).get("wayland_display") and not payload.get("windows", {}).get("display"):
        reasons.append("wayland_without_x11_display")
    screenshots = payload.get("screenshots", [])
    failed_screenshots = [
        s for s in screenshots
        if s.get("returncode") not in (0, None) or (s.get("path") and not s.get("exists"))
    ]
    if failed_screenshots:
        reasons.append("screenshot_failed")
    if "usb_port_conflict" in reasons or "screenshot_failed" in reasons:
        return "RED", reasons
    if reasons:
        return "YELLOW", reasons
    if qgc_connected:
        return "GREEN", ["qgc_window_visible_and_owns_rtt_mavlink_cdc"]
    return "GREEN", ["gcs_window_visible_and_usb_diag_green"]


def qgc_owns_rtt_mavlink_cdc(payload: dict[str, Any]) -> bool:
    qgc_processes = [
        proc for proc in payload.get("processes", [])
        if proc.get("name") == "QGroundControl" and proc.get("active")
    ]
    qgc_windows = [
        window for window in payload.get("windows", {}).get("wmctrl", {}).get("windows", [])
        if window.get("app") == "QGroundControl"
    ]
    if not qgc_processes or not qgc_windows:
        return False

    qgc_pids = {
        int(window["pid"]) for window in qgc_windows
        if isinstance(window.get("pid"), int)
    }
    for port in payload.get("usb", {}).get("ports", []):
        if port.get("role") != "rtt_mavlink_cdc" or not port.get("owned"):
            continue
        owners = port.get("lsof", {}).get("processes", [])
        for owner in owners:
            command = str(owner.get("command", ""))
            pid = owner.get("pid")
            if command == "QGroundControl" and (not qgc_pids or pid in qgc_pids):
                return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=None, help="write JSON evidence to this path")
    parser.add_argument("--pretty", action="store_true", help="pretty-print JSON")
    parser.add_argument("--screenshot", action="store_true", help="capture visible QGC/MP windows with ImageMagick import")
    args = parser.parse_args()

    out_dir = args.out.parent if args.out else None
    payload = collect(out_dir=out_dir, screenshot=args.screenshot)
    data = json.dumps(payload, indent=2 if args.pretty else None, sort_keys=True)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(data + "\n", encoding="utf-8")
    print(data)
    return 0 if payload["verdict"] in ("GREEN", "YELLOW") else 2


if __name__ == "__main__":
    raise SystemExit(main())
