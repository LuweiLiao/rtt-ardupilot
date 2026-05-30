"""UART7 feedback sensor agent."""
from __future__ import annotations

from typing import Any, Dict

from sensors.uart7_sensor import sample as uart7_sample


class UART7SensorAgent:
    role = "sensor"
    name = "UART7Sensor"

    def sample(self, *, uart_port: str, run_seconds: float, **_: Any) -> Dict[str, Any]:
        res = uart7_sample(uart_port, 115200, run_seconds + 10.0)
        return res.to_dict()
