#!/usr/bin/env python3
"""Read RTT CherryUSB debug counters through OpenOCD.

This is a focused post-failure snapshot for Windows/Mission Planner USB CDC
debugging.  It halts briefly, reads the symbols by address, resumes the target,
and writes JSON evidence.  Run it immediately after a failed COM/Mission Planner
attempt to see whether Windows reached CDC class requests, DTR, OUT data, and
IN completions on the firmware side.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_ELF = "build/rtt_deploy/cuav_v5/rt-thread.elf"
DEFAULT_CFG = ("interface/stlink.cfg", "target/stm32f7x.cfg")

SYMBOLS = (
    "rtt_dbg_usb_init",
    "rtt_dbg_usb_usbrst",
    "rtt_dbg_usb_enumdne",
    "rtt_dbg_usb_setup_stup",
    "rtt_dbg_cherry_configured_state",
    "rtt_dbg_cherry_suspended_state",
    "rtt_dbg_cherry_last_event",
    "rtt_dbg_cherry_suspend_count",
    "rtt_dbg_cherry_resume_count",
    "rtt_dbg_cherry_disconnect_count",
    "rtt_dbg_cherry_connect_count",
    "rtt_dbg_cherry_usb_reset",
    "rtt_dbg_usbd_set_interface_calls",
    "rtt_dbg_usbd_set_interface_same_alt_ack",
    "rtt_dbg_usbd_set_interface_reconfigure",
    "rtt_dbg_usbd_set_interface_last_intf",
    "rtt_dbg_usbd_set_interface_last_alt",
    "rtt_dbg_usbd_set_interface_last_prev_alt",
    "rtt_dbg_cherry_get_line_coding_calls",
    "rtt_dbg_cherry_set_line_coding_calls",
    "rtt_dbg_cherry_last_line_intf",
    "rtt_dbg_cherry_last_line_baud",
    "rtt_dbg_cherry_last_line_format",
    "rtt_dbg_cherry_set_dtr_calls",
    "rtt_dbg_cherry_set_rts_calls",
    "rtt_dbg_cherry_dtr_state",
    "rtt_dbg_cherry_rts_state",
    "rtt_dbg_cherry_host_open_hint_state",
    "rtt_dbg_cherry_host_open_hint_set_count",
    "rtt_dbg_cherry_host_open_hint_clear_count",
    "rtt_dbg_cherry_host_open_hint_clear_reason",
    "rtt_dbg_cherry2_dtr_state",
    "rtt_dbg_cherry2_rts_state",
    "rtt_dbg_cherry_dtr_open_count",
    "rtt_dbg_cherry_dtr_close_count",
    "rtt_dbg_cherry_last_dtr_intf",
    "rtt_dbg_cherry_last_rts_intf",
    "rtt_dbg_cherry_last_dtr_open_ms",
    "rtt_dbg_cherry_last_dtr_close_ms",
    "rtt_dbg_cherry_dtr_open_kicks",
    "rtt_dbg_cherry_bulk_out_calls",
    "rtt_dbg_cherry_bulk_out_bytes",
    "rtt_dbg_cherry_last_out_ms",
    "rtt_dbg_cherry_last_out_ep",
    "rtt_dbg_cherry_last_out_len",
    "rtt_dbg_cherry_last_out_w0",
    "rtt_dbg_cherry_last_out_w1",
    "rtt_dbg_cherry_rx_enqueued",
    "rtt_dbg_cherry_rx_dropped",
    "rtt_dbg_cherry_rx_drained",
    "rtt_dbg_cherry_bulk_in_calls",
    "rtt_dbg_cherry_last_bulk_in_ms",
    "rtt_dbg_cherry_tx_start_ok",
    "rtt_dbg_cherry_tx_start_fail",
    "rtt_dbg_cherry_tx_busy_state",
    "rtt_dbg_cherry_tx_busy_max_ms",
    "rtt_dbg_cherry_tx_ring_enqueued",
    "rtt_dbg_cherry_tx_ring_dropped",
    "rtt_dbg_cherry_tx_ring_high_water",
    "rtt_dbg_cherry_tx_arm_bytes",
    "rtt_dbg_cherry_tx_complete_bytes",
    "rtt_dbg_cherry_tx_last_arm_len",
    "rtt_dbg_cherry_tx_last_complete_len",
    "rtt_dbg_cherry_tx_zlp_armed",
    "rtt_dbg_cherry_tx_zlp_complete",
    "rtt_dbg_cherry_tx_zlp_start_fail",
    "rtt_dbg_cherry_epena_guard_hits",
    "rtt_dbg_cherry_epdis_recovery_count",
    "rtt_dbg_cherry_recovery_busy_timeout_count",
    "rtt_dbg_cherry_recovery_epena_stuck_count",
    "rtt_dbg_cherry_recovery_last_reason",
    "rtt_dbg_cherry_recovery_last_elapsed_ms",
    "rtt_dbg_cherry_recovery_last_diepctl",
    "rtt_dbg_cherry_recovery_last_diepint",
    "rtt_dbg_cherry_recovery_last_dieptsiz",
    "rtt_dbg_cherry_recovery_last_dtxfsts",
    "rtt_dbg_cherry_recovery_last_empmsk",
    "rtt_dbg_cherry_recovery_last_dctl",
    "rtt_dbg_cherry_recovery_last_dsts",
    "rtt_dbg_cherry_recovery_last_gintsts",
    "rtt_dbg_cherry_recovery_last_gintmsk",
    "rtt_dbg_cherry_recovery_last_daint",
    "rtt_dbg_cherry_recovery_last_ring_count",
    "rtt_dbg_cherry_recovery_last_bulk_arm",
    "rtt_dbg_cherry_recovery_last_bulk_now",
    "rtt_dbg_cherry_tx_inflight_slots_state",
    "rtt_dbg_cherry_tx_complete_discards",
    "rtt_dbg_cherry_tx_completion_assumed",
    "rtt_dbg_cherry_tx_completion_assumed_slots",
    "rtt_dbg_cherry_tx_completion_replayed",
    "rtt_dbg_cherry_tx_uncertain_drop_after_recovery",
    "rtt_dbg_cherry_epena_guard_last_ring_count",
    "rtt_dbg_cherry_epena_guard_last_diepctl",
    "rtt_dbg_cherry_epena_guard_last_diepint",
    "rtt_dbg_cherry_epena_guard_last_dieptsiz",
    "rtt_dbg_cherry_epena_guard_last_dtxfsts",
    "rtt_dbg_cherry_epena_guard_last_dtr",
    "rtt_dbg_cherry_epena_guard_last_busy",
    "rtt_dbg_dwc2_irq_calls",
    "rtt_dbg_dwc2_gint_last",
    "rtt_dbg_dwc2_gint_iepint",
    "rtt_dbg_dwc2_iep_intr_last",
    "rtt_dbg_dwc2_ep1_seen",
    "rtt_dbg_dwc2_ep1_raw",
    "rtt_dbg_dwc2_ep1_masked",
    "rtt_dbg_dwc2_ep1_msk",
    "rtt_dbg_dwc2_ep1_empmsk",
    "rtt_dbg_dwc2_ep1_xfrc",
    "rtt_dbg_dwc2_ep1_xfrc_incomplete_ignored",
    "rtt_dbg_dwc2_ep1_xfrc_incomplete_dieptsiz",
    "rtt_dbg_dwc2_ep1_xfrc_incomplete_actual",
    "rtt_dbg_dwc2_ep1_xfrc_incomplete_xfer_len",
    "rtt_dbg_dwc2_ep1_xfrc_incomplete_txfe_rearmed",
    "rtt_dbg_dwc2_ep1_xfrc_deferred_until_drained",
    "rtt_dbg_dwc2_ep1_xfrc_complete_after_fifo_load_with_residue",
    "rtt_dbg_dwc2_ep1_xfrc_complete_after_fifo_load_dieptsiz",
    "rtt_dbg_dwc2_ep1_txfe",
    "rtt_dbg_dwc2_ep1_epdisd",
    "rtt_dbg_dwc2_ep1_txfe_process",
    "rtt_dbg_dwc2_ep1_complete_calls",
    "rtt_dbg_dwc2_ep1_start_write_calls",
    "rtt_dbg_dwc2_ep1_start_len_last",
    "rtt_dbg_dwc2_ep1_dieptsiz_last",
    "rtt_dbg_dwc2_ep1_diepctl_last",
    "rtt_dbg_dwc2_ep1_daintmsk_last",
    "rtt_dbg_dwc2_ep1_diepmsk_last",
    "rtt_dbg_dwc2_ep1_gintmsk_last",
    "rtt_dbg_dwc2_ep1_actual_last",
    "rtt_dbg_dwc2_ep1_txfe_zero_remaining",
    "rtt_dbg_dwc2_ep1_txfe_no_fifo_space",
    "rtt_dbg_dwc2_ep1_txfe_write_loops",
    "rtt_dbg_dwc2_ep1_txfe_wrote_bytes",
    "rtt_dbg_dwc2_ep1_txfe_enter_xfer_len",
    "rtt_dbg_dwc2_ep1_txfe_enter_actual",
    "rtt_dbg_dwc2_ep1_txfe_enter_len",
    "rtt_dbg_dwc2_ep1_txfe_enter_len32b",
    "rtt_dbg_dwc2_ep1_txfe_enter_dtxfsts",
    "rtt_dbg_dwc2_ep1_txfe_enter_dieptsiz",
    "rtt_dbg_dwc2_ep1_txfe_enter_diepctl",
    "rtt_dbg_dwc2_ep1_txfe_enter_diepint",
    "rtt_dbg_dwc2_ep1_txfe_post_dtxfsts",
    "rtt_dbg_dwc2_ep1_txfe_post_dieptsiz",
    "rtt_dbg_dwc2_ep1_txfe_post_diepctl",
    "rtt_dbg_dwc2_ep1_txfe_post_diepint",
    "rtt_dbg_dwc2_ep1_txfe_post_actual",
    "rtt_dbg_dwc2_ep1_txfe_mask_clears",
    "rtt_dbg_dwc2_ep1_irq_xfrc_txfe_same",
    "rtt_dbg_dwc2_ep1_txfe_before_xfrc",
    "rtt_dbg_dwc2_ep1_immediate_prime_calls",
    "rtt_dbg_dwc2_ep1_immediate_prime_wrote",
    "rtt_dbg_dwc2_ep1_immediate_prime_zero",
    "rtt_dbg_dwc2_ep1_immediate_prime_bytes",
    "rtt_dbg_dwc2_ep1_last_start_ms",
    "rtt_dbg_dwc2_ep1_last_txfe_ms",
    "rtt_dbg_dwc2_ep1_last_xfrc_ms",
    "rtt_dbg_dwc2_ep1_start_gap_last_ms",
    "rtt_dbg_dwc2_ep1_start_gap_max_ms",
    "rtt_dbg_dwc2_ep1_start_to_txfe_last_ms",
    "rtt_dbg_dwc2_ep1_start_to_txfe_max_ms",
    "rtt_dbg_dwc2_ep1_start_to_xfrc_last_ms",
    "rtt_dbg_dwc2_ep1_start_to_xfrc_max_ms",
    "rtt_dbg_dwc2_ep1_txfe_to_xfrc_last_ms",
    "rtt_dbg_dwc2_ep1_txfe_to_xfrc_max_ms",
    "rtt_dbg_dwc2_ep1_xfrc_to_start_last_ms",
    "rtt_dbg_dwc2_ep1_xfrc_to_start_max_ms",
)

FAULT_REGS = {
    "CFSR": 0xE000ED28,
    "HFSR": 0xE000ED2C,
    "DFSR": 0xE000ED30,
    "AFSR": 0xE000ED3C,
    "VTOR": 0xE000ED08,
}


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def cleanup_openocd() -> None:
    subprocess.run(["pkill", "-9", "-x", "openocd"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "[o]penoccd"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def symbol_addresses(elf: Path, nm: str) -> dict[str, int]:
    proc = subprocess.run([nm, "-g", str(elf)], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    addresses: dict[str, int] = {}
    wanted = set(SYMBOLS)
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        name = parts[-1]
        if name in wanted:
            addresses[name] = int(parts[0], 16)
    return addresses


def parse_openocd(output: str, addrs: dict[str, int]) -> tuple[dict[str, int], dict[str, int], dict[str, int | None]]:
    addr_to_symbol = {addr: name for name, addr in addrs.items()}
    addr_to_fault = {addr: name for name, addr in FAULT_REGS.items()}
    values: dict[str, int] = {}
    faults: dict[str, int] = {}
    registers: dict[str, int | None] = {"pc": None, "msp": None, "psp": None}
    mdw_re = re.compile(r"0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})")
    reg_re = re.compile(r"^(pc|msp|psp)\s+\(/32\):\s+0x([0-9a-fA-F]+)", re.MULTILINE)
    for match in mdw_re.finditer(output):
        addr = int(match.group(1), 16)
        value = int(match.group(2), 16)
        if addr in addr_to_symbol:
            values[addr_to_symbol[addr]] = value
        if addr in addr_to_fault:
            faults[addr_to_fault[addr]] = value
    for match in reg_re.finditer(output):
        registers[match.group(1)] = int(match.group(2), 16)
    return values, faults, registers


def classify(values: dict[str, int], faults: dict[str, int]) -> dict[str, Any]:
    setup = values.get("rtt_dbg_usb_setup_stup", 0)
    set_interface = values.get("rtt_dbg_usbd_set_interface_calls", 0)
    set_interface_same_alt = values.get("rtt_dbg_usbd_set_interface_same_alt_ack", 0)
    get_line_coding = values.get("rtt_dbg_cherry_get_line_coding_calls", 0)
    set_line_coding = values.get("rtt_dbg_cherry_set_line_coding_calls", 0)
    dtr_calls = values.get("rtt_dbg_cherry_set_dtr_calls", 0)
    rts_calls = values.get("rtt_dbg_cherry_set_rts_calls", 0)
    dtr_state = values.get("rtt_dbg_cherry_dtr_state", 0)
    rts_state = values.get("rtt_dbg_cherry_rts_state", 0)
    host_open_hint = values.get("rtt_dbg_cherry_host_open_hint_state", 0)
    out_calls = values.get("rtt_dbg_cherry_bulk_out_calls", 0)
    in_calls = values.get("rtt_dbg_cherry_bulk_in_calls", 0)
    tx_start = values.get("rtt_dbg_cherry_tx_start_ok", 0)
    cfsr = faults.get("CFSR", 0)
    hfsr = faults.get("HFSR", 0)
    if cfsr or hfsr:
        verdict = "RED_FAULT"
        reason = "fault_register_nonzero"
    elif setup == 0:
        verdict = "RED_NO_CDC_CLASS_REQUESTS"
        reason = "host_has_not_completed_cdc_class_handshake"
    elif set_interface > 0 and setup == 0:
        verdict = "YELLOW_INTERFACE_ONLY_NO_CDC_CLASS"
        reason = "host_sent_standard_interface_requests_but_no_cdc_class_requests"
    elif get_line_coding == 0 and set_line_coding == 0 and dtr_calls == 0 and rts_calls == 0:
        verdict = "YELLOW_NO_CDC_OPEN_REQUESTS"
        reason = "host_configured_usb_but_did_not_query_line_coding_or_control_lines"
    elif dtr_calls == 0 and rts_calls == 0 and host_open_hint == 0:
        verdict = "YELLOW_NO_CONTROL_LINES_OR_OPEN_HINT"
        reason = "cdc_class_requests_seen_but_no_dtr_rts_or_open_hint"
    elif out_calls == 0 and in_calls == 0 and tx_start == 0:
        verdict = "YELLOW_OPENED_NO_DATA"
        reason = "cdc_open_requests_seen_but_no_bulk_in_or_out_activity"
    elif tx_start > 0 and in_calls == 0:
        verdict = "RED_TX_ARMED_NO_IN_COMPLETION"
        reason = "firmware_armed_tx_but_host_controller_did_not_complete_in"
    else:
        verdict = "GREEN_USB_COUNTERS_ACTIVE"
        reason = "cdc_handshake_and_bulk_activity_seen"
    return {
        "verdict": verdict,
        "reason": reason,
        "setup_requests": setup,
        "set_interface_calls": set_interface,
        "set_interface_same_alt_ack": set_interface_same_alt,
        "get_line_coding_calls": get_line_coding,
        "set_line_coding_calls": set_line_coding,
        "dtr_calls": dtr_calls,
        "rts_calls": rts_calls,
        "dtr_state": dtr_state,
        "rts_state": rts_state,
        "host_open_hint": host_open_hint,
        "bulk_out_calls": out_calls,
        "bulk_in_calls": in_calls,
        "tx_start_ok": tx_start,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    elf = Path(args.elf)
    addrs = symbol_addresses(elf, args.nm)
    missing = [name for name in SYMBOLS if name not in addrs]
    cmds = ["init"]
    if args.run_seconds > 0:
        cmds.extend(["halt", "resume", f"sleep {int(args.run_seconds * 1000)}"])
    cmds.extend(["halt", "reg pc", "reg msp", "reg psp"])
    for addr in FAULT_REGS.values():
        cmds.append(f"mdw 0x{addr:08x}")
    for name in SYMBOLS:
        addr = addrs.get(name)
        if addr is not None:
            cmds.append(f"mdw 0x{addr:08x}")
    cmds.extend(["resume", "shutdown"])

    command = ["openocd"]
    for cfg in args.openocd_config:
        command.extend(["-f", cfg])
    for cmd in cmds:
        command.extend(["-c", cmd])

    proc = subprocess.run(command, check=False, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=args.timeout)
    cleanup_openocd()
    values, faults, registers = parse_openocd(proc.stdout, addrs)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "elf": str(elf),
        "openocd_returncode": proc.returncode,
        "missing_symbols": missing,
        "registers": registers,
        "faults": faults,
        "values": values,
        "classification": classify(values, faults),
    }
    return payload


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", default=DEFAULT_ELF)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--openocd-config", action="append", default=list(DEFAULT_CFG))
    parser.add_argument("--outdir", default="rtt_usb_debug_snapshot")
    parser.add_argument("--run-seconds", type=float, default=0.0)
    parser.add_argument("--timeout", type=float, default=60.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    outdir = Path(args.outdir) / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    outdir.mkdir(parents=True, exist_ok=True)
    payload = run(args)
    payload["json_path"] = str(outdir / "usb_debug_snapshot.json")
    with (outdir / "usb_debug_snapshot.json").open("w", encoding="utf-8") as fp:
        json.dump(payload, fp, indent=2, sort_keys=True)
        fp.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    print(f"Evidence directory: {outdir}")
    return 0 if payload.get("classification", {}).get("verdict", "").startswith(("GREEN", "YELLOW")) else 2


if __name__ == "__main__":
    raise SystemExit(main())
