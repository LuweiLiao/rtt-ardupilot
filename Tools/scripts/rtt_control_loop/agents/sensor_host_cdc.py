"""Host USB CDC presence sensor agent (setpoint 1209:5740)."""
from __future__ import annotations

import subprocess
from typing import Any, Dict, List, Tuple


class HostCDCSensorAgent:
    role = "sensor"
    name = "HostCDCSensor"

    def sample(self, **_: Any) -> Dict[str, Any]:
        present, ids = self._read_host_cdc()
        return {"present": present, "ids": ids}

    @staticmethod
    def _read_host_cdc() -> Tuple[bool, List[str]]:
        try:
            out = subprocess.check_output(["lsusb"], text=True, stderr=subprocess.DEVNULL)
        except (subprocess.CalledProcessError, FileNotFoundError):
            return False, []
        ids = [ln.strip() for ln in out.splitlines() if "1209:5740" in ln]
        return bool(ids), ids
