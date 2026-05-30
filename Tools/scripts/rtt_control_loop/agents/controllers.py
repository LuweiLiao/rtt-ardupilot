"""Ten parallel controller agents (C0–C9)."""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from typing import Dict, List

from agents.base import ControllerReport
from agents.plant import PlantState, SETPOINT, plant_is_dead, _v


def c0_supervisor(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    total = err.get("total", 1.0)
    if total == 0:
        return ControllerReport("C0", "Supervisor", 0, "ok", "HOLD", "Setpoint reached")
    top = max(((k, v) for k, v in err.items() if k != "total"), key=lambda x: x[1])
    return ControllerReport(
        "C0", "Supervisor", total, "warn" if total < 0.5 else "fail",
        "COORDINATE", f"Largest error: {top[0]}={top[1]:.2f}",
    )


def c1_boot(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    pc = state.openocd.get("pc")
    vtor = state.openocd.get("vtor")
    if plant_is_dead(state):
        return ControllerReport(
            "C1", "Boot/Reset", 1.0, "fail", "FLASH_FULL",
            f"Plant dead (all dbg=0); flash BL+app, wait ≥{SETPOINT['boot_wait_seconds']}s before sample",
        )
    if pc is None:
        return ControllerReport("C1", "Boot/Reset", 0.5, "warn", "SAMPLE", "PC unknown")
    in_app = 0x08008000 <= pc < 0x08100000
    in_bl = 0x08000000 <= pc < 0x08008000
    if in_bl:
        return ControllerReport(
            "C1", "Boot/Reset", 0.8, "warn", "WAIT_BOOT",
            f"PC=0x{pc:08x} in bootloader — wait ≥{SETPOINT['boot_wait_seconds']}s after reset",
        )
    if not in_app:
        return ControllerReport("C1", "Boot/Reset", 1.0, "fail", "CHECK_FLASH",
                                f"PC=0x{pc:08x} outside app; VTOR=0x{(vtor or 0):08x}")
    return ControllerReport("C1", "Boot/Reset", 0.0, "ok", "NONE",
                            f"PC=0x{pc:08x} VTOR=0x{(vtor or 0):08x}")


def c2_startup(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    ov = state.openocd.get("vars") or {}
    uf = state.uart7.get("fields") or {}
    hal = _v(uf, "hal") or ov.get("rtt_dbg_hal_run_called")
    stg = _v(uf, "stg") or ov.get("rtt_dbg_setup_stage") or 0
    e = err.get("hal_run", 1.0)
    if e == 0:
        return ControllerReport("C2", "Startup", 0, "ok", "NONE", f"setup ok hal=0x{hal:08x} stage={stg}")
    return ControllerReport("C2", "Startup", e, "fail", "TRACE_SETUP",
                            f"setup incomplete hal=0x{(hal or 0):08x} stage={stg}")


def c3_hardfault(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    ov = state.openocd.get("vars") or {}
    uf = state.uart7.get("fields") or {}
    hf = _v(uf, "hf") or ov.get("rtt_dbg_hardfault_stack_pc") or 0
    cfsr = state.openocd.get("cfsr")
    pc = state.openocd.get("pc")
    if hf == 0 and (pc is None or pc != 0x08008412):
        return ControllerReport("C3", "HardFault", 0, "ok", "NONE", "No fault captured")
    msg = f"hf_pc=0x{hf:08x}"
    if cfsr is not None:
        msg += f" CFSR=0x{cfsr:08x}"
    if pc == 0x08008412:
        msg += " halted in hardfault_hang"
    return ControllerReport("C3", "HardFault", 1.0, "fail", "GDB_BT",
                            msg + " — thread PSP bt at HardFault")


def c4_iwdg(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    ov = state.openocd.get("vars") or {}
    hal = ov.get("rtt_dbg_hal_run_called")
    if hal in (None, 0, 0xDEADBEEF):
        return ControllerReport("C4", "IWDG", 0.7, "warn", "CHECK_IWDG",
                                "hal_run not set — verify ap_rtt_iwdg_init feed-before-config")
    return ControllerReport("C4", "IWDG", 0.0, "ok", "NONE", "Past IWDG init window")


def c5_usb_phy(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    uf = state.uart7.get("fields") or {}
    ov = state.openocd.get("vars") or {}
    usb = _v(uf, "usb") or ov.get("rtt_dbg_usb_init") or 0
    usbrst = _v(uf, "usbrst") or ov.get("rtt_dbg_usb_usbrst") or 0
    if usb < 2:
        return ControllerReport("C5", "USB/Init", 1.0, "fail", "FIX_USB_INIT",
                                f"usb_init={usb} (expect 2)")
    if usbrst == 0:
        return ControllerReport("C5", "USB/Init", 0.6, "warn", "CHECK_HOST",
                                "No USBRST — host may not see device (cable/port/power)")
    return ControllerReport("C5", "USB/Init", 0.0, "ok", "NONE", f"usb_init={usb} usbrst={usbrst}")


def c6_ep0(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    uf = state.uart7.get("fields") or {}
    ov = state.openocd.get("vars") or {}
    stup = _v(uf, "stup") or ov.get("rtt_dbg_usb_setup_stup") or 0
    cfg = _v(uf, "cfg")
    if cfg == 1:
        return ControllerReport("C6", "EP0/Enum", 0, "ok", "NONE", "configured=1")
    if stup <= 1:
        return ControllerReport("C6", "EP0/Enum", 1.0, "fail", "FIX_EP0",
                                f"setup_stup={stup} configured=0 — EP0 STATUS/SET_CONFIGURATION")
    return ControllerReport("C6", "EP0/Enum", 0.5, "warn", "FIX_EP0", f"stup={stup} but cfg=0")


def c7_main_loop(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    uf = state.uart7.get("fields") or {}
    ov = state.openocd.get("vars") or {}
    iters = _v(uf, "iter") or ov.get("rtt_dbg_main_loop_iterations") or 0
    lhz = _v(uf, "loop_hz") or 0
    ov_cnt = _v(uf, "ov") or ov.get("rtt_dbg_overrun_count") or 0
    e = err.get("main_loop", 1.0)
    if e == 0:
        return ControllerReport("C7", "MainLoop", 0, "ok", "NONE",
                                f"iter={iters} loop_hz≈{lhz} overrun={ov_cnt}")
    return ControllerReport("C7", "MainLoop", e, "fail", "WAIT_OR_FIX",
                            f"iter={iters} (need ≥{SETPOINT['main_loop_iters_min']})")


def c8_host_cdc(state: PlantState, err: Dict[str, float], **_) -> ControllerReport:
    if state.host_cdc_present:
        return ControllerReport("C8", "HostCDC", 0, "ok", "MAVLINK_TEST",
                                f"lsusb 1209:5741 OK: {state.host_cdc_ids[:1]}")
    return ControllerReport("C8", "HostCDC", 1.0, "fail", "FIX_PLANT_USB",
                            "Host missing 1209:5741 (note: /dev/ttyACM0 may be CH340 UART7)")


def c9_actuator(state: PlantState, err: Dict[str, float], *, code_dirty: bool = False, **_) -> ControllerReport:
    if err.get("total", 1) == 0:
        return ControllerReport("C9", "Actuator", 0, "ok", "NONE", "No flash needed")
    if plant_is_dead(state):
        return ControllerReport("C9", "Actuator", 0.9, "fail", "FLASH_FULL",
                                "Flash bootloader+app (actuators/flash_full.sh), then re-sample ≥12s")
    if code_dirty:
        return ControllerReport("C9", "Actuator", 0.3, "info", "REBUILD_FLASH",
                                "Local changes — run actuator flash after fix")
    return ControllerReport("C9", "Actuator", 0.2, "info", "REFLASH_OPTIONAL",
                            "Plant error but no pending diff — reflash only if firmware stale")


CONTROLLER_AGENTS = [
    c0_supervisor, c1_boot, c2_startup, c3_hardfault, c4_iwdg,
    c5_usb_phy, c6_ep0, c7_main_loop, c8_host_cdc, c9_actuator,
]


def run_controllers_parallel(
    state: PlantState,
    err: Dict[str, float],
    *,
    code_dirty: bool = False,
) -> List[ControllerReport]:
    def _invoke(fn):
        if fn is c9_actuator:
            return fn(state, err, code_dirty=code_dirty)
        return fn(state, err)

    with ThreadPoolExecutor(max_workers=10) as pool:
        reports = list(pool.map(_invoke, CONTROLLER_AGENTS))
    return reports
