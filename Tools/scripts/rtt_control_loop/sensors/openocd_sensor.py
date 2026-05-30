#!/usr/bin/env python3
"""
OpenOCD feedback sensor for RTT closed-loop control.

Two-phase GDB: reset/run → Python sleep → halt/read. Keeps OpenOCD up between
phases so the target runs freely (bootloader 5s wait + app init need ≥12s).
"""
from __future__ import annotations

import re
import subprocess
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Dict, List, Optional

ROOT = Path(__file__).resolve().parents[4]
DEFAULT_ELF = ROOT / "build/rtt_deploy/cuav_v5/rt-thread.elf"
DEFAULT_CFG = ROOT / "Tools/debug/openocd-f7.cfg"
GDB_PORT = 3333

VARS = [
    "rtt_dbg_hal_run_called",
    "rtt_dbg_main_loop_entry_called",
    "rtt_dbg_main_loop_iterations",
    "rtt_dbg_usb_init",
    "rtt_dbg_usb_setup_stup",
    "rtt_dbg_usb_usbrst",
    "rtt_dbg_usb_enumdne",
    "rtt_dbg_setup_stage",
    "rtt_dbg_whoami_val",
    "rtt_dbg_spi_ok",
    "rtt_dbg_hardfault_lr",
    "rtt_dbg_hardfault_frame_sp",
    "rtt_dbg_hardfault_frame_ext",
    "rtt_dbg_hardfault_stack_lr",
    "rtt_dbg_hardfault_stack_pc",
    "rtt_dbg_hardfault_stack_xpsr",
    "rtt_dbg_loop_time_us",
    "rtt_dbg_overrun_count",
    "rtt_dbg_main_called",
]

# ARMv7-M system control block fault registers (read via OpenOCD monitor mdw).
FAULT_MDW = (
    ("vtor", 0xE000ED08),
    ("cfsr", 0xE000ED28),
    ("hfsr", 0xE000ED2C),
    ("mmfar", 0xE000ED34),
    ("bfar", 0xE000ED38),
)

HALT_READ_CMDS = (
    ["monitor halt", "info registers pc"]
    + [f"monitor mdw 0x{addr:08X} 1" for _, addr in FAULT_MDW]
    + [f"x/wx &{sym}" for sym in VARS]
    + ["p (int)usb_lld_is_configured_rtt()"]
)


@dataclass
class OpenOCDSample:
    ok: bool
    halted: bool
    pc: Optional[int]
    cfsr: Optional[int]
    vtor: Optional[int]
    configured: Optional[bool]
    vars: Dict[str, Optional[int]]
    raw_tail: str
    hfsr: Optional[int] = None
    mmfar: Optional[int] = None
    bfar: Optional[int] = None
    error: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


def _parse_xwx(output: str, sym: str) -> Optional[int]:
    pat = rf"<{re.escape(sym)}>.*?:\s*(0x[0-9a-fA-F]+)"
    m = re.search(pat, output)
    if m:
        return int(m.group(1), 16)
    return None


def _parse_pc(output: str) -> Optional[int]:
    m = re.search(r"^\s*pc\s+(0x[0-9a-fA-F]+)", output, re.I | re.M)
    if m:
        return int(m.group(1), 16)
    m = re.search(r"pc:\s*(0x[0-9a-fA-F]+)", output, re.I)
    if m:
        return int(m.group(1), 16)
    return None


def _parse_fault_regs(output: str) -> Dict[str, Optional[int]]:
    """Parse monitor mdw lines for SCB fault / vector registers."""
    out: Dict[str, Optional[int]] = {name: None for name, _ in FAULT_MDW}
    mdw_re = re.compile(
        r"0x(e000ed08|e000ed28|e000ed2c|e000ed34|e000ed38):\s*(0x)?([0-9a-fA-F]+)",
        re.I,
    )
    addr_to_name = {f"{addr:08x}": name for name, addr in FAULT_MDW}
    for m in mdw_re.finditer(output):
        name = addr_to_name.get(m.group(1).lower())
        if name is not None:
            out[name] = int(m.group(3), 16)
    return out


def parse_halt_text(text: str) -> OpenOCDSample:
    """Parse GDB batch output from HALT_READ_CMDS (shared by sample() and OpenOCDSession)."""
    vars_map: Dict[str, Optional[int]] = {}
    for sym in VARS:
        vars_map[sym] = _parse_xwx(text, sym)

    cfg_val: Optional[bool] = None
    cfg_matches = re.findall(r"\$1 = (\d+)", text)
    if cfg_matches:
        cfg_val = int(cfg_matches[-1]) != 0

    fault = _parse_fault_regs(text)
    return OpenOCDSample(
        ok=True,
        halted=True,
        pc=_parse_pc(text),
        cfsr=fault.get("cfsr"),
        vtor=fault.get("vtor"),
        configured=cfg_val,
        vars=vars_map,
        raw_tail=text[-2500:],
        hfsr=fault.get("hfsr"),
        mmfar=fault.get("mmfar"),
        bfar=fault.get("bfar"),
    )


def _wait_gdb_port(timeout_s: float = 15.0) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            import socket

            s = socket.create_connection(("127.0.0.1", GDB_PORT), timeout=0.2)
            s.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def _gdb_batch(elf: Path, cmds: List[str], timeout: int = 30) -> str:
    gdb_argv = ["arm-none-eabi-gdb", "-batch", "-ex", "set confirm off", "-ex", f"file {elf}"]
    gdb_argv.extend(["-ex", f"target extended-remote :{GDB_PORT}"])
    for c in cmds:
        gdb_argv.extend(["-ex", c])
    out = subprocess.run(
        gdb_argv,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return out.stdout + out.stderr


def sample(
    *,
    elf: Path = DEFAULT_ELF,
    cfg: Path = DEFAULT_CFG,
    run_seconds: float = 0.0,
    halt: bool = True,
    start_openocd: bool = True,
) -> OpenOCDSample:
    if not elf.is_file():
        return OpenOCDSample(False, False, None, None, None, None, {}, "", f"missing ELF {elf}")

    oc_proc: Optional[subprocess.Popen] = None
    try:
        if start_openocd:
            oc_proc = subprocess.Popen(
                ["openocd", "-f", str(cfg), "-c", "init"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if not _wait_gdb_port():
                return OpenOCDSample(
                    False, False, None, None, None, None, {}, "", "OpenOCD GDB port timeout"
                )

        # Phase 1: reset and release — target runs without GDB attached during sleep.
        _gdb_batch(elf, ["monitor reset run"], timeout=15)

        if run_seconds > 0:
            time.sleep(run_seconds)

        if not halt:
            return OpenOCDSample(True, False, None, None, None, None, {}, "", "")

        # Phase 2: halt and read plant state.
        text = _gdb_batch(elf, list(HALT_READ_CMDS), timeout=30)
        return parse_halt_text(text)
    except subprocess.TimeoutExpired:
        return OpenOCDSample(False, False, None, None, None, None, {}, "", "GDB timeout")
    except FileNotFoundError as e:
        return OpenOCDSample(False, False, None, None, None, None, {}, "", str(e))
    finally:
        if oc_proc is not None:
            oc_proc.terminate()
            try:
                oc_proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                oc_proc.kill()


if __name__ == "__main__":
    import argparse
    import json

    p = argparse.ArgumentParser(description="OpenOCD plant sensor")
    p.add_argument("--run", type=float, default=20.0, help="seconds free-run before halt")
    p.add_argument("--no-halt", action="store_true")
    args = p.parse_args()
    s = sample(run_seconds=args.run, halt=not args.no_halt)
    print(json.dumps(s.to_dict(), indent=2))
