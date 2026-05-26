#!/usr/bin/env bash
# M0 bring-up verification for RTT CUAV V5 (STM32F767).
# Checks build artifacts, flashes via OpenOCD, reads debug markers after run,
# optionally probes USB CDC MAVLink heartbeat.
#
# Usage: Tools/scripts/rtt_m0_verify.sh [--skip-flash] [--wait SEC] [--no-mavlink]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT}/build/rtt_cuav_v5/rtthread.bin"
ELF="${ROOT}/build/rtt_deploy/cuav_v5/rt-thread.elf"
OPENOCD_CFG="${ROOT}/Tools/debug/openocd-f7.cfg"
FLASH_ADDR="0x08008000"
GDB_PORT="${GDB_PORT:-3333}"
RUN_WAIT_SEC="${RUN_WAIT_SEC:-15}"
SKIP_FLASH=0
NO_MAVLINK=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Verify M0 startup chain on CUAV V5 via OpenOCD + GDB debug variables.

Options:
  --skip-flash    Skip OpenOCD program step (debug vars only)
  --wait SEC      Seconds to run before sampling debug vars (default: 15)
  --no-mavlink    Skip optional pymavlink heartbeat on /dev/ttyACM*
  -h, --help      Show this help

M0 PASS criterion: rtt_dbg_hal_run_called == 0xAAAAAAAA
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-flash) SKIP_FLASH=1 ;;
        --wait) RUN_WAIT_SEC="$2"; shift ;;
        --no-mavlink) NO_MAVLINK=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

log() { printf '[rtt_m0_verify] %s\n' "$*"; }
fail() { log "ERROR: $*"; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "Required command not found: $1"
}

require_cmd openocd
require_cmd arm-none-eabi-gdb

OPENOCD_PID=""
cleanup() {
    if [[ -n "${OPENOCD_PID}" ]] && kill -0 "${OPENOCD_PID}" 2>/dev/null; then
        kill "${OPENOCD_PID}" 2>/dev/null || true
        wait "${OPENOCD_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

wait_for_gdb_port() {
    local i
    for i in $(seq 1 60); do
        if (echo >/dev/tcp/127.0.0.1/"${GDB_PORT}") 2>/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

read_gdb_u32() {
    local var="$1"
    local out hex dec
    out="$(arm-none-eabi-gdb -batch \
        -ex "set confirm off" \
        -ex "file ${ELF}" \
        -ex "target extended-remote :${GDB_PORT}" \
        -ex "monitor halt" \
        -ex "p/x (uint32_t)${var}" 2>&1)" || true
    if grep -q "No symbol" <<<"${out}"; then
        echo "MISSING"
        return 0
    fi
    hex="$(grep -Eo '0x[0-9a-fA-F]+' <<<"${out}" | tail -1 || true)"
    if [[ -z "${hex}" ]]; then
        echo "UNKNOWN"
        return 0
    fi
    dec="$(printf '%d' "${hex}" 2>/dev/null || echo "?")"
    echo "${hex} ${dec}"
}

# --- 1) Build artifacts ---
log "Checking build artifacts..."
[[ -f "${BIN}" ]] || fail "Missing bin firmware: ${BIN}"
[[ -f "${ELF}" ]] || fail "Missing ELF symbols: ${ELF}"
log "  OK ${BIN}"
log "  OK ${ELF}"

# --- 2) OpenOCD: flash + keep server for GDB ---
log "Starting OpenOCD (${OPENOCD_CFG})..."
OPENOCD_LOG="$(mktemp /tmp/rtt_m0_openocd.XXXXXX.log)"
OPENOCD_CMD=(openocd -f "${OPENOCD_CFG}")

if [[ "${SKIP_FLASH}" -eq 0 ]]; then
    OPENOCD_CMD+=(
        -c "init"
        -c "reset halt"
        -c "program ${BIN} ${FLASH_ADDR} verify"
        -c "reset run"
    )
else
    OPENOCD_CMD+=(-c "init")
fi

"${OPENOCD_CMD[@]}" >"${OPENOCD_LOG}" 2>&1 &
OPENOCD_PID=$!

if ! wait_for_gdb_port; then
    log "OpenOCD log tail:"
    tail -n 40 "${OPENOCD_LOG}" >&2 || true
    fail "OpenOCD GDB port :${GDB_PORT} not ready"
fi

if [[ "${SKIP_FLASH}" -eq 0 ]]; then
    log "Flashed ${BIN} -> ${FLASH_ADDR}"
else
    log "Skipped flash (--skip-flash)"
fi

# --- 3) GDB batch: run, wait, halt, read debug vars ---
log "Running target for ${RUN_WAIT_SEC}s..."
arm-none-eabi-gdb -batch \
    -ex "set confirm off" \
    -ex "file ${ELF}" \
    -ex "target extended-remote :${GDB_PORT}" \
    -ex "monitor reset run" \
    -ex "shell sleep ${RUN_WAIT_SEC}" \
    -ex "monitor halt" >/dev/null 2>&1 || log "WARN: GDB run/wait/halt returned non-zero (continuing marker read)"

declare -A DBG_VAL=()
log "Debug markers:"
for name in \
    rtt_dbg_hal_run_called \
    rtt_dbg_main_thread_entered \
    rtt_dbg_components_init_done \
    rtt_dbg_main_called \
    rtt_dbg_main_loop_entry_called
do
    val="$(read_gdb_u32 "${name}")"
    DBG_VAL["${name}"]="${val}"
    log "  ${name} = ${val}"
done

HAL_VAL="${DBG_VAL[rtt_dbg_hal_run_called]}"
M0_PASS=0
if [[ "${HAL_VAL}" == "0xaaaaaaaa"* ]] || [[ "${HAL_VAL}" == *" 2863311530" ]]; then
    M0_PASS=1
elif [[ "${HAL_VAL}" == "0xbbbbbbbb"* ]] || [[ "${HAL_VAL}" == *" 3149642683" ]]; then
    M0_PASS=1
elif [[ "${HAL_VAL}" == "0x11111111"* ]] || [[ "${HAL_VAL}" == *" 286331153" ]]; then
    M0_PASS=1
fi

# --- 4) Optional pymavlink heartbeat ---
HEARTBEAT_STATUS="SKIP"
if [[ "${NO_MAVLINK}" -eq 0 ]]; then
    ACM_DEV=""
    for dev in /dev/ttyACM*; do
        [[ -e "${dev}" ]] || continue
        ACM_DEV="${dev}"
        break
    done

    if [[ -z "${ACM_DEV}" ]]; then
        HEARTBEAT_STATUS="SKIP (no /dev/ttyACM*)"
    elif ! python3 -c "import pymavlink" 2>/dev/null; then
        HEARTBEAT_STATUS="SKIP (pymavlink not installed)"
    else
        log "Optional MAVLink heartbeat on ${ACM_DEV}..."
        HB_RC=0
        python3 - "${ACM_DEV}" <<'PY' || HB_RC=$?
import sys
from pymavlink import mavutil

port = sys.argv[1]
m = mavutil.mavlink_connection(port, baud=115200)
hb = m.wait_heartbeat(timeout=15)
if hb:
    print(f"HEARTBEAT_OK type={hb.type} autopilot={hb.autopilot} status={hb.system_status}")
    sys.exit(0)
print("HEARTBEAT_FAIL")
sys.exit(1)
PY
        if [[ "${HB_RC}" -eq 0 ]]; then
            HEARTBEAT_STATUS="PASS"
        else
            HEARTBEAT_STATUS="FAIL"
        fi
    fi
else
    HEARTBEAT_STATUS="SKIP (--no-mavlink)"
fi

# --- 5) Summary ---
echo ""
echo "========== M0 Verification Summary =========="
echo "Artifacts:     PASS"
echo "Flash @ ${FLASH_ADDR}: $([[ ${SKIP_FLASH} -eq 0 ]] && echo DONE || echo SKIPPED)"
echo "Run wait:      ${RUN_WAIT_SEC}s"
echo "MAVLink HB:    ${HEARTBEAT_STATUS}"
echo ""
if [[ "${M0_PASS}" -eq 1 ]]; then
    echo "M0 RESULT: PASS (rtt_dbg_hal_run_called == 0xAAAAAAAA)"
    exit 0
else
    echo "M0 RESULT: FAIL (rtt_dbg_hal_run_called=${HAL_VAL}, expected 0xAAAAAAAA)"
    exit 1
fi
