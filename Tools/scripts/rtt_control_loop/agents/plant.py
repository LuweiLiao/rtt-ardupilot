"""Setpoint and plant state for RTT closed-loop control."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

SETPOINT = {
    "hal_run_values": {0xAAAAAAAA, 0xBBBBBBBB, 0x11111111},
    "loop_entry": 0x12345678,
    "usb_init": 2,
    "usb_configured": True,
    "main_loop_iters_min": 50,
    "boot_wait_seconds": 12,
    "cdc_usb_id": "1209:5741",
    "no_hardfault_pc": True,
}

HAL_RUN_OK = SETPOINT["hal_run_values"]
LOOP_ENTRY_OK = SETPOINT["loop_entry"]


@dataclass
class PlantState:
    uart7: Dict[str, Any] = field(default_factory=dict)
    openocd: Dict[str, Any] = field(default_factory=dict)
    host_cdc_present: bool = False
    host_cdc_ids: List[str] = field(default_factory=list)


def _v(d: Dict[str, Any], *keys: str, default: Optional[int] = None) -> Optional[int]:
    for k in keys:
        if k in d and d[k] is not None:
            return int(d[k])
    return default


def merge_plant(uart: Dict[str, Any], oc: Dict[str, Any], host_cdc: bool) -> PlantState:
    return PlantState(uart7=uart, openocd=oc, host_cdc_present=host_cdc)


def setpoint_error(state: PlantState) -> Dict[str, float]:
    uf = state.uart7.get("fields") or {}
    ov = state.openocd.get("vars") or {}

    hal = _v(uf, "hal") or ov.get("rtt_dbg_hal_run_called")
    ent = _v(uf, "ent") or ov.get("rtt_dbg_main_loop_entry_called")
    iters = _v(uf, "iter") or ov.get("rtt_dbg_main_loop_iterations") or 0
    usb = _v(uf, "usb") or ov.get("rtt_dbg_usb_init") or 0
    cfg_u = _v(uf, "cfg")
    cfg_o = state.openocd.get("configured")
    cfg = bool(cfg_u) if cfg_u is not None else bool(cfg_o)
    hf = _v(uf, "hf") or ov.get("rtt_dbg_hardfault_stack_pc") or 0
    stup = _v(uf, "stup") or ov.get("rtt_dbg_usb_setup_stup") or 0

    err: Dict[str, float] = {}
    err["hal_run"] = 0.0 if (hal in HAL_RUN_OK) else 1.0
    err["main_loop"] = 0.0 if (ent == LOOP_ENTRY_OK and iters >= SETPOINT["main_loop_iters_min"]) else 1.0
    err["usb_init"] = 0.0 if usb >= SETPOINT["usb_init"] else 1.0
    err["usb_cfg"] = 0.0 if cfg else 1.0
    err["ep0_progress"] = 0.0 if stup > 1 else (0.5 if stup == 1 else 1.0)
    err["host_cdc"] = 0.0 if state.host_cdc_present else 1.0
    err["hardfault"] = 1.0 if hf not in (0, None) else 0.0
    err["total"] = sum(err.values()) / max(len(err), 1)
    return err


def plant_is_dead(state: PlantState) -> bool:
    ov = state.openocd.get("vars") or {}
    uf = state.uart7.get("fields") or {}
    hal = _v(uf, "hal") or ov.get("rtt_dbg_hal_run_called") or 0
    iters = _v(uf, "iter") or ov.get("rtt_dbg_main_loop_iterations") or 0
    main_called = ov.get("rtt_dbg_main_called") or 0
    return hal in (0, 0xDEADBEEF) and iters == 0 and main_called == 0
