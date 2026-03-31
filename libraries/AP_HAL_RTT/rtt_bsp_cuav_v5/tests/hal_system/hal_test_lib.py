"""
AP_HAL_RTT — HAL System Test Library

Shared utilities for all HAL-level tests:
  - Board connection (pymavlink, GDB, raw serial)
  - Test result reporting (PASS/FAIL with metrics)
  - Automatic device detection
"""

import os
import sys
import time
import subprocess
import json

# ── Device Detection ──────────────────────────────────────────────

CDC_SYMLINK_PATTERN = "usb-ArduPilot_CUAVv5_RTT_"
ELF_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..",
                         "build", "rtt_deploy", "cuav_v5", "rt-thread.elf")

def find_cdc_port():
    by_id = "/dev/serial/by-id"
    if not os.path.isdir(by_id):
        return None
    for name in os.listdir(by_id):
        if CDC_SYMLINK_PATTERN in name:
            return os.path.realpath(os.path.join(by_id, name))
    return None


def find_elf():
    p = os.path.abspath(ELF_PATH)
    return p if os.path.isfile(p) else None


# ── GDB Helper ────────────────────────────────────────────────────

def gdb_read_vars(var_list, elf=None):
    """Read firmware variables via single-shot GDB.
    Returns dict {var_name: value_string} or None on failure."""
    elf = elf or find_elf()
    if not elf:
        return None
    cmds = [
        "target remote | openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c 'gdb_port pipe'",
        "monitor halt",
    ]
    for v in var_list:
        cmds.append(f"p {v}")
    cmds.append("monitor resume")

    args = ["arm-none-eabi-gdb", "-batch"]
    for c in cmds:
        args += ["-ex", c]
    args.append(elf)

    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=15)
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None

    result = {}
    idx = 0
    for line in r.stdout.splitlines():
        if line.startswith("$"):
            parts = line.split("=", 1)
            if len(parts) == 2 and idx < len(var_list):
                val = parts[1].strip()
                result[var_list[idx]] = val
                idx += 1
    return result if result else None


# ── MAVLink Helper ────────────────────────────────────────────────

def mavlink_connect(port=None, baud=115200, timeout=8):
    """Connect to board via pymavlink. Returns (connection, error_string)."""
    try:
        from pymavlink import mavutil
    except ImportError:
        return None, "pymavlink not installed"

    port = port or find_cdc_port()
    if not port:
        return None, "CDC port not found"

    try:
        m = mavutil.mavlink_connection(port, baud=baud, source_system=255)
        m.wait_heartbeat(timeout=timeout)
        if m.target_system == 0:
            m.close()
            return None, "no heartbeat (target_system=0)"
        return m, None
    except Exception as e:
        return None, str(e)


def mavlink_request_streams(conn, rate_hz=4):
    """Request all data streams at given rate."""
    conn.mav.request_data_stream_send(
        conn.target_system, conn.target_component,
        0, rate_hz, 1)


def fetch_all_params(conn, max_retries=3, overall_timeout=60, stall_timeout=8):
    """Download params with light retry, similar to a real GCS."""
    params = {}
    expected_count = 0

    for attempt in range(max_retries):
        conn.mav.param_request_list_send(conn.target_system, conn.target_component)
        deadline = time.time() + overall_timeout
        stall_start = time.time()

        while time.time() < deadline:
            msg = conn.recv_match(type='PARAM_VALUE', blocking=True, timeout=2)
            if msg:
                param_id = msg.param_id
                if isinstance(param_id, bytes):
                    param_id = param_id.decode(errors='ignore').rstrip('\x00')
                else:
                    param_id = str(param_id).rstrip('\x00')
                params[param_id] = msg.param_value
                if msg.param_count > 0:
                    expected_count = msg.param_count
                stall_start = time.time()
                if expected_count > 0 and len(params) >= expected_count:
                    return params, expected_count

            if time.time() - stall_start > stall_timeout:
                break

        if expected_count > 0 and len(params) >= expected_count:
            break

    return params, expected_count


# ── Test Result Reporting ─────────────────────────────────────────

class TestResult:
    def __init__(self, name):
        self.name = name
        self.checks = []
        self._start = time.time()

    def check(self, label, passed, actual=None, expected=None):
        status = "PASS" if passed else "FAIL"
        detail = ""
        if actual is not None:
            detail = f" (got {actual}"
            if expected is not None:
                detail += f", expect {expected}"
            detail += ")"
        self.checks.append({"label": label, "passed": passed, "detail": detail})
        print(f"  [{self.name}] {label} ... {status}{detail}")
        return passed

    def summary(self):
        passed = sum(1 for c in self.checks if c["passed"])
        total = len(self.checks)
        elapsed = time.time() - self._start
        ok = passed == total
        status = "PASS" if ok else "FAIL"
        print(f"[{self.name}] RESULT: {status} ({passed}/{total}) in {elapsed:.1f}s")
        return ok

    def to_dict(self):
        passed = sum(1 for c in self.checks if c["passed"])
        total = len(self.checks)
        return {
            "name": self.name,
            "passed": passed == total,
            "checks_passed": passed,
            "checks_total": total,
            "elapsed": round(time.time() - self._start, 2),
            "details": self.checks,
        }
