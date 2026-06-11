# RTT USB CDC Root Cause: ChibiOS SerialUSB vs RT-Thread CherryUSB

This note records the current continue019 root-cause model for CUAV V5 RTT
USB CDC parameter download, MAVLink FTP, SDCard, and peripheral streaming.

## Executive Summary

The last reproduced failure was not a low-level USB byte-loss failure.
RTT/CherryUSB delivered all parameters, MAVLink FTP stayed valid, SDCard
operations passed, and IMU/attitude/barometer/EKF/SYS_STATUS streams remained
healthy.  The failure was an intermittent `PARAM_VALUE` production gap:
one parameter interval reached 322 ms even though the complete list still
arrived.

The current evidence points to a system timing bug as the active root cause:
the old RTT `AP_HAL::micros64()` mixed `rt_tick_get()` with
`DWT_CYCCNT % tick_period_us`.  DWT is free-running and not phase-locked to the
RT-Thread tick, so the composed timestamp could move backwards near tick
boundaries.  That violates a core ArduPilot assumption: scheduler and GCS
budget math expect `micros()` / `micros64()` to be monotonic.

ChibiOS avoids this class of failure because its `hrt_micros64()` is derived
from ChibiOS' monotonic timestamp API (`chVTGetTimeStampI()`) under lock.  Its
SerialUSB path also keeps cleaner queue ownership boundaries, but the final
RED/GREEN evidence shows that the remaining 322 ms gap was explained by RTT
time-source reversal, not by EP1 transfer loss.

## Reproduced Failure

Reliable RED gate:

```text
results/master_launch/ml_20260611_072954_continue019_final_gate/multiround_reliable
multiround_reliable.rc = 2
```

The failing round:

```text
round 3:
  PARAM duration = 1.386 s
  PARAM coverage = 912/912
  missing = 0
  duplicates = 0
  max inter-parameter gap = 0.322 s
  gap location = index 132 COMPASS_OPTIONS -> index 133 COMPASS_DEV_ID4
  raw MAVLink FTP = PASS
  strict MAVLink FTP/SD = PASS
  peripheral gate = PASS
```

This shape matters.  If USB packets were being dropped, corrupted, or trapped
inside a dead endpoint state, we would expect missing parameters, stale FTP
replies, short writes, CherryUSB recovery, or DWC2 residue.  Instead, the bytes
arrived; parameter production was delayed.

## RED Counter Evidence

Counter report:

```text
results/master_launch/ml_20260611_072954_continue019_final_gate/counter_decode_after_reliable_red.md
```

Key RED counters:

| Area | Evidence | Value |
|---|---|---:|
| PARAM | request-list count | 6 |
| PARAM | completed lists | 6 |
| PARAM | stream sent total | 5472 |
| PARAM | max send gap | 322 ms |
| PARAM | large send-gap count | 1 |
| PARAM | large gap before/after index | 132 / 133 |
| PARAM | tx-buffer breaks | 0 |
| UART | write wait ms | 0 |
| UART | write no-space / short writes | 0 / 0 |
| CherryUSB | bulk-IN callbacks | 11521 |
| CherryUSB | start failures | 0 |
| CherryUSB | armed / completed bytes | 484044 / 484044 |
| CherryUSB | ring dropped | 0 |
| CherryUSB | completion assumed | 0 |
| CherryUSB | EPDIS recovery | 0 |
| DWC2 | start_write / XFRC / complete | 11521 / 11521 / 11521 |
| DWC2 | incomplete / deferred / residue paths | 0 / 0 / 0 |
| IOMCU | status error resets | 0 |

The most important RED value was:

```text
rtt_dbg_gcs_param_max_stream_elapsed_us = 0xffffffc4
```

Interpreted as signed 32-bit elapsed time, `0xffffffc4` is `-60 us`.  That is
direct evidence that a `micros()` elapsed-time calculation moved backwards.
The same run also had low-level USB conservation, so the failure sits above
USB transfer completion.

## Why The Old RTT Timebase Failed

The old RTT model was approximately:

```cpp
tick_us = rt_tick_get() * 1000000 / tick_hz;
sub_us = (DWT_CYCCNT / cpu_mhz) % tick_period_us;
return tick_us + sub_us;
```

This is attractive because it appears to combine a coarse OS tick with a fine
sub-tick counter.  It is not valid unless both counters share phase.  Here they
do not:

- `rt_tick_get()` advances at the RT-Thread scheduler tick.
- `DWT_CYCCNT` is a free-running CPU cycle counter.
- `(DWT_CYCCNT % tick_period)` is not the elapsed time since the last RT tick.
- Near tick boundaries, `tick_us` can stay constant while `sub_us` wraps down,
  or `tick_us` can step while `sub_us` has an unrelated phase.

That can make `AP_HAL::micros64()` non-monotonic.  Once time moves backwards,
several ArduPilot feedback loops become mathematically unsafe:

- `AP_Scheduler` run-time availability and delay loops
- `GCS_MAVLINK::update_send()` time budget
- parameter stream quantum and out-of-time checks
- MAVLink pacing around FTP and background streams
- driver wait/timeout logic that uses `AP_HAL::micros() - start`

This is the system-level reason the last problem looked like USB slowness while
the USB counters were clean.

## Why ChibiOS Did Not Hit This Failure

ChibiOS has two advantages here.

First, its high-resolution timebase is monotonic.  `AP_HAL_ChibiOS::micros64()`
delegates to `hrt_micros64()`, which reads ChibiOS' timestamp source
(`chVTGetTimeStampI()`) under the appropriate system or ISR lock.  It does not
reconstruct time by mixing an unsynchronized scheduler tick with a modulo of a
free-running CPU counter.

Second, ChibiOS SerialUSB has mature queue ownership semantics:

- `UARTDriver::txspace()` reports AP `_writebuf` space to GCS scheduling.
- `UARTDriver::_write()` queues bytes into the AP HAL buffer.
- `_flush()` services SerialUSB with `sduSOFHookI()`.
- `write_pending_bytes_NODMA()` uses `chnWriteTimeout(..., TIME_IMMEDIATE)`,
  so USB service does not wait for host completion.
- `sduDataTransmitted` releases SerialUSB-owned output buffers from the USB IN
  completion path.

Those queue semantics are still important.  RTT had to rebuild equivalent
behavior with a CherryUSB TX ring, an aggregate `cdc_tx_buf`, EPENA guards,
completion-owned ring discard, ZLP handling, and DWC2 completion counters.
However, after those lower layers became byte-conserving, the final intermittent
PARAM gap remained until the RTT timebase was made monotonic.

## RTT/CherryUSB Data Path

Current RTT USB CDC transmit path:

```text
MAVLink
 -> AP_HAL_RTT::UARTDriver::_writebuf
 -> UARTDriver::_drain_writebuf_to_dev()
 -> CherryUSB 64-byte TX ring slots
 -> shared cdc_tx_buf aggregate, up to 256 bytes
 -> usbd_ep_start_write()
 -> DWC2 DIEPTSIZ/DIEPCTL + TX FIFO
 -> XFRC interrupt
 -> usbd_cdc_acm_bulk_in()
 -> discard completed inflight ring slots and allow cdc_tx_buf reuse
```

The current implementation intentionally mirrors ChibiOS where it matters:

- AP-level `txspace()` reports `_writebuf` space.
- CherryUSB does not overwrite the shared aggregate buffer while EPENA says
  DWC2 still owns it.
- Ring slots are released only after bulk-IN completion.
- Full-size final packets arm a ZLP so the host TTY layer is not left waiting
  for a short packet.
- MAVLink header, payload, and checksum fragments are flushed after unlock.

This lower layer is not considered irrelevant; it is required for a safe USB
transport.  The current evidence simply says it was no longer the active cause
of the final 322 ms parameter gap.

## Fix

Code fix:

```text
libraries/AP_HAL_RTT/Util.cpp
```

`Util::get_micros64()` now uses a DWT delta accumulator under interrupt lock:

- seed the epoch once from `rt_tick_get()`
- store `last_cyc`
- accumulate unsigned DWT cycle deltas
- convert cycle deltas to microseconds with `SystemCoreClock`
- retain `fractional_cycles`
- never compose time from `rt_tick_get()` plus `DWT % tick_period`

This makes the RTT microsecond clock monotonic across RT tick boundaries and
matches the ChibiOS hrt property that ArduPilot expects.

## GREEN Verification

Latest monotonic-time GREEN gate:

```text
results/master_launch/ml_20260611_074609_continue019_monotonic_time_gate
build.rc = 0
flash.rc = 0
heartbeat.rc = 0
multiround_usb_gate.rc = 0
status.md Verdict = GREEN
```

Runtime result:

| Round | PARAM duration | Max gap | PARAM | Raw FTP | FTP/SD | Peripheral |
|---:|---:|---:|---|---|---|---|
| 1 | 1.244 s | 27.2 ms | PASS | PASS | PASS | PASS |
| 2 | 1.196 s | 29.4 ms | PASS | PASS | PASS | PASS |
| 3 | 1.273 s | 56.1 ms | PASS | PASS | PASS | PASS |

Peripheral coverage in the gate included:

- `RAW_IMU`
- `ATTITUDE`
- `SCALED_PRESSURE`
- `EKF_STATUS_REPORT`
- `SYS_STATUS` with error counters zero
- magnetometer fields present in `RAW_IMU`
- `BAD_DATA` count zero
- strict SD read/write/delete through MAVLink FTP

SD/FTP readiness did need several startup attempts before the final gate began:

```text
sd_ready_attempt1.json ... sd_ready_attempt6.json
```

That is not a failure of the final rounds, but it remains a startup readiness
jitter to keep visible.

## GREEN Counter Evidence

Counter report:

```text
results/master_launch/ml_20260611_074609_continue019_monotonic_time_gate/counter_decode_after_green.md
```

Key GREEN counters:

| Area | Evidence | Value |
|---|---|---:|
| Scheduler | wait-sample max | 16871 us |
| Scheduler | wait-sample large count | 0 |
| PARAM | request-list count | 3 |
| PARAM | completed lists | 3 |
| PARAM | stream sent total | 2736 |
| PARAM | max send gap | 56 ms |
| PARAM | large send-gap count | 0 |
| PARAM | max stream elapsed | 5199 us |
| PARAM | tx-buffer breaks | 0 |
| PARAM | delay pump calls | 371 |
| UART | write wait ms | 0 |
| UART | write no-space / short writes | 0 / 0 |
| CherryUSB | bulk-IN callbacks | 6503 |
| CherryUSB | start failures | 0 |
| CherryUSB | armed / completed bytes | 299083 / 299083 |
| CherryUSB | ring dropped | 0 |
| CherryUSB | completion assumed | 0 |
| CherryUSB | EPDIS recovery | 0 |
| DWC2 | start_write / XFRC / complete | 6503 / 6503 / 6503 |
| DWC2 | incomplete / deferred / residue paths | 0 / 0 / 0 |
| IOMCU | status error resets | 0 |

The decisive change is that the underflowed PARAM elapsed-time counter
disappeared and the max PARAM send gap dropped from 322 ms to 56 ms while
CherryUSB/DWC2 byte conservation stayed intact.

## Code-Level ChibiOS vs RTT Matrix

The important differences are visible in source, not just in the runtime
numbers:

| Concern | ChibiOS implementation | RTT/CherryUSB implementation | Why it matters |
|---|---|---|---|
| Timebase | `libraries/AP_HAL_ChibiOS/hwdef/common/hrt.c`: `hrt_micros64()` reads `chVTGetTimeStampI()` under system/ISR lock. | `libraries/AP_HAL_RTT/Util.cpp`: `get_micros64()` now uses a DWT delta accumulator under `rt_hw_interrupt_disable()`. | ArduPilot scheduler and GCS budget math require monotonic time.  This was the active missing invariant in RTT. |
| AP transmit backpressure | `libraries/AP_HAL_ChibiOS/UARTDriver.cpp`: `txspace()` returns `_writebuf.space()`. | `libraries/AP_HAL_RTT/UARTDriver.cpp`: RTT also reports AP `_writebuf` space for USB. | GCS should see AP queue capacity, not raw endpoint FIFO capacity. |
| USB service entry | ChibiOS `_flush()` calls `sduSOFHookI()` for USB; SOF handler also calls `sduSOFHookI()`. | RTT `_flush()` / timer paths drain AP `_writebuf` into the CherryUSB ring and kick EP1 when safe. | ChibiOS hides endpoint timing below SerialUSB queues; RTT has to rebuild the same decoupling explicitly. |
| Nonblocking USB write | ChibiOS `write_pending_bytes_NODMA()` calls `chnWriteTimeout(..., TIME_IMMEDIATE)`. | RTT USB write path records wait/short/no-space counters and avoids host-completion waits; GREEN shows wait/short/no-space stayed zero. | The MAVLink producer must not block on host-side USB completion. |
| IN completion ownership | `libraries/AP_HAL_ChibiOS/hwdef/common/usbcfg.c`: EP1 bulk-IN completion uses `sduDataTransmitted`. | `libraries/AP_HAL_RTT/hal_usb_cherryusb_shim.c`: `usbd_cdc_acm_bulk_in()` releases inflight CherryUSB ring slots and allows `cdc_tx_buf` reuse. | A buffer is reusable only after the USB completion callback owns the release decision. |
| Endpoint-idle guard | SerialUSB/ChibiOS driver owns endpoint state internally. | RTT checks EPENA before copying the next aggregate into `cdc_tx_buf` or calling `usbd_ep_start_write()`. | Prevents overwriting the shared aggregate buffer while DWC2 still owns it. |
| Transfer completion edge | ChibiOS OTG/SerialUSB treats XFRC as the completion edge for the class queue. | RTT DWC2 code tracks EP1 `start_write`, `XFRC`, and `complete`; it has defensive handling for early/residue cases. | Final GREEN counters show start/XFRC/complete were conserved, so this layer was safe but not the final active root cause. |
| Packet scale | ChibiOS SerialUSB effectively provides a multi-buffer queue below AP `_writebuf`. | RTT keeps 64-byte ring slots and aggregates up to 256 bytes before arming EP1. | Reduces re-arm churn for PARAM/FTP bursts and makes RTT closer to ChibiOS queue scale. |

The mistake in earlier reasoning was treating these USB queue differences as
the only possible core issue.  They were real and worth fixing, but the final
RED evidence separated the layers:

```text
USB transport conservation: good
UART producer wait/short paths: inactive
PARAM elapsed-time arithmetic: underflowed
```

That combination means the last visible symptom was caused by broken time
feedback above the USB transport, not by an active CherryUSB/DWC2 byte-transfer
failure.

## What Is Proven vs Not Proven

Proven by the current evidence:

- The old RTT timebase could move backwards.
- That reversal was visible inside firmware counters during a real PARAM gap.
- Low-level USB transmit completion was conserved during the same RED run.
- After the monotonic DWT accumulator fix, the reliable 3-round USB gate passed.
- PARAM download, MAVLink FTP, strict SD read/write/delete, and core peripheral
  streams passed in the GREEN gate.

Not proven:

- The DWC2 early-XFRC defensive branches were the final root cause.  Their
  counters stayed zero in the GREEN run, so they are defensive guards, not the
  demonstrated active fix.
- A single 3-round gate proves indefinite soak stability.  It proves the
  current gate, not all future host or timing conditions.
- SD readiness is perfectly instantaneous after boot.  The final gate passed,
  but startup readiness jitter is still documented.

## Remaining Cleanup Risk

The current tree still contains many debug counters and trace arrays that were
useful for isolating the failure.  Before a final GitHub push, the diff should
be reviewed so the final commit includes only:

- behavior needed for correctness and stable timing
- diagnostic counters that are intentionally retained
- root-cause documentation
- reproducible validation evidence or manifests

Do not remove diagnostic evidence from the source until a post-cleanup build,
flash, 3-round USB gate, and OpenOCD counter snapshot have passed again.
