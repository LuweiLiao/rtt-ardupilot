#!/usr/bin/env python3
"""Reusable OpenOCD session — amortize startup cost across control-loop cycles."""
from __future__ import annotations

import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from sensors.openocd_sensor import (
    DEFAULT_CFG,
    DEFAULT_ELF,
    HALT_READ_CMDS,
    OpenOCDSample,
    _gdb_batch,
    _wait_gdb_port,
    parse_halt_text,
)

# Backward-compatible alias for any code importing READ_CMDS from this module.
READ_CMDS = list(HALT_READ_CMDS)


class OpenOCDSession:
    """Keep OpenOCD alive; each cycle is reset/run → sleep → halt/read only."""

    def __init__(
        self,
        *,
        elf: Path = DEFAULT_ELF,
        cfg: Path = DEFAULT_CFG,
        reuse_existing: bool = True,
    ) -> None:
        self.elf = elf
        self.cfg = cfg
        self.reuse_existing = reuse_existing
        self._proc: Optional[subprocess.Popen] = None
        self._owned = False

    def start(self) -> None:
        if not self.elf.is_file():
            raise FileNotFoundError(f"missing ELF {self.elf}")
        if self.reuse_existing and _wait_gdb_port(0.4):
            return
        self._proc = subprocess.Popen(
            ["openocd", "-f", str(self.cfg), "-c", "init"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self._owned = True
        if not _wait_gdb_port(12.0):
            self.stop()
            raise RuntimeError("OpenOCD GDB port timeout")

    def stop(self) -> None:
        if self._proc is not None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()
            self._proc = None
        self._owned = False

    def reset_run(self, run_seconds: float) -> None:
        _gdb_batch(self.elf, ["monitor reset run"], timeout=12)
        if run_seconds > 0:
            time.sleep(run_seconds)

    def halt_read(self) -> OpenOCDSample:
        text = _gdb_batch(self.elf, READ_CMDS, timeout=20)
        return parse_halt_text(text)

    def sample(self, run_seconds: float = 0.0, *, halt: bool = True) -> OpenOCDSample:
        self.reset_run(run_seconds)
        if not halt:
            return OpenOCDSample(True, False, None, None, None, None, {}, "", "")
        return self.halt_read()

    def __enter__(self) -> "OpenOCDSession":
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._owned:
            self.stop()
