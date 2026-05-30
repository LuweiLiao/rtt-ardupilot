#!/usr/bin/env python3
"""
M7 ChibiOS vs RTT performance baseline runner (framework skeleton).

Phase 1: RTT-only measurements on CUAV v5.
Phase 2: ChibiOS A/B on the same board / wiring / SD / param set (serial hardware).

This script does NOT flash firmware, invoke OpenOCD, or modify production code.
Hardware measurement hooks are stubbed until the dedicated hardware agent runs them.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

SCHEMA_VERSION = "1.0.0"
DEFAULT_PORT = "/dev/ttyACM1"
DEFAULT_UART7 = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_0001-if00"
DEFAULT_ROUNDS = 3

# Metric id -> existing regression entrypoint (see docs/perf/chibios_vs_rtt_perf_plan.md)
METRIC_REGISTRY: Dict[str, Dict[str, Any]] = {
    "param_download_sec": {
        "kind": "duration",
        "unit": "seconds",
        "source": "tests/test_full_functional.py (T3 param_request_list)",
        "threshold": {"max_ok": 120.0},
    },
    "mavftp_pass_count": {
        "kind": "count",
        "unit": "tests",
        "source": "tests/test_mavftp.py (T1-T6 suite)",
        "threshold": {"min_ok": 6.0},
    },
    "attitude_hz": {
        "kind": "rate",
        "unit": "Hz",
        "source": "tests/test_mavlink_rates.py (ATTITUDE stream)",
        "threshold": {"min_ok": 3.0},
    },
    "raw_imu_hz": {
        "kind": "rate",
        "unit": "Hz",
        "source": "tests/test_mavlink_rates.py (RAW_IMU stream)",
        "threshold": {"min_ok": 1.0},
    },
    "sys_status_hz": {
        "kind": "rate",
        "unit": "Hz",
        "source": "tests/test_mavlink_rates.py (SYS_STATUS stream)",
        "threshold": {"min_ok": 1.0},
    },
    "heartbeat_hz": {
        "kind": "rate",
        "unit": "Hz",
        "source": "tests/test_full_functional.py (T1 heartbeat window)",
        "threshold": {"min_ok": 0.8},
    },
    "mission_protocol_ok": {
        "kind": "boolean",
        "unit": "pass",
        "source": "tests/test_mission_protocol.py",
        "threshold": {"min_ok": 1.0},
    },
    "cpu_idle_pct": {
        "kind": "idle_pct",
        "unit": "percent",
        "source": "UART7 msh: ap_rate (command-catalog.md)",
        "threshold": {"min_ok": 90.0},
    },
    "main_loop_hz": {
        "kind": "rate",
        "unit": "Hz",
        "source": "UART7 msh: ap_rate loop_hz",
        "threshold": {"min_ok": 300.0},
    },
}


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def percentile(values: List[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = (len(ordered) - 1) * (pct / 100.0)
    lo = int(math.floor(rank))
    hi = int(math.ceil(rank))
    if lo == hi:
        return ordered[lo]
    weight = rank - lo
    return ordered[lo] * (1.0 - weight) + ordered[hi] * weight


def summarize(values: List[float], failures: int, unit: str) -> Dict[str, Any]:
    if not values:
        return {
            "min": 0.0,
            "max": 0.0,
            "median": 0.0,
            "p95": 0.0,
            "failure_count": failures,
            "unit": unit,
            "samples": 0,
        }
    return {
        "min": min(values),
        "max": max(values),
        "median": percentile(values, 50),
        "p95": percentile(values, 95),
        "failure_count": failures,
        "unit": unit,
        "samples": len(values),
    }


def build_metric_stub(metric_id: str, meta: Dict[str, Any], rounds: int) -> Dict[str, Any]:
    """Scaffold metric block matching docs/perf/schema.json."""
    round_rows = [
        {"round": i, "ok": False, "value": None, "error": "not_implemented: hardware agent required"}
        for i in range(1, rounds + 1)
    ]
    return {
        "kind": meta["kind"],
        "source": meta["source"],
        "summary": summarize([], rounds, meta["unit"]),
        "rounds": round_rows,
        "threshold": meta.get("threshold"),
    }


def build_report(
    *,
    stack: str,
    phase: int,
    rounds: int,
    port: str,
    uart7: str,
    dry_run: bool,
    notes: Optional[List[str]] = None,
) -> Dict[str, Any]:
    started = utc_now()
    metrics = {
        metric_id: build_metric_stub(metric_id, meta, rounds)
        for metric_id, meta in METRIC_REGISTRY.items()
    }
    report: Dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "run": {
            "stack": stack,
            "phase": phase,
            "started_at": started,
            "finished_at": utc_now(),
            "rounds_requested": rounds,
            "dry_run": dry_run,
            "notes": notes or [],
        },
        "environment": {
            "board": "cuav_v5",
            "target": "cuav_v5",
            "cdc_port": port,
            "uart7_port": uart7,
            "firmware_ref": None,
            "param_set_id": "default_factory_or_documented_snapshot",
            "sd_card_present": None,
        },
        "metrics": metrics,
        "functional_gates": {
            "mavftp_6of6": {
                "passed": False,
                "failure_count": rounds,
                "details": "stub until hardware agent runs tests/test_mavftp.py",
            },
            "param_readable": {
                "passed": False,
                "failure_count": rounds,
                "details": "stub until hardware agent runs param download gate",
            },
        },
    }
    if phase == 2:
        report["compare"] = {}
    return report


def validate_stack(stack: str, force_chibios: bool) -> None:
    if stack == "chibios" and not force_chibios:
        print(
            "ERROR: --stack chibios is blocked by default.\n"
            "ChibiOS A/B requires:\n"
            "  1) Functional baseline B1 closed or explicitly waived\n"
            "  2) Same board / wiring / SD card / param set as RTT run\n"
            "  3) Serial execution by the hardware agent only\n"
            "Re-run with --force-chibios after manual authorization.",
            file=sys.stderr,
        )
        sys.exit(2)


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Collect ChibiOS vs RTT performance baseline metrics (framework skeleton). "
            "Does not flash firmware or call OpenOCD."
        )
    )
    parser.add_argument(
        "--stack",
        choices=["rtt", "chibios"],
        default="rtt",
        help="Firmware stack under test (default: rtt)",
    )
    parser.add_argument(
        "--port",
        default=DEFAULT_PORT,
        help=f"MAVLink CDC serial device (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--uart7",
        default=DEFAULT_UART7,
        help="UART7 debug serial by-id path for ap_rate probes",
    )
    parser.add_argument(
        "--rounds",
        type=int,
        default=DEFAULT_ROUNDS,
        help=f"Measurement rounds per metric (default: {DEFAULT_ROUNDS})",
    )
    parser.add_argument(
        "--out",
        type=Path,
        help="Write JSON report to this path (stdout if omitted)",
    )
    parser.add_argument(
        "--force-chibios",
        action="store_true",
        help="Allow --stack chibios after manual B1/functional baseline authorization",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Emit scaffold JSON only; do not open CDC or UART7",
    )
    parser.add_argument(
        "--phase",
        type=int,
        choices=[1, 2],
        default=1,
        help="Phase 1 RTT-only (default); Phase 2 paired A/B",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    if args.rounds < 1:
        print("ERROR: --rounds must be >= 1", file=sys.stderr)
        return 2

    validate_stack(args.stack, args.force_chibios)

    phase = args.phase
    if args.stack == "chibios" and phase == 1:
        phase = 2

    notes: List[str] = []
    if args.dry_run:
        notes.append("dry_run: no hardware probes executed")
    else:
        notes.append(
            "live measurement hooks not wired in skeleton; use --dry-run or wait for hardware agent"
        )

    report = build_report(
        stack=args.stack,
        phase=phase,
        rounds=args.rounds,
        port=args.port,
        uart7=args.uart7,
        dry_run=args.dry_run,
        notes=notes,
    )

    payload = json.dumps(report, indent=2, sort_keys=True)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(payload + "\n", encoding="utf-8")
        print(f"Wrote {args.out}")
    else:
        print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
