# CUAV V5 RTT Acceptance Current State

Updated: 2026-06-13 (Asia/Shanghai)

This note is the current evidence map for the CUAV V5 RT-Thread ArduPilot
bring-up. It records what is verified in this worktree, what is still not
claimed, and which old runs were intentionally superseded. The strict rule for
this file is closed-loop evidence: do not replace a measured result with a
wishful conclusion.

## Current Verdict

Driver-level software acceptance is GREEN on the current firmware:

- USB CDC parameter download is complete and fast: `942/942` parameters in
  `1.601 s`, `588.3 params/s`, no missing parameters.
- MAVLink FTP is GREEN, including `/`, `/APM`, SD create/read/remove, and
  `@PARAM/param.pck` decode integrity.
- `PARAM_SET` persistence through OpenOCD reset is GREEN.
- DataFlash log list/download/restore is GREEN.
- IMU/INS, magnetometer, barometer, and logging driver health are GREEN.
- OpenOCD/GDB fault-register sampling after full acceptance shows no HardFault
  (`CFSR=0`, `HFSR=0`, `VTOR=0x08008000`) and the target resumed cleanly.
- USB SLCAN standard CAN, DroneCAN, and CDC+SLCAN coexistence are GREEN on the
  bench-proven `CAN_SLCAN_CPORT=2` path.

The current accepted suite is:

```text
results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/acceptance_suite.json
verdict=GREEN
reason=acceptance_suite_ok
```

Strict AHRS/pre-arm health is still RED because the board has not completed the
physical accel/compass/safety calibration state needed for real pre-arm. This
is not a driver failure in the latest gate: `driver_verdict=GREEN` while
`calibration_verdict=RED`. Do not claim physical flight/pre-arm readiness until
strict pre-arm is GREEN without the allowed calibration exception.

## Accepted Fixes

The accepted fix is the combination below; earlier partial fixes are not enough
by themselves.

1. USB CDC / MAVLink throughput and latency:
   - Preserve CDC endpoint ownership and do not reuse aggregate buffers while
     DWC2 still owns an IN transfer.
   - Handle ZLP on full-size terminal CDC packets.
   - Keep MAVLink FTP responsive by using UART-class priority and a small burst
     pacing floor.
   - Normalize negative RTT errno values before MAVFTP maps them to MAVLink FTP
     errors.
   - Run verification gates serially on the CDC endpoint to avoid false RED
     results from competing pymavlink readers.

2. Scheduler / logger fairness:
   - Keep `log_io` at normal IO priority.
   - Make the ArduPilot main loop periodically yield one real RT-Thread tick so
     USB, FTP, storage, and logger liveness can make progress.
   - Do not use the older `log_io` main-priority experiment as the final fix; it
     proved logger heartbeat starvation, then caused a fresh
     `PreArm: Main loop slow (345Hz < 400Hz)` regression.

3. IST8310 / I2C reset-run stability:
   - Treat MCU-only OpenOCD reset as a noisy plant state, not a fresh sensor
     power-on.
   - Pull `VDD_3V3_SENSORS_EN` (`PE3`) low early in board init.
   - Clamp the internal compass I2C3 lines (`PH7`/`PH8`) low while the sensor
     rail is held off so IST8310 is not weak-powered through I2C protection
     paths.
   - Use a deterministic rail cycle of `1800 ms` off and `900 ms` settle before
     probes.
   - Enable ChibiOS-style I2C clear-on-timeout by default.
   - During bus clear, release SDA as input/pull-up, pulse SCL up to 20 times,
     generate STOP, and restore AF open-drain mode.
   - Recover the STM32 I2C peripheral and GPIO state after timeout/error, then
     retry the IST8310 probe budget.

## Current Evidence

| Requirement | Evidence | Result |
|---|---|---|
| Build | `results/execution/build_20260613T085730Z_align_current_worktree/build.json` | `rc=0`; current worktree built with SCons |
| Flash | `results/execution/flash_20260613T085836Z_align_current_worktree/flash.json` | `rc=0`; OpenOCD program/verify OK at `0x08008000` |
| Full acceptance suite | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/acceptance_suite.json` | GREEN, `acceptance_suite_ok`; strict pre-arm RED allowed as calibration-only exception |
| Fast USB CDC parameter download | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/param_download/param_download.json` | `942/942`, `1.601 s`, `588.3 params/s`, missing `0` |
| Parameter persistence | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/param_persist/param_persist.json` | GREEN, `param_persist_restore_ok` |
| MAVLink FTP | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/mavftp/mavftp_gate.json` | GREEN, `/` and `/APM` list OK, SD write/read/remove OK, post-FTP heartbeats stable |
| MAVLink FTP packed params | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/mavftp/mavftp_gate.json` | `@PARAM/param.pck` decoded: `decoded_count=942`, `num_params=942`, `total_params=942` |
| Peripheral driver health | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/peripheral_driver/peripheral_health.json` | `driver_verdict=GREEN`, `calibration_verdict=RED`; accel/gyro/baro/mag/logging healthy |
| Magnetometer data | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/peripheral_driver/peripheral_health.json` | `mag.healthy=true`, `mag_norm_mgauss=382.9` |
| DataFlash logging | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/log_download/log_download_gate.json` | GREEN, downloaded `94208` bytes, SHA256 `b0087c044e97fb5fdc56b28100558b55ca70ed07e018d6becaccc35ca13a7c44` |
| Strict pre-arm health | `results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/peripheral_strict_prearm/peripheral_health.json` | RED, `driver_verdict=GREEN`, `calibration_verdict=RED`; physical readiness not claimed |
| OpenOCD/GDB fault sample | `results/execution/gdb_fault_20260613T090506Z_after_full_acceptance/gdb.log` | No HardFault: `CFSR=0`, `HFSR=0`, `VTOR=0x08008000`; PC in UART service thread |
| USB SLCAN standard CAN | `results/execution/slcan_20260613T090627Z_ascii_after_full_acceptance/slcan_ascii_gate.json` | GREEN, `slcan_ascii_bidir_ok` |
| DroneCAN / pydronecan | `results/execution/dronecan_20260613T091439Z_after_full_acceptance_fixed_device/pydronecan.json` | GREEN, 9 `NodeStatus` events from node `10`, `health=0`, `mode=0` |
| CDC + SLCAN coexistence | `results/execution/coexist_20260613T091529Z_after_full_acceptance/coexist_summary.json` | GREEN; CAN traffic active, CDC params `942/942` in `1.944 s`, MAVFTP GREEN |
| Direct OpenOCD reset-run peripheral stability | `results/execution/reset_stability_20260612T204147Z_i2c_clear_chibios_style/summary.txt` | `5/5` GREEN; mag healthy ratio `1.0` every round; mag norm `411.4/414.8/412.6/411.3/410.6` |
| Single reset after final I2C clear | `results/execution/peripheral_health_20260612T204039Z_i2c_clear_chibios_style_after_reset/peripheral_health.json` | GREEN, `driver_verdict=GREEN`, `mag.healthy=true`, mag norm about `412 mG` |
| Post-documentation/hygiene reset check | `results/execution/peripheral_health_20260612T205450Z_post_docs_hygiene_final_reset/peripheral_health.json` | GREEN after OpenOCD `reset run`; accel/gyro/baro/mag/logging healthy ratio `1.0`; `mag_norm_mgauss=407.2`; strict calibration still RED |

## Superseded Or Negative Evidence

These runs are important because they prevent us from over-claiming a partial
fix:

| Observation | Evidence | Meaning |
|---|---|---|
| Short sensor rail cycle was insufficient | `results/execution/peripheral_health_20260612T195623Z_after_ahrs_compass_not_healthy/peripheral_health.json`, `results/execution/gdb_ist8310_20260612T195741Z_live_after_mag_zero/gdb.log` | The `100 ms` off / `600 ms` settle variant could still produce mag zero and IST8310 WHOAMI read failure |
| Early hold-off `800/600` was insufficient | `results/execution/peripheral_health_20260612T201608Z_port_wait_after_openocd_reset`, `results/execution/gdb_ist8310_20260612T201744Z_live_after_early_holdoff_mag_zero/gdb.log` | A later direct reset contradicted the earlier green suite |
| I2C clamp without ChibiOS-style clear was insufficient | `results/execution/reset_stability_20260612T202611Z_i2c_clamp_1800_900/summary.txt`, `results/execution/gdb_ist8310_20260612T203715Z_live_after_i2c_clamp_reconnect_mag_zero/gdb.log` | Mixed reset results; live GDB showed IST8310 probe failed and `PH8`/SDA stuck low |
| GDB halt can disturb live CDC state | `results/execution/openocd_resume_20260612T203307Z_after_live_gdb_no_heartbeat`, `results/execution/heartbeat_20260612T203319Z_after_openocd_resume` | If GDB leaves the target halted, resume/reset before judging CDC loss as firmware failure |

## Why ChibiOS Worked And RTT Needed System Fixes

The failure was not one isolated USB byte-loss bug. It was a system mismatch
between ArduPilot assumptions and the early RTT/CherryUSB port.

ChibiOS works because its mature ArduPilot port already satisfies several
assumptions at the same time:

- SerialUSB buffer ownership and endpoint completion ownership are distinct.
- USB service, GCS/MAVLink work, logger work, and control-loop work have known
  scheduling relationships.
- The HAL timebase is monotonic for send budgets, driver timeouts, and liveness
  checks.
- Board reset/startup sequencing has been exercised around external sensors,
  including I2C recovery and reset timing.

RTT originally violated those assumptions in several small ways:

- CDC TX could reuse memory while DWC2 still owned the USB IN transfer.
- Full-size CDC packets could leave the host waiting without ZLP completion
  behavior.
- MAVFTP could be delayed behind parameter, UART, scheduler, or logger work.
- MAVFTP burst reads could outrun CDC completion unless paced slightly.
- RTT errno sign conventions could map filesystem errors incorrectly.
- Multiple verifier scripts on one CDC endpoint could create artificial data
  starvation.
- The main loop's sub-tick busy-wait behavior could starve lower-priority
  logger/storage liveness even though the underlying SD writes were successful.
- MCU-only OpenOCD reset did not power-cycle IST8310, so the sensor and I2C bus
  could remain in a stale or weak-powered state; later GDB showed `PH8`/SDA
  stuck low.

The final root-cause model:

```text
RTT needed to be made boring in the same places ChibiOS is boring.

USB/FTP became stable when buffer ownership, endpoint completion, ZLP handling,
worker priority, errno mapping, CDC pacing, and single-reader verification were
all aligned with ArduPilot's expectations.

Logging became stable when the main loop yielded a real RT-Thread tick often
enough for lower-priority storage/logger liveness to run, instead of hiding
behind successful low-level SD writes.

Magnetometer reset-run stability became stable only after treating MCU reset as
a partially unknown plant state: hold the sensor rail off early, clamp I2C3
while the rail is off, give the external device a deterministic rail-off/settle
window, and use ChibiOS-style I2C bus/peripheral recovery when SDA is stuck.
```

## Current Gate Scripts

Run individual gates with `nohup timeout` when executing manually. The preferred
wrapper is serial on purpose, so only one process owns the CDC endpoint:

```bash
TS=$(date -u +%Y%m%dT%H%M%SZ)
DIR="results/execution/acceptance_${TS}_cuav_v5_rtt"
mkdir -p "$DIR"
nohup timeout 900s python3 Tools/scripts/rtt_acceptance_suite.py \
  --port /dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00 \
  --outdir "$DIR" \
  --include-param-persist \
  --strict-prearm-health \
  --allow-strict-prearm-red \
  > "$DIR/stdout.log" 2>&1 &
echo "$!" > "$DIR/pid"
```

Use `--strict-prearm-health` without `--allow-strict-prearm-red` only after the
board has completed the required physical accel/compass/safety calibration and
the expected outcome is full GREEN.

## Workspace Hygiene

Root-level generated test artifacts are not deleted. They are moved to
`results/recycle_bin/<timestamp>_<reason>/` with a manifest. The same rule is
used for superseded execution directories.

Latest cleanup:

```text
results/recycle_bin/20260612T205248Z_superseded_execution_after_i2c_clear_final/
```

That recycle directory moved superseded green suites, old one-off gates, and
old build/flash/runtime artifacts that were replaced by the final
`i2c_clear_chibios_style` evidence. The active `results/execution/` directory is
kept focused on:

- final build/flash/acceptance/reset-stability evidence;
- final USB SLCAN, DroneCAN, and CDC+SLCAN coexistence evidence;
- red or mixed diagnostics needed to explain why partial fixes were rejected;
- short GDB/heartbeat artifacts needed to interpret reset and halt behavior.

Each recycle directory contains `manifest.tsv` and `summary.json`. Evidence is
retained, only relocated.
