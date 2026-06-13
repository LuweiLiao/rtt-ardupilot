#!/usr/bin/env python3
"""Read one or more @SYS files via MAVLink FTP and write JSON evidence."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pymavlink import mavftp, mavutil


DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def ret_payload(ret: mavftp.MAVFTPReturn) -> dict[str, Any]:
    return {
        "operation": ret.operation_name,
        "error_code": int(ret.error_code),
        "error_name": getattr(ret.error_code, "name", str(ret.error_code)),
        "system_error": int(ret.system_error),
    }


def read_file(ftp: mavftp.MAVFTP, remote_path: str, outdir: Path,
              timeout_s: float) -> dict[str, Any]:
    done: dict[str, bytes | None] = {}
    local_path = outdir / remote_path.strip("/").replace("/", "_").replace("@", "at_")

    def callback(fh: io.BytesIO | None) -> None:
        if fh is None:
            done["data"] = None
            return
        fh.seek(0)
        done["data"] = fh.read()

    ret = ftp.cmd_get([remote_path, str(local_path)], callback=callback)
    result: dict[str, Any] = {
        "remote_path": remote_path,
        "cmd_ret": ret_payload(ret),
    }
    if ret.error_code != mavftp.FtpError.Success:
        return result

    start = time.monotonic()
    while "data" not in done and time.monotonic() - start < timeout_s:
        wait_ret = ftp.process_ftp_reply("BurstReadFile", timeout=8)
        if wait_ret.error_code != mavftp.FtpError.Success:
            result["last_wait_ret"] = ret_payload(wait_ret)
        time.sleep(0.01)

    data = done.get("data")
    if data is None:
        result["error"] = "no_data"
        return result
    local_path.write_bytes(data)
    text = data.decode("utf-8", errors="replace")
    result.update({
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "local_path": str(local_path),
        "text": text,
        "remote_estimated_size": int(ftp.remote_file_size) if ftp.remote_file_size is not None else None,
    })
    return result


def run(args: argparse.Namespace) -> dict[str, Any]:
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    conn = mavutil.mavlink_connection(args.port, baud=115200, robust_parsing=True,
                                      source_system=args.source_system)
    try:
        hb = conn.wait_heartbeat(timeout=args.heartbeat_timeout)
        if hb is None or conn.target_system == 0:
            raise RuntimeError("no_heartbeat")
        ftp = mavftp.MAVFTP(conn, conn.target_system, conn.target_component)
        payload: dict[str, Any] = {
            "timestamp_utc": iso_now(),
            "port": args.port,
            "target_system": int(conn.target_system),
            "target_component": int(conn.target_component),
            "files": {},
        }
        for remote_path in args.paths:
            payload["files"][remote_path] = read_file(ftp, remote_path, outdir, args.timeout)
        payload["verdict"] = (
            "GREEN"
            if all("bytes" in info and info["bytes"] > 0 for info in payload["files"].values())
            else "RED"
        )
        payload["reason"] = "sys_read_ok" if payload["verdict"] == "GREEN" else "sys_read_failed"
        return payload
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Read @SYS files via MAVLink FTP")
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--source-system", type=int, default=244)
    parser.add_argument("--heartbeat-timeout", type=float, default=20.0)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("paths", nargs="+")
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    try:
        payload = run(args)
        rc = 0 if payload["verdict"] == "GREEN" else 2
    except Exception as exc:
        payload = {
            "timestamp_utc": iso_now(),
            "verdict": "RED",
            "reason": str(exc),
            "port": args.port,
            "files": {},
        }
        rc = 2
    json_path = outdir / "mavftp_sys_read.json"
    payload["json_path"] = str(json_path)
    with json_path.open("w", encoding="utf-8") as fp:
        json.dump(payload, fp, indent=2, sort_keys=True)
        fp.write("\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
