#!/usr/bin/env python3
"""Shared OpenOCD process guard for RTT host-side gates.

OpenOCD owns ST-Link and can perturb USB CDC timing while gates are trying to
measure MAVLink, Mission Planner, QGC, or SLCAN behavior.  Keep cleanup logic in
one place so all RTT scripts use the same exact process matching and evidence.
"""

from __future__ import annotations

import subprocess
from dataclasses import asdict, dataclass
from typing import Any
import os
import signal


PROCESS_PATTERNS = (
    ("exact_openocd", ["pkill", "-9", "-x", "openocd"]),
    ("exact_openoccd_typo", ["pkill", "-9", "-x", "openoccd"]),
    ("filtered_pattern_openocd", ["pgrep", "-af", "[o]penocd"]),
    ("filtered_pattern_openoccd_typo", ["pgrep", "-af", "[o]penoccd"]),
)


@dataclass
class GuardResult:
    name: str
    argv: list[str]
    returncode: int | None
    stdout: str
    stderr: str
    error: str | None = None


def _run(argv: list[str], timeout_s: float) -> GuardResult:
    try:
        proc = subprocess.run(
            argv,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
        )
        return GuardResult("", argv, int(proc.returncode), proc.stdout, proc.stderr)
    except FileNotFoundError as exc:
        return GuardResult("", argv, None, "", "", f"not_found:{exc.filename or argv[0]}")
    except subprocess.TimeoutExpired as exc:
        return GuardResult(
            "",
            argv,
            None,
            exc.stdout or "",
            exc.stderr or "",
            "timeout",
        )


def _looks_like_openocd_process(command: str) -> bool:
    words = command.split()
    if not words:
        return False
    executable = os.path.basename(words[0])
    if executable in ("openocd", "openoccd"):
        return True
    if executable in ("timeout", "sudo", "env") and any(
        os.path.basename(word) in ("openocd", "openoccd")
        for word in words[1:]
    ):
        return True
    return False


def _filtered_pattern_kill(argv: list[str], timeout_s: float) -> GuardResult:
    pgrep = _run(argv, timeout_s)
    if pgrep.error:
        return pgrep
    killed: list[str] = []
    skipped: list[str] = []
    self_pids = {os.getpid(), os.getppid()}
    for line in pgrep.stdout.splitlines():
        parts = line.strip().split(maxsplit=1)
        if not parts:
            continue
        try:
            pid = int(parts[0])
        except ValueError:
            skipped.append(line)
            continue
        command = parts[1] if len(parts) > 1 else ""
        if pid in self_pids or not _looks_like_openocd_process(command):
            skipped.append(line)
            continue
        try:
            os.kill(pid, signal.SIGKILL)
            killed.append(line)
        except ProcessLookupError:
            skipped.append(line + " <gone>")
        except PermissionError as exc:
            skipped.append(line + f" <permission:{exc}>")
    return GuardResult(
        "",
        argv,
        0 if killed else pgrep.returncode,
        "\n".join(killed),
        "\n".join(skipped),
        None,
    )


def cleanup_openocd(timeout_s: float = 2.0) -> list[dict[str, Any]]:
    """Kill leftover OpenOCD processes and return structured evidence.

    A returncode of 1 from pkill usually means no matching process existed; that
    is a successful no-op for this guard.
    """
    results: list[dict[str, Any]] = []
    for name, argv in PROCESS_PATTERNS:
        if name.startswith("filtered_pattern_"):
            result = _filtered_pattern_kill(argv, timeout_s)
        else:
            result = _run(argv, timeout_s)
        result.name = name
        results.append(asdict(result))
    return results


def cleanup_openocd_quiet(timeout_s: float = 2.0) -> None:
    cleanup_openocd(timeout_s=timeout_s)
