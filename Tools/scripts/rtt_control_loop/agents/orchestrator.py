"""Closed-loop orchestrator: parallel sensors + 10 controllers + actuator."""
from __future__ import annotations

import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from agents.actuator_openocd import OpenOCDFlashActuatorAgent
from agents.base import ControllerReport
from agents.controllers import run_controllers_parallel
from agents.plant import PlantState, SETPOINT, merge_plant, setpoint_error, plant_is_dead
from agents.sensor_host_cdc import HostCDCSensorAgent
from agents.sensor_openocd import OpenOCDSensorAgent
from agents.sensor_uart7 import UART7SensorAgent
from sensors.openocd_session import OpenOCDSession
from sensors.uart7_sensor import UART7Sample, sample as uart7_sample

ROOT = Path(__file__).resolve().parents[4]

# UART collector runs through OpenOCD free-run + halt margin (not +10s slack).
UART_MARGIN_S = 4.0


def git_dirty() -> bool:
    try:
        out = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        )
        return bool(out.strip())
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


@dataclass
class CycleTiming:
    sensors_s: float = 0.0
    cdc_s: float = 0.0
    controllers_s: float = 0.0
    actuator_s: float = 0.0
    total_s: float = 0.0


def _sample_sensors_parallel(
    *,
    uart_port: str,
    run_seconds: float,
    session: Optional[OpenOCDSession],
    dirty_box: Dict[str, bool],
    cdc_box: Dict[str, Any],
    timing: CycleTiming,
) -> Tuple[Dict[str, Any], Dict[str, Any]]:
    """UART7 + OpenOCD + git_dirty + host CDC in parallel (4 workers)."""
    oc_agent = OpenOCDSensorAgent()
    cdc_agent = HostCDCSensorAgent()
    t0 = time.perf_counter()

    with ThreadPoolExecutor(max_workers=4) as pool:
        f_uart = pool.submit(
            uart7_sample, uart_port, 115200, run_seconds + UART_MARGIN_S
        )
        f_oc = pool.submit(oc_agent.sample, run_seconds=run_seconds, session=session)
        f_dirty = pool.submit(git_dirty)
        f_cdc = pool.submit(cdc_agent.sample)

        uart_res: Any = None
        oc_res: Any = None
        for fut in as_completed((f_uart, f_oc)):
            if fut is f_uart:
                uart_res = fut.result()
            else:
                oc_res = fut.result()
        dirty_box["dirty"] = f_dirty.result()
        cdc_box.update(f_cdc.result())

    timing.sensors_s = time.perf_counter() - t0
    uart_dict = uart_res.to_dict() if isinstance(uart_res, UART7Sample) else uart_res
    return uart_dict, oc_res


class ControlLoopEngine:
    """Multi-cycle engine: one OpenOCD session, parallel sensor/controller agents."""

    def __init__(
        self,
        *,
        uart_port: str = "/dev/ttyACM0",
        run_seconds: float = 22.0,
        reuse_openocd: bool = True,
    ) -> None:
        self.uart_port = uart_port
        self.run_seconds = run_seconds
        self.reuse_openocd = reuse_openocd
        self._session: Optional[OpenOCDSession] = None
        self.last_timing = CycleTiming()

    def __enter__(self) -> "ControlLoopEngine":
        self._session = OpenOCDSession(reuse_existing=self.reuse_openocd)
        self._session.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._session is not None:
            self._session.stop()
            self._session = None

    def one_cycle(self, *, auto_actuator: bool = False) -> Tuple[PlantState, Dict[str, float], List[ControllerReport], bool]:
        return one_cycle(
            uart_port=self.uart_port,
            run_seconds=self.run_seconds,
            auto_actuator=auto_actuator,
            session=self._session,
            timing=self.last_timing,
        )


def one_cycle(
    *,
    uart_port: str,
    run_seconds: float,
    auto_actuator: bool,
    session: Optional[OpenOCDSession] = None,
    timing: Optional[CycleTiming] = None,
) -> Tuple[PlantState, Dict[str, float], List[ControllerReport], bool]:
    t_cycle = time.perf_counter()
    timing = timing or CycleTiming()
    flash_agent = OpenOCDFlashActuatorAgent()
    dirty_box: Dict[str, bool] = {"dirty": False}
    cdc_box: Dict[str, Any] = {}

    uart_res, oc_res = _sample_sensors_parallel(
        uart_port=uart_port,
        run_seconds=run_seconds,
        session=session,
        dirty_box=dirty_box,
        cdc_box=cdc_box,
        timing=timing,
    )

    timing.cdc_s = 0.0  # folded into sensors_s

    state = merge_plant(uart_res, oc_res, cdc_box.get("present", False))
    state.host_cdc_ids = cdc_box.get("ids") or []
    err = setpoint_error(state)

    t_ctrl = time.perf_counter()
    reports = run_controllers_parallel(state, err, code_dirty=dirty_box["dirty"])
    timing.controllers_s = time.perf_counter() - t_ctrl

    flashed = False
    t_act = time.perf_counter()
    if auto_actuator and err.get("total", 1) > 0:
        actions = {r.action for r in reports}
        if "FLASH_FULL" in actions or plant_is_dead(state):
            print("[C9] Executing actuator: flash bootloader + app...")
            flashed = flash_agent.flash_full()
            if flashed:
                wait = SETPOINT["boot_wait_seconds"] + 3
                print(f"[C9] Post-flash settle {wait}s...")
                time.sleep(wait)
        elif "REBUILD_FLASH" in actions or dirty_box["dirty"]:
            print("[C9] Executing actuator: rebuild + flash app...")
            flashed = flash_agent.flash_app()
    timing.actuator_s = time.perf_counter() - t_act
    timing.total_s = time.perf_counter() - t_cycle

    return state, err, reports, flashed


def print_cycle(
    state: PlantState,
    err: Dict[str, float],
    reports: List[ControllerReport],
    *,
    timing: Optional[CycleTiming] = None,
) -> None:
    import json

    print("\n=== Plant (sensors) ===")
    print(json.dumps({
        "uart7": state.uart7,
        "openocd": {k: state.openocd.get(k) for k in ("ok", "pc", "cfsr", "configured", "vars", "error")},
        "host_cdc": state.host_cdc_present,
    }, indent=2))
    print("\n=== Error vector (setpoint) ===")
    print(json.dumps(err, indent=2))
    print("\n=== Controllers (10 parallel agents) ===")
    for r in reports:
        print(f"  [{r.id}] {r.name}: error={r.error:.2f} sev={r.severity} action={r.action}")
        print(f"       {r.message}")
    if timing is not None:
        print(
            f"\n=== Perf === sensors={timing.sensors_s:.1f}s "
            f"cdc={timing.cdc_s:.2f}s controllers={timing.controllers_s:.2f}s "
            f"actuator={timing.actuator_s:.1f}s total={timing.total_s:.1f}s"
        )
    if err.get("total", 1) == 0:
        print("\n*** SETPOINT REACHED ***")
    else:
        print(f"\n*** SETPOINT NOT MET (total error={err.get('total', 1):.3f}) ***")
