"""OpenOCD/GDB feedback sensor agent."""
from __future__ import annotations

from typing import Any, Dict, Optional, TYPE_CHECKING

from sensors.openocd_sensor import sample as openocd_sample

if TYPE_CHECKING:
    from sensors.openocd_session import OpenOCDSession


class OpenOCDSensorAgent:
    role = "sensor"
    name = "OpenOCDSensor"

    def sample(
        self,
        *,
        run_seconds: float,
        session: Optional["OpenOCDSession"] = None,
        **_: Any,
    ) -> Dict[str, Any]:
        if session is not None:
            return session.sample(run_seconds=run_seconds, halt=True).to_dict()
        return openocd_sample(run_seconds=run_seconds, halt=True, start_openocd=True).to_dict()
