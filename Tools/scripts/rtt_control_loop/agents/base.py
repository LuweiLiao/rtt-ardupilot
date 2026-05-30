"""Agent base types for RTT control loop."""
from __future__ import annotations

from dataclasses import dataclass, asdict
from typing import Any, Callable, Dict, List, Protocol

from agents.plant import PlantState


@dataclass
class ControllerReport:
    id: str
    name: str
    error: float
    severity: str
    action: str
    message: str

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


class SensorAgent(Protocol):
    role: str

    def sample(self, **kwargs: Any) -> Dict[str, Any]: ...


class ControllerAgent(Protocol):
    id: str
    name: str

    def run(self, state: PlantState, err: Dict[str, float], **kwargs: Any) -> ControllerReport: ...


ControllerFn = Callable[..., ControllerReport]
