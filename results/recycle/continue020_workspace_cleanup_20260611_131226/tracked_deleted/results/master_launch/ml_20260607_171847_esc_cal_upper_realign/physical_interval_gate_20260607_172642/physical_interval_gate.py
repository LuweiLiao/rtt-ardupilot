#!/usr/bin/env python3
from __future__ import annotations
import glob, json, os, time
from datetime import datetime, timezone
from pymavlink import mavutil

DEFAULT_PORT = "/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00"
MSG_IDS = {
    "HEARTBEAT": 0,
    "SYS_STATUS": 1,
    "RAW_IMU": 27,
    "SCALED_PRESSURE": 29,
    "ATTITUDE": 30,
    "EKF_STATUS_REPORT": 193,
}
REQUEST_MSG_IDS = {k: v for k, v in MSG_IDS.items() if k != "HEARTBEAT"}

def iso_now():
    return datetime.now(timezone.utc).isoformat()

def resolve_port():
    if os.path.exists(DEFAULT_PORT):
        return DEFAULT_PORT
    for pattern in ("/dev/serial/by-id/usb-APM_CUAV_V5_CDC*", "/dev/serial/by-id/*CUAV*CDC*", "/dev/ttyACM*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    raise RuntimeError("no_cdc_port")

def wait_ready(conn, timeout=45):
    start = time.monotonic()
    hist = {}
    last = None
    while time.monotonic() - start < timeout:
        msg = conn.recv_match(type="HEARTBEAT", blocking=True, timeout=1)
        if msg is None:
            continue
        last = msg.to_dict()
        status = int(getattr(msg, "system_status", -1))
        hist[str(status)] = hist.get(str(status), 0) + 1
        if status in (3, 4):
            return {"ready_s": round(time.monotonic() - start, 3), "status_histogram": hist, "last": last}
    raise RuntimeError(json.dumps({"reason": "not_ready", "hist": hist, "last": last}))

def drain(conn, seconds=0.25):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)

def wait_ack(conn, command, timeout=3):
    end = time.monotonic() + timeout
    seen = []
    while time.monotonic() < end:
        msg = conn.recv_match(type="COMMAND_ACK", blocking=True, timeout=0.25)
        if msg is None:
            continue
        d = msg.to_dict()
        seen.append(d)
        if int(getattr(msg, "command", -1)) == command:
            return d, seen
    return None, seen

def get_interval(conn, msg_id, timeout=4):
    conn.mav.command_long_send(conn.target_system, conn.target_component, mavutil.mavlink.MAV_CMD_GET_MESSAGE_INTERVAL, 0, msg_id, 0, 0, 0, 0, 0, 0)
    end = time.monotonic() + timeout
    seen = []
    while time.monotonic() < end:
        msg = conn.recv_match(type=["MESSAGE_INTERVAL", "COMMAND_ACK"], blocking=True, timeout=0.25)
        if msg is None:
            continue
        d = msg.to_dict()
        if len(seen) < 20:
            seen.append(d)
        if msg.get_type() == "MESSAGE_INTERVAL" and int(getattr(msg, "message_id", -1)) == msg_id:
            return d, seen
    return None, seen

def configure(conn, rate=5):
    interval_us = int(1_000_000 / rate)
    records = []
    for name, msg_id in REQUEST_MSG_IDS.items():
        drain(conn, 0.1)
        conn.mav.command_long_send(conn.target_system, conn.target_component, mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL, 0, msg_id, interval_us, 0, 0, 0, 0, 0)
        ack, ack_seen = wait_ack(conn, mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL)
        interval, interval_seen = get_interval(conn, msg_id)
        records.append({"name": name, "msg_id": msg_id, "ack": ack, "ack_seen": ack_seen, "interval": interval, "interval_seen": interval_seen})
    return records

def sample(conn, seconds=30):
    counts = {name: 0 for name in MSG_IDS}
    last = {}
    last_any = None
    max_silence = 0.0
    start = time.monotonic()
    end = start + seconds
    while time.monotonic() < end:
        msg = conn.recv_match(type=list(MSG_IDS.keys()), blocking=True, timeout=1)
        now = time.monotonic()
        if msg is None:
            continue
        if last_any is not None:
            max_silence = max(max_silence, now - last_any)
        last_any = now
        typ = msg.get_type()
        counts[typ] += 1
        last[typ] = msg.to_dict()
    rates = {k: round(v / seconds, 3) for k, v in counts.items()}
    return {"counts": counts, "rates_hz": rates, "last_values": last, "max_silence": round(max_silence, 3)}

def main():
    port = resolve_port()
    conn = mavutil.mavlink_connection(port, baud=115200, robust_parsing=True, source_system=245)
    try:
        hb = conn.wait_heartbeat(timeout=25)
        if hb is None:
            raise RuntimeError("no_heartbeat")
        ready = wait_ready(conn)
        params = {}
        for pname in ("AHRS_EKF_TYPE", "GPS1_TYPE", "SCHED_LOOP_RATE"):
            drain(conn, 0.1)
            conn.mav.param_request_read_send(conn.target_system, conn.target_component, pname.encode("ascii"), -1)
            pend = time.monotonic() + 5
            while time.monotonic() < pend:
                msg = conn.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
                if msg is None:
                    continue
                got = msg.param_id.decode(errors="ignore").rstrip("\x00") if isinstance(msg.param_id, bytes) else str(msg.param_id).rstrip("\x00")
                if got == pname:
                    params[pname] = float(msg.param_value)
                    break
        cfg = configure(conn)
        drain(conn, 2.0)
        smp = sample(conn)
        failures = {}
        required = {"HEARTBEAT": 24, "RAW_IMU": 60, "ATTITUDE": 60, "SCALED_PRESSURE": 60, "EKF_STATUS_REPORT": 1, "SYS_STATUS": 1}
        for name, need in required.items():
            if smp["counts"].get(name, 0) < need:
                failures[name] = f"{smp['counts'].get(name,0)}<{need}"
        bad_cfg = []
        for rec in cfg:
            if not rec.get("ack") or int(rec["ack"].get("result", -1)) != 0:
                bad_cfg.append({"name": rec["name"], "reason": "ack", "ack": rec.get("ack")})
            interval = rec.get("interval")
            if not interval or int(interval.get("interval_us", -1)) != 200000:
                bad_cfg.append({"name": rec["name"], "reason": "interval", "interval": interval})
        if bad_cfg:
            failures["interval_config"] = bad_cfg
        result = {"timestamp_utc": iso_now(), "port": port, "heartbeat": hb.to_dict(), "heartbeat_ready": ready, "params": params, "interval_config": cfg, **smp, "required_counts": required, "failures": failures, "verdict": "GREEN" if not failures else "RED", "reason": "ok" if not failures else "sample_or_config_failed"}
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0 if not failures else 1
    finally:
        conn.close()

if __name__ == "__main__":
    raise SystemExit(main())
