#!/usr/bin/env python3
"""Shared Linux USB CDC port selection for RTT ArduPilot gates."""

from __future__ import annotations

import glob
import os


MAVLINK_PORT = "/dev/serial/by-id/usb-ArduPilot_*_CUAV*_*-if00"
SLCAN_PORT = "/dev/serial/by-id/usb-ArduPilot_*_CUAV*_*-if02"

EXCLUDED_PORT_NAME_FRAGMENTS = (
    "1a86_USB_Single_Serial",
    "STLink",
    "ST-LINK",
    "CUAVv5-BL",
    "-BL_",
)


def realpath_or_self(path: str) -> str:
    try:
        return os.path.realpath(path)
    except OSError:
        return path


def excluded_port_realpaths() -> set[str]:
    by_id_dir = "/dev/serial/by-id"
    excluded: set[str] = set()
    if not os.path.isdir(by_id_dir):
        return excluded
    for name in os.listdir(by_id_dir):
        if port_name_is_excluded(name):
            excluded.add(realpath_or_self(os.path.join(by_id_dir, name)))
    return excluded


def port_name_is_excluded(path: str) -> bool:
    name = os.path.basename(path).lower()
    return any(fragment.lower() in name for fragment in EXCLUDED_PORT_NAME_FRAGMENTS)


def auto_port_is_excluded(path: str, excluded_realpaths: set[str] | None = None) -> bool:
    if excluded_realpaths is None:
        excluded_realpaths = excluded_port_realpaths()
    return port_name_is_excluded(path) or realpath_or_self(path) in excluded_realpaths


def _first_existing(patterns: tuple[str, ...], excluded_realpaths: set[str]) -> str | None:
    for pattern in patterns:
        matches = [
            path for path in sorted(glob.glob(pattern))
            if not auto_port_is_excluded(path, excluded_realpaths)
        ]
        if matches:
            return matches[0]
    return None


def _ardu_cuav_by_id(interface_suffix: str, excluded_realpaths: set[str]) -> str | None:
    by_id_dir = "/dev/serial/by-id"
    if not os.path.isdir(by_id_dir):
        return None
    matches = []
    for name in os.listdir(by_id_dir):
        if not name.endswith(interface_suffix):
            continue
        lower = name.lower()
        if "ardupilot" not in lower or "cuav" not in lower:
            continue
        path = os.path.join(by_id_dir, name)
        if not auto_port_is_excluded(path, excluded_realpaths):
            matches.append(path)
    return sorted(matches)[0] if matches else None


def resolve_mavlink_port(port_arg: str, *, allow_tty_fallback: bool = True) -> str:
    """Resolve the MAVLink CDC port. Auto mode only selects if00 by-id entries."""
    if port_arg != "auto":
        return port_arg

    excluded_realpaths = excluded_port_realpaths()
    patterns = (
        "/dev/serial/by-id/usb-ArduPilot_*_CUAV*_*-if00",
        "/dev/serial/by-id/usb-ArduPilot_*_RTT5740*-if00",
        "/dev/serial/by-id/usb-ArduPilot_*_[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]*-if00",
        "/dev/serial/by-id/*VID_1209*PID_5740*if00",
        "/dev/serial/by-id/*ArduPilot*CUAV*if00",
        "/dev/serial/by-id/*CUAV*if00",
        "/dev/serial/by-id/*MAVLink*if00",
    )
    match = _first_existing(patterns, excluded_realpaths)
    if match is not None:
        return match
    match = _ardu_cuav_by_id("-if00", excluded_realpaths)
    if match is not None:
        return match

    if allow_tty_fallback:
        acms = [
            dev for dev in sorted(glob.glob("/dev/ttyACM*"))
            if not auto_port_is_excluded(dev, excluded_realpaths)
        ]
        if acms:
            return acms[0]

    raise RuntimeError("no_mavlink_cdc_if00_port")


def resolve_slcan_port(port_arg: str) -> str:
    """Resolve the board SLCAN CDC port. Auto mode only selects if02 by-id entries."""
    if port_arg != "auto":
        return port_arg

    excluded_realpaths = excluded_port_realpaths()
    patterns = (
        "/dev/serial/by-id/usb-ArduPilot_*_CUAV*_*-if02",
        "/dev/serial/by-id/usb-ArduPilot_*_RTT5740*-if02",
        "/dev/serial/by-id/usb-ArduPilot_*_[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]*-if02",
        "/dev/serial/by-id/*VID_1209*PID_5740*if02",
        "/dev/serial/by-id/*ArduPilot*CUAV*if02",
        "/dev/serial/by-id/*CUAV*if02",
        "/dev/serial/by-id/*SLCAN*if02",
    )
    match = _first_existing(patterns, excluded_realpaths)
    if match is not None:
        return match
    match = _ardu_cuav_by_id("-if02", excluded_realpaths)
    if match is not None:
        return match

    raise RuntimeError("no_slcan_cdc_if02_port")
