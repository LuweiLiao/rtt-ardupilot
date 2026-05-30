# M7 ChibiOS vs RTT Performance Baseline Plan

Status: **framework only** — no ChibiOS firmware is flashed in this phase; no hardware claims.

## Purpose

Per `rtt-functional-baseline-governance` **R2 / Prohibited Claims**:

> **接近 ChibiOS 性能/语义** → 所需证据：ChibiOS 同板同脚本 A/B 数据

RTT functional baseline (MAVFTP 6/6, param readable, CFSR=0) is locked on CMSIS SPI bypass.
Performance gaps (param download variance, stream rates, cpu_idle under load) are **known gaps**, not correctness blockers.
This plan defines **how** to collect comparable numbers before any optimization or LLD activation is justified.

## Phases

### Phase 1 — RTT-only (current)

| Item | Rule |
|------|------|
| Stack | `--stack rtt` only |
| Hardware | Single hardware agent, serial (OpenOCD ↔ CDC ↔ UART7 never parallel) |
| Goal | Establish RTT baseline JSON with min/max/median/p95/failure_count per metric |
| Output | `results/perf/rtt_<timestamp>.json` matching `docs/perf/schema.json` |
| Forbidden | Claiming parity with ChibiOS; flashing ChibiOS without Phase 2 gate |

**Entry command (scaffold / dry-run, no CDC):**

```bash
python3 tools/perf/run_chibios_rtt_baseline.py \
  --stack rtt --phase 1 --dry-run --rounds 3 \
  --out /tmp/rtt_baseline_scaffold.json
```

**Hardware measurement entry (Phase 1 live — hardware agent only):**

```bash
# Preconditions: pgrep openocd empty; RTT ArduCopter flashed; SD inserted; CDC enumerated
python3 tools/perf/run_chibios_rtt_baseline.py \
  --stack rtt --phase 1 --rounds 5 \
  --port /dev/serial/by-id/usb-ArduPilot_CUAVv5_RTT_*-if00 \
  --uart7 /dev/serial/by-id/usb-1a86_USB_Single_Serial_*-if00 \
  --out results/perf/rtt_$(date -u +%Y%m%dT%H%M%SZ).json
```

> Live probes will delegate to existing regression scripts (table below). The runner skeleton wires IDs and JSON shape first.

### Phase 2 — ChibiOS A/B (future, gated)

| Item | Rule |
|------|------|
| Authorization | `--force-chibios` **and** explicit waiver that B1 / functional baseline conflicts are closed |
| Same conditions | **Same board**, **same wiring**, **same SD card**, **same param set** (document hash or param snapshot ID) |
| Execution | **Serial hardware only** — flash RTT → measure → flash ChibiOS → measure; never two stacks concurrently |
| Comparison | Paired runs; delta on median/p95; functional gates must pass on both stacks before perf deltas matter |

**Entry command (blocked until authorized):**

```bash
python3 tools/perf/run_chibios_rtt_baseline.py \
  --stack chibios --phase 2 --force-chibios --rounds 5 \
  --port /dev/ttyACM1 --uart7 /dev/serial/by-id/usb-1a86_* \
  --out results/perf/chibios_$(date -u +%Y%m%dT%H%M%SZ).json
```

Default behavior without `--force-chibios`: exit code **2** with authorization message.

## Performance statistics (required)

Every **performance-class** metric in the JSON report MUST include:

| Field | Meaning |
|-------|---------|
| `min` | Best successful sample across rounds |
| `max` | Worst successful sample |
| `median` | 50th percentile of successful samples |
| `p95` | 95th percentile of successful samples |
| `failure_count` | Rounds that failed threshold or errored |

Functional pass/fail gates (e.g. MAVFTP 6/6) additionally record `functional_gates.*.passed` and `failure_count`.

Boolean metrics (mission protocol) use `min_ok` threshold 1.0; failed rounds increment `failure_count`.

## Metric → command mapping

Existing regression scripts are the **source of truth** for probe semantics. Do not reimplement FTP/mission logic inside the perf runner until the hardware agent wires subprocess calls.

| Metric ID | Kind | Unit | Source command / script | Notes |
|-----------|------|------|-------------------------|-------|
| `param_download_sec` | duration | s | `python3 tests/test_full_functional.py --port $PORT` (T3) | Expect ≥900 params; record wall time |
| `mavftp_pass_count` | count | tests | `python3 tests/test_mavftp.py --port $PORT` | Milestone gate: 6/6 pass |
| `attitude_hz` | rate | Hz | `python3 tests/test_mavlink_rates.py --port $PORT` | Default stream + optional SET_MESSAGE_INTERVAL |
| `raw_imu_hz` | rate | Hz | same | RAW_IMU window |
| `sys_status_hz` | rate | Hz | same | SYS_STATUS window |
| `heartbeat_hz` | rate | Hz | `tests/test_full_functional.py` T1 | 5 s window |
| `mission_protocol_ok` | boolean | pass | `python3 tests/test_mission_protocol.py --port $PORT` | Upload/download/clear |
| `cpu_idle_pct` | idle_pct | % | UART7: `ap_rate` (see command-catalog) | OpenOCD must be **off** during sample |
| `main_loop_hz` | rate | Hz | UART7: `ap_rate` → `loop_hz` | Same session as cpu_idle |

Reference catalog: `.cursor/project/command-catalog.md` (MAVProxy/pymavlink, UART7 msh, OpenOCD mutual exclusion).

## JSON schema

- File: `docs/perf/schema.json`
- Version: `1.0.0`
- Validate: `python3 -m json.tool docs/perf/schema.json`

Reports MUST set `schema_version`, `run.stack`, `run.phase`, and populate every registered metric with a `summary` block even when samples are empty (scaffold phase).

## Hardware agent workflow (serial)

```text
1. Verify OpenOCD not running (pgrep openocd)
2. Flash target stack (RTT or ChibiOS) — outside this script
3. Wait CDC enumerate ≥12s after reset
4. Run perf runner OR constituent tests; capture JSON
5. Optional UART7 ap_rate between CDC sessions (close pymavlink first)
6. Release board before next stack flash
```

**Mutual exclusion (mandatory):**

- OpenOCD halt/GDB and CDC pymavlink **never** overlap for load-sensitive metrics (`cpu_idle_pct`, param download under debug).
- Document in report `run.notes` if a metric could not be sampled due to tooling conflict.

## Repository layout

```text
docs/perf/
  chibios_vs_rtt_perf_plan.md   # this file
  schema.json                   # JSON Schema draft-07
tools/perf/
  run_chibios_rtt_baseline.py   # argparse runner skeleton
results/perf/                   # gitignored; hardware agent writes JSON here
```

## Out of scope (this milestone)

- Building or flashing ChibiOS firmware
- OpenOCD / GDB automation inside the runner
- Modifying AP_HAL_RTT or HAL-above production code
- Declaring RTT "close to ChibiOS" without Phase 2 paired data

## Decision linkage

- SPI LLD activation remains **archived** until M7 A/B proves SPI path is the bottleneck (`open-issues.md`, `rtt-functional-baseline-governance` R1–R3).
- Param slowness: prioritize GCS/MAVLink/USB scheduling evidence over SPI until measured otherwise.

## Changelog

| Date | Change |
|------|--------|
| 2026-05-30 | Initial framework: plan, schema v1.0.0, runner skeleton |
