#!/usr/bin/env bash
# Actuator: compile + OpenOCD flash (releases ST-Link on exit).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
BIN="${ROOT}/build/rtt_cuav_v5/rtthread.bin"
CFG="${ROOT}/Tools/debug/openocd-f7.cfg"
ADDR="0x08008000"
JOBS="${JOBS:-$(nproc)}"

log() { printf '[actuator_flash] %s\n' "$*"; }

cd "${ROOT}"
log "Building ArduCopter cuav_v5..."
scons --v=ArduCopter --target=cuav_v5 -j"${JOBS}"

[[ -f "${BIN}" ]] || { log "missing ${BIN}"; exit 1; }

log "Flashing ${BIN} -> ${ADDR}"
openocd -f "${CFG}" \
  -c "init" \
  -c "reset halt" \
  -c "program ${BIN} ${ADDR} verify" \
  -c "reset run" \
  -c "exit"
log "Done — ST-Link released"
