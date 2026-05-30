#!/usr/bin/env bash
# Actuator: flash bootloader + app, reset run, release ST-Link.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
BL="${ROOT}/Tools/bootloaders/CUAVv5_bl.bin"
BIN="${ROOT}/build/rtt_cuav_v5/rtthread.bin"
CFG="${ROOT}/Tools/debug/openocd-f7.cfg"
BL_ADDR="0x08000000"
APP_ADDR="0x08008000"

log() { printf '[actuator_flash_full] %s\n' "$*"; }

[[ -f "${BL}" ]] || { log "missing bootloader ${BL}"; exit 1; }
[[ -f "${BIN}" ]] || { log "missing app ${BIN}"; exit 1; }

log "Programming BL ${BL} -> ${BL_ADDR}"
log "Programming app ${BIN} -> ${APP_ADDR}"
openocd -f "${CFG}" \
  -c "init" \
  -c "reset halt" \
  -c "program ${BL} ${BL_ADDR} verify" \
  -c "program ${BIN} ${APP_ADDR} verify" \
  -c "reset run" \
  -c "exit"
log "Done — wait ≥12s (bootloader 5s + init) before sampling"
