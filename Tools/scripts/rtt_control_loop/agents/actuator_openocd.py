"""OpenOCD flash actuator agent."""
from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Any, Dict

ROOT = Path(__file__).resolve().parents[4]


class OpenOCDFlashActuatorAgent:
    role = "actuator"
    name = "OpenOCDFlash"

    def flash_app(self) -> bool:
        script = Path(__file__).resolve().parents[1] / "actuators/flash.sh"
        return subprocess.call(["bash", str(script)], cwd=ROOT) == 0

    def flash_full(self) -> bool:
        script = Path(__file__).resolve().parents[1] / "actuators/flash_full.sh"
        return subprocess.call(["bash", str(script)], cwd=ROOT) == 0

    def execute(self, action: str, **_: Any) -> Dict[str, Any]:
        ok = False
        if action == "FLASH_FULL":
            ok = self.flash_full()
        elif action in ("REBUILD_FLASH", "FLASH_APP"):
            ok = self.flash_app()
        return {"action": action, "ok": ok}
