#!/usr/bin/env python3
"""RTT MAVLink FTP gate for CUAV v5.

This gate uses pymavlink's MAVFTP client to exercise the same MAVLink FTP path
used by GCS tools:
- reset FTP sessions;
- list "/" and "/APM";
- read "@PARAM/param.pck";
- write, read back, and remove a small file on "/APM";
- verify heartbeat stability after FTP traffic.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pymavlink import mavftp, mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"


class GateError(RuntimeError):
    def __init__(self, reason: str, payload: dict[str, Any]):
        super().__init__(reason)
        self.payload = payload


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(port_arg: str) -> str:
    if port_arg != "auto":
        return port_arg
    if os.path.exists(DEFAULT_PORT):
        return DEFAULT_PORT
    for pattern in (
        "/dev/serial/by-id/usb-APM_CUAV_V5_CDC*",
        "/dev/serial/by-id/*CUAV*CDC*",
        "/dev/serial/by-id/*ArduPilot*",
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    acms = sorted(glob.glob("/dev/ttyACM*"))
    if acms:
        return acms[-1]
    raise RuntimeError("no_cdc_port")


def connect(port: str, source_system: int, timeout_s: float) -> Any:
    conn = mavutil.mavlink_connection(
        port,
        baud=115200,
        robust_parsing=True,
        source_system=source_system,
    )
    hb = conn.wait_heartbeat(timeout=timeout_s)
    if hb is None or conn.target_system == 0:
        conn.close()
        raise RuntimeError("no_heartbeat")
    return conn


def drain(conn: Any, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def ret_payload(ret: mavftp.MAVFTPReturn) -> dict[str, Any]:
    return {
        "operation": ret.operation_name,
        "error_code": int(ret.error_code),
        "error_name": getattr(ret.error_code, "name", str(ret.error_code)),
        "system_error": int(ret.system_error),
    }


def require_success(ret: mavftp.MAVFTPReturn, payload: dict[str, Any]) -> None:
    if ret.error_code != mavftp.FtpError.Success:
        raise GateError(
            f"ftp_{ret.operation_name}_failed:{getattr(ret.error_code, 'name', ret.error_code)}",
            payload,
        )


def timed_step(payload: dict[str, Any], name: str, fn: Any) -> Any:
    start = time.monotonic()
    try:
        result = fn()
    except Exception as exc:
        payload["steps"].append({
            "name": name,
            "elapsed_s": round(time.monotonic() - start, 3),
            "error": str(exc),
        })
        raise
    payload["steps"].append({
        "name": name,
        "elapsed_s": round(time.monotonic() - start, 3),
    })
    return result


def list_dir(ftp: mavftp.MAVFTP, path: str, payload: dict[str, Any]) -> list[dict[str, Any]]:
    def op() -> mavftp.MAVFTPReturn:
        return ftp.cmd_list([path])

    ret = timed_step(payload, f"list_{path}", op)
    payload[f"list_ret_{path}"] = ret_payload(ret)
    require_success(ret, payload)
    entries = [
        {"name": entry.name, "is_dir": bool(entry.is_dir), "size_b": int(entry.size_b)}
        for entry in ftp.list_result
    ]
    payload[f"entries_{path}"] = entries
    return entries


def safe_local_name(remote_path: str) -> str:
    return remote_path.strip("/").replace("/", "_").replace("@", "at_") or "root"


def decode_param_pck(data: bytes) -> dict[str, Any]:
    if len(data) < 6:
        raise GateError("param_pck_header_too_short", {"bytes": len(data)})

    magic, num_params, total_params = struct.unpack("<HHH", data[:6])
    if magic not in (0x671B, 0x671C):
        raise GateError(f"param_pck_bad_magic:0x{magic:04x}", {"magic": magic})

    type_lengths = {
        1: 1,  # AP_PARAM_INT8
        2: 2,  # AP_PARAM_INT16
        3: 4,  # AP_PARAM_INT32
        4: 4,  # AP_PARAM_FLOAT
    }
    pos = 6
    last_name = ""
    count = 0
    first_names: list[str] = []
    last_names: list[str] = []
    pad_bytes = 0
    with_defaults = magic == 0x671C

    while pos < len(data):
        while pos < len(data) and data[pos] == 0:
            pad_bytes += 1
            pos += 1
        if pos >= len(data):
            break
        if pos + 2 > len(data):
            raise GateError(f"param_pck_truncated_descriptor:pos={pos}",
                            {"pos": pos, "bytes": len(data)})

        ptype_flags = data[pos]
        name_info = data[pos + 1]
        ptype = ptype_flags & 0x0F
        flags = (ptype_flags >> 4) & 0x0F
        if ptype not in type_lengths:
            raise GateError(f"param_pck_bad_type:{ptype}:pos={pos}",
                            {"pos": pos, "ptype": ptype})
        common_len = name_info & 0x0F
        name_len = ((name_info >> 4) & 0x0F) + 1
        if common_len > len(last_name) or common_len + name_len > 16:
            raise GateError(
                f"param_pck_bad_name_lengths:common={common_len}:name={name_len}:pos={pos}",
                {"pos": pos, "common_len": common_len, "name_len": name_len},
            )

        type_len = type_lengths[ptype]
        value_len = type_len * (2 if (flags & 0x01) else 1)
        end = pos + 2 + name_len + value_len
        if end > len(data):
            raise GateError(f"param_pck_truncated_value:pos={pos}:end={end}:bytes={len(data)}",
                            {"pos": pos, "end": end, "bytes": len(data)})

        suffix = data[pos + 2:pos + 2 + name_len].decode("utf-8", errors="strict")
        name = last_name[:common_len] + suffix
        if not name:
            raise GateError(f"param_pck_empty_name:pos={pos}", {"pos": pos})
        last_name = name
        count += 1
        if len(first_names) < 5:
            first_names.append(name)
        last_names.append(name)
        if len(last_names) > 5:
            last_names.pop(0)
        pos = end

    if count != num_params or count > total_params:
        raise GateError(
            f"param_pck_count_mismatch:decoded={count}:header={num_params}/{total_params}",
            {"decoded_count": count, "num_params": num_params, "total_params": total_params},
        )
    if num_params != total_params:
        raise GateError(
            f"param_pck_partial_file:{num_params}/{total_params}",
            {"decoded_count": count, "num_params": num_params, "total_params": total_params},
        )

    return {
        "magic": f"0x{magic:04x}",
        "with_defaults": with_defaults,
        "decoded_count": count,
        "num_params": num_params,
        "total_params": total_params,
        "pad_bytes": pad_bytes,
        "first_names": first_names,
        "last_names": last_names,
    }


def ftp_read(ftp: mavftp.MAVFTP, remote_path: str, local_dir: Path,
             payload: dict[str, Any]) -> bytes:
    done: dict[str, bytes | None] = {}
    local_dir.mkdir(parents=True, exist_ok=True)
    local_path = local_dir / safe_local_name(remote_path)

    def callback(fh: io.BytesIO | None) -> None:
        if fh is None:
            done["data"] = None
            return
        fh.seek(0)
        done["data"] = fh.read()

    def op() -> mavftp.MAVFTPReturn:
        return ftp.cmd_get([remote_path, str(local_path)], callback=callback)

    ret = timed_step(payload, f"read_{remote_path}", op)
    payload[f"read_ret_{remote_path}"] = ret_payload(ret)
    require_success(ret, payload)

    start = time.monotonic()
    wait_rets: list[dict[str, Any]] = []
    while "data" not in done and time.monotonic() - start < 35.0:
        ret = ftp.process_ftp_reply("BurstReadFile", timeout=8)
        ret_info = ret_payload(ret)
        if ret.error_code != mavftp.FtpError.Success:
            wait_rets.append(ret_info)
        time.sleep(0.01)
    if wait_rets:
        payload[f"read_intermediate_rets_{remote_path}"] = wait_rets[-5:]

    data = done.get("data")
    if data is None or len(data) == 0:
        raise GateError(f"ftp_read_empty:{remote_path}", payload)
    remote_estimated_size = ftp.remote_file_size
    payload[f"read_{remote_path}"] = {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "path": str(local_path),
    }
    if remote_estimated_size is not None:
        payload[f"read_{remote_path}"]["remote_estimated_size"] = int(remote_estimated_size)
    if remote_path.startswith("@PARAM/") and remote_estimated_size != len(data):
        payload[f"read_{remote_path}"]["size_note"] = (
            "AP_Filesystem_Param::stat() reports an estimated unscanned size; "
            "param.pck is compressed and may be smaller when complete."
        )
    return bytes(data)


def ftp_put(ftp: mavftp.MAVFTP, local_path: Path, remote_path: str,
            payload: dict[str, Any]) -> None:
    done: dict[str, Any] = {}

    def callback(length: int | None) -> None:
        done["length"] = length

    def op() -> mavftp.MAVFTPReturn:
        return ftp.cmd_put([str(local_path), remote_path], callback=callback)

    ret = timed_step(payload, f"put_{remote_path}", op)
    payload[f"put_ret_{remote_path}"] = ret_payload(ret)
    require_success(ret, payload)

    start = time.monotonic()
    while "length" not in done and time.monotonic() - start < 30.0:
        ret = ftp.process_ftp_reply("WriteFile", timeout=8)
        if ret.error_code != mavftp.FtpError.Success:
            payload[f"put_wait_ret_{remote_path}"] = ret_payload(ret)
            require_success(ret, payload)
        time.sleep(0.01)

    if done.get("length") != local_path.stat().st_size:
        raise GateError(f"ftp_put_incomplete:{remote_path}:{done}", payload)


def ftp_rm(ftp: mavftp.MAVFTP, remote_path: str, payload: dict[str, Any],
           allow_missing: bool = False) -> None:
    def op() -> mavftp.MAVFTPReturn:
        return ftp.cmd_rm([remote_path])

    ret = timed_step(payload, f"rm_{remote_path}", op)
    payload[f"rm_ret_{remote_path}"] = ret_payload(ret)
    if allow_missing and ret.error_code == mavftp.FtpError.FileNotFound:
        return
    require_success(ret, payload)


def heartbeat_stability(conn: Any, seconds: float) -> dict[str, Any]:
    start = time.monotonic()
    count = 0
    statuses: list[int] = []
    while time.monotonic() - start < seconds:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if msg is None:
            continue
        count += 1
        statuses.append(int(msg.system_status))
    return {
        "seconds": seconds,
        "heartbeats": count,
        "statuses": statuses,
        "stable": count >= max(2, int(seconds) - 1) and all(status in (3, 4) for status in statuses),
    }


def run_gate(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "port": port,
        "steps": [],
    }

    conn = connect(port, source_system=args.source_system, timeout_s=args.heartbeat_timeout)
    try:
        payload["target_system"] = int(conn.target_system)
        payload["target_component"] = int(conn.target_component)
        drain(conn, 0.4)

        ftp = timed_step(
            payload,
            "ftp_init_reset_sessions",
            lambda: mavftp.MAVFTP(conn, conn.target_system, conn.target_component),
        )

        root_entries = list_dir(ftp, "/", payload)
        if args.require_sd and not any(entry["name"] == "APM" and entry["is_dir"] for entry in root_entries):
            raise GateError("sd_apm_directory_missing", payload)

        list_dir(ftp, "/APM", payload)
        download_dir = outdir / "downloads"
        param_data = ftp_read(ftp, "@PARAM/param.pck", download_dir, payload)
        if len(param_data) < args.min_param_bytes:
            raise GateError(f"param_pck_too_small:{len(param_data)}", payload)
        try:
            payload["param_pck_decode"] = decode_param_pck(param_data)
        except GateError as exc:
            exc.payload.update(payload)
            raise exc

        test_bytes = (
            b"rtt-mavftp-gate\n"
            + iso_now().encode("ascii")
            + b"\n"
            + os.urandom(args.test_file_bytes)
        )
        remote_test = args.remote_test_path
        with tempfile.NamedTemporaryFile(prefix="rtt_mavftp_gate_", delete=False) as tmp:
            local_path = Path(tmp.name)
            tmp.write(test_bytes)

        try:
            ftp_rm(ftp, remote_test, payload, allow_missing=True)
            ftp_put(ftp, local_path, remote_test, payload)
            readback = ftp_read(ftp, remote_test, download_dir, payload)
            if readback != test_bytes:
                raise GateError("ftp_readback_mismatch", payload)
            ftp_rm(ftp, remote_test, payload)
        finally:
            try:
                local_path.unlink(missing_ok=True)
            except OSError:
                pass

        reset_ret = timed_step(payload, "reset_sessions_final", lambda: ftp.cmd_cancel())
        payload["reset_sessions_final_ret"] = ret_payload(reset_ret)
        require_success(reset_ret, payload)

        stability = heartbeat_stability(conn, args.post_ftp_stability)
        payload["post_ftp_stability"] = stability
        if not stability["stable"]:
            raise GateError("post_ftp_heartbeat_unstable", payload)

    except GateError:
        payload.setdefault("verdict", "RED")
        payload.setdefault("reason", str(sys.exc_info()[1]))
        raise
    except Exception:
        payload["verdict"] = "RED"
        payload["reason"] = str(sys.exc_info()[1])
        raise GateError(payload["reason"], payload)
    finally:
        conn.close()

    payload["verdict"] = "GREEN"
    payload["reason"] = "mavftp_ok"
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="RTT MAVLink FTP gate")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--source-system", type=int, default=243)
    parser.add_argument("--heartbeat-timeout", type=float, default=25.0)
    parser.add_argument("--require-sd", action="store_true")
    parser.add_argument("--param-read-size", type=int, default=4096)
    parser.add_argument("--min-param-bytes", type=int, default=128)
    parser.add_argument("--test-file-bytes", type=int, default=512)
    parser.add_argument("--remote-test-path", default="/APM/test_ftp.tmp")
    parser.add_argument("--post-ftp-stability", type=float, default=4.0)
    args = parser.parse_args()

    resolved_port = args.port
    try:
        resolved_port = resolve_port(args.port)
        payload = run_gate(args)
        rc = 0
    except GateError as exc:
        payload = exc.payload
        payload.setdefault("verdict", "RED")
        payload.setdefault("reason", str(exc))
        rc = 2
    except Exception as exc:
        payload = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": str(exc),
            "port": resolved_port,
        }
        rc = 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    json_path = outdir / "mavftp_gate.json"
    payload["json_path"] = str(json_path)
    with json_path.open("w", encoding="utf-8") as outfile:
        json.dump(payload, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return rc


if __name__ == "__main__":
    sys.exit(main())
