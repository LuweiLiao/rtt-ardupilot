#!/usr/bin/env python3
"""
RTT closed-loop control orchestrator (control-theory layout).

Plant:        CUAV V5 firmware (STM32F767 RT-Thread ArduPilot)
Setpoint:     main loop running + USB CDC 1209:5740 enumerated/configured
Sensors:      UART7 + OpenOCD/GDB + host lsusb (parallel agent threads)
Controllers:  10 parallel agents (C0–C9)
Actuator:     OpenOCD compile+flash (optional --auto-actuator)

Usage:
  Tools/scripts/rtt_control_loop/rtt_control_loop.py --once
  Tools/scripts/rtt_control_loop/rtt_control_loop.py --cycles 5 --auto-actuator
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from agents.orchestrator import ControlLoopEngine, one_cycle, print_cycle  # noqa: E402
from agents.plant import SETPOINT  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="RTT closed-loop control orchestrator")
    ap.add_argument("--uart-port", default="/dev/ttyACM0")
    ap.add_argument("--run-seconds", type=float, default=22.0,
                    help="free-run before OpenOCD halt (≥12 for BL 5s wait + init)")
    ap.add_argument("--cycles", type=int, default=1)
    ap.add_argument("--once", action="store_true", help="single cycle (same as --cycles 1)")
    ap.add_argument("--auto-actuator", action="store_true", help="flash when REBUILD_FLASH / dirty tree")
    ap.add_argument("--json-out", type=Path, help="write last cycle JSON")
    ap.add_argument("--no-session", action="store_true",
                    help="restart OpenOCD every cycle (slower, legacy)")
    args = ap.parse_args()
    cycles = 1 if args.once else args.cycles

    exit_code = 1
    last = None

    def run_one(i: int, engine: ControlLoopEngine | None) -> bool:
        nonlocal last, exit_code
        print(f"\n######## Cycle {i + 1}/{cycles} ########")
        if engine is not None:
            last = engine.one_cycle(auto_actuator=args.auto_actuator)
            timing = engine.last_timing
        else:
            last = one_cycle(
                uart_port=args.uart_port,
                run_seconds=args.run_seconds,
                auto_actuator=args.auto_actuator,
            )
            timing = None
        state, err, reports, flashed = last
        print_cycle(state, err, reports, timing=timing)
        if flashed:
            print("[C9] Flash completed — wait and re-sample next cycle")
        if err.get("total", 1) == 0:
            exit_code = 0
            return True
        return False

    if args.no_session:
        for i in range(cycles):
            if run_one(i, None):
                break
            if i + 1 < cycles:
                time.sleep(1)
    else:
        with ControlLoopEngine(
            uart_port=args.uart_port,
            run_seconds=args.run_seconds,
        ) as engine:
            for i in range(cycles):
                if run_one(i, engine):
                    break
                if i + 1 < cycles:
                    time.sleep(1)

    if args.json_out and last:
        state, err, reports, flashed = last
        payload = {
            "setpoint": SETPOINT,
            "state": asdict(state),
            "error": err,
            "controllers": [r.to_dict() for r in reports],
            "flashed": flashed,
        }
        args.json_out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"Wrote {args.json_out}")

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
