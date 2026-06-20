# Windows USB Completion Matrix

Updated: 2026-06-18 (Asia/Shanghai)

This file is the requirement-by-requirement completion map for the Windows /
Mission Planner USB goal. It intentionally separates Linux/firmware evidence
from Windows evidence so the goal is not closed prematurely.

## Current Firmware Identity

The current default build artifact has now been built, flashed, and
Linux-validated on the bench as the ArduPilot/ChibiOS-style composite CDC
identity. It no longer uses the old fixed `RTT5740N` serial string; the USB
serial is generated from the STM32 96-bit UID, matching ChibiOS `%SERIAL%`
semantics.

```text
1209:5740 Generic CUAVv5
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00
/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if02
```

Latest evidence:

```text
results/execution/chibios_product_sync_20260618T115640Z/descriptor.json
results/execution/chibios_product_sync_20260618T115640Z/openocd_flash.log
results/execution/chibios_product_sync_20260618T115640Z/usb_after_flash.txt
results/execution/chibios_product_sync_20260618T115640Z/param_download/param_download.json
results/execution/chibios_product_sync_20260618T115640Z/mavftp_rerun/mavftp_gate.json
results/execution/chibios_product_sync_20260618T115640Z/socketcan_dronecan/socketcan_dronecan_gate.json

Previous UID-serial evidence:
results/execution/chibios_uid_serial_followup_20260618T113117Z/descriptor.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/openocd_flash.log
results/execution/chibios_uid_serial_followup_20260618T113117Z/usb_after_flash.txt
results/execution/chibios_uid_serial_followup_20260618T113117Z/param_download/param_download.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/mavftp/mavftp_gate.json
results/execution/chibios_uid_serial_followup_20260618T113117Z/socketcan_dronecan_after_flash/socketcan_dronecan_gate.json
```

`if00` maps to `MI_00` / MAVLink. `if02` maps to `MI_02` / SLCAN.

This build aligns the CDC notification endpoint packet size, device class,
IAD layout, Call Management capability, and endpoint map with the ArduPilot
ChibiOS dual-CDC descriptor. The default build uses the ChibiOS-style
function/interface string indices (`iFunction=0`, `iInterface=0`), a
per-board UID serial, and the ChibiOS `CUAVv5` product string to reduce
Windows composite CDC binding variance after the user reported that Windows
showed only one `ArduPilot` COM port.

The board serial topology has also been moved to the ChibiOS fmuv5 shape:
`SERIAL6` is UART7/debug and `SERIAL7` is OTG2/SLCAN.  The RTT hwdef generator
now emits `HAL_OTG2_UART_INDEX=7` and `DEFAULT_SERIAL7_PROTOCOL
HAL_OTG2_PROTOCOL`, with `HAL_OTG2_PROTOCOL=SerialProtocol_SLCAN` for CUAV V5.
The embedded defaults route USB SLCAN to the user's current CAN1 bench path:
`SERIAL7_PROTOCOL=22` and `CAN_SLCAN_CPORT=1`.

```text
MI_00 notification EP 0x81 wMaxPacketSize=0x0010
MI_02 notification EP 0x83 wMaxPacketSize=0x0010
bcdDevice = 0x0200
serial = 2B0039000351383439353636
MI_00 iFunction/iInterface = 0
MI_02 iFunction/iInterface = 0
```

The latest Windows-safe firmware change keeps each MAVLink CDC IN transfer at
one full-speed 64B bulk packet while retaining the CDC ring queue.  This avoids
Windows `usbser.sys` / Mission Planner sensitivity to composite CDC transfer
boundary and ZLP drift while keeping Linux parameter download fast.

The latest TX recovery change keeps the ChibiOS-visible contract that configured
CDC can transmit immediately, while adding a feedback stop when endpoint recovery
proves that Windows configured the composite device but is not consuming the
MAVLink IN endpoint.  After three no-DTR recovery events, TX pauses until a CDC
class request, DTR/RTS transition, or OUT packet proves the host has opened the
MAVLink pipe again.  This is the current explanation for why ChibiOS works well
with Mission Planner while RTT previously could fall into a busy endpoint loop:
ChibiOS' SerialUSB write path is immediate and queue-limited; RTT/CherryUSB
needed explicit DWC2 endpoint feedback and recovery to avoid hiding host
backpressure.

The current UID-serial firmware keeps the ChibiOS `bcdDevice=0x0200` and the
official dual-CDC descriptor shape while forcing Windows to create a clean
per-board app instance instead of reusing stale fixed-serial bindings. Windows
validation must prefer the interface node (`MI_00` for Mission Planner,
`MI_02` for CAN/SLCAN) and the diagnostic topology JSON, not the friendly name
shown in Device Manager. If Python/Win32 serial APIs hide MI tags and expose
only one `1209:5740` COM, the acceptance script now probes that COM as a
data-path diagnostic; final closure still requires Mission Planner proof on
the same COM and preferably explicit `MI_00` / MAVLink interface identity.
If the only visible `ArduPilot` COM is classified as `SLCAN_MI02` or sends/receives
SLCAN ASCII such as `V\r`, `F\r`, or `N\r`, it is not a Mission Planner port.
`rtt_windows_usb_diag.ps1 -ProbeComOpen` now records this in
`slcan_ascii_probe_results.json`.

The current default build now matches ChibiOS more closely in two places.
First, MAVLink TX no longer depends on DTR being asserted: `CONFIGURED`, CDC
line-coding/control requests, including `GET_LINE_CODING`, and MAVLink OUT
data all count as host-open hints. A falling DTR edge no longer clears MAVLink
TX; only USB reset/deinit or endpoint recovery can clear the host-open hint.
Second, the CherryUSB standard interface request path now ACKs repeated
`SET_INTERFACE(alt=0)` without tearing down CDC endpoints. That mirrors the
ChibiOS dual-CDC behavior and removes a plausible Windows composite-binding
regression when the host replays alt-setting zero during interface setup. The
latest diagnostic-only addition also records `SET_CONTROL_LINE_STATE` RTS state
next to DTR in `rtt_usb_debug_snapshot.py`, so a Windows/Mission Planner failure
can distinguish "host never opened the CDC interface" from "class requests
arrived but CDC bulk TX did not advance".

The ChibiOS-vs-RTT startup timing difference is now controlled by
`RTT_USB_PREINIT_DISCONNECT_US`.  The default image uses a ChibiOS-like 1500 us
preinit soft-disconnect window while keeping the final `1209:5740` dual-CDC
identity.  A legacy A/B image can be built with
`libraries/AP_HAL_RTT/hwdef/extras/windows_long_usb_disconnect.dat`, which
restores the older 750 ms wait without changing the descriptor shape.

Current local build snapshot:

```text
build/rtt_cuav_v5/rtthread.bin
sha256=cc568ac54d6de378e78e9aa56f208597b76c69cfcede96247df6702fc678bc2c

build/rtt_deploy/cuav_v5/rt-thread.elf
sha256=6b4460ff07447fbbfe82dff9bce5a419c0cafaa71847a0f07ebd220d4c8734d6

build/rtt_deploy/cuav_v5/arducopter.apj
sha256=679121f8d51881bf278293f456daaf62ae35301bc382423057f273917124b02c
```

The latest default dual-CDC source builds successfully:

```text
python3 -m SCons --v=ArduCopter --target=cuav_v5 -j$(nproc)
scons: done building targets
Reset_Handler literal pool correct
```

Static descriptor gate `Tools/scripts/rtt_usb_descriptor_gate.py` on
`build/rtt_cuav_v5/rtthread.bin` found the expected ChibiOS dual CDC descriptor
(`1209:5740`, `bcdDevice=0x0200`, 4 interfaces, IAD at `MI_00`/`MI_02`,
`iFunction/iInterface=0`, interrupt MPS 16, bulk MPS 64, `bMaxPower=50`).
The gate now also checks Windows-binding-sensitive string descriptors:
language `0x0409`, manufacturer `ArduPilot`, product `CUAVv5`, and a
24-character uppercase hexadecimal serial descriptor. In the static binary that
serial is the zero placeholder; runtime Linux enumeration proves it is patched
to the STM32 UID before USB descriptor registration.
Latest descriptor evidence:

```text
results/execution/chibios_product_sync_20260618T115640Z/descriptor.json
results/execution/chibios_string_gate_20260618T120747Z/descriptor.json
verdict=GREEN
```

The latest flashed image that the Linux gates use was programmed over OpenOCD:

```text
results/execution/chibios_product_sync_20260618T115640Z/openocd_flash.log
Programming Finished
Verified OK
```

Post-flash Linux gates on that image were rerun sequentially to avoid opening
the same `if00` CDC port from multiple host processes:

```text
results/execution/chibios_product_sync_20260618T115640Z/param_download/param_download.json
verdict=GREEN, 945/945, 1.695 s, 557.4 params/s, first response 0.205 s

results/execution/chibios_product_sync_20260618T115640Z/mavftp_rerun/mavftp_gate.json
verdict=GREEN, @PARAM/param.pck decoded 945 params, write/read/remove OK

results/execution/chibios_1500us_20260618T110002Z/slcan_ascii_board_only/slcan_ascii_gate.json
verdict=GREEN, V1010, F00, standard z, extended Z

results/execution/chibios_1500us_20260618T110002Z/peripheral_health/peripheral_health.json
verdict=GREEN, driver_verdict=GREEN, mag.healthy=true, calibration_verdict=RED

results/execution/chibios_sync_cont_20260618T094056Z_after_txpause/open_close_after_flash/cdc_open_close_stress.json
verdict=GREEN, 8 interrupted open/close rounds, final full param download recovered

results/execution/chibios_product_sync_20260618T115640Z/socketcan_dronecan/socketcan_dronecan_gate.json
verdict=GREEN, sudo slcand, standard/extended cansend OK, DroneCAN-like extended frames from source node 10
```

The temporary single-CDC MAVLink diagnostic firmware was built, flashed, and
verified on Linux as `VID_1209&PID_5741` / `RTT5741M`, then archived before the
final dual-CDC firmware was restored:

```text
results/execution/windows_mp_singlecdc_diag_artifact_20260615T032024Z/
results/execution/windows_mp_singlecdc_diag_linux_mavlink_after_scriptfix_20260615T031929Z/20260615T031929Z/mavlink_acceptance.json
```

That earlier evidence is retained for comparison but is not proof that the new
UID-serial dual-CDC artifact fixes Windows/Mission Planner. The hashes above are a
local build snapshot; the flashed/validated evidence remains the dated
`results/execution/...` entries below.

## Completion Matrix

| Requirement | Required evidence | Current evidence | Status |
| --- | --- | --- | --- |
| Firmware builds | SCons build passes for CUAV V5 | `python3 -m SCons --target=cuav-v5 -j16` completed; binary integrity passed | PROVEN |
| Firmware flashes | OpenOCD program OK at `0x08008000` | `results/execution/chibios_product_sync_20260618T115640Z/openocd_flash.log`: `Programming Finished`; `Verified OK`; rc is `0` | PROVEN |
| Linux enumerates app USB identity | `1209:5740`, IAD composite, `if00` and `if02` present, `bcdDevice=2.00`, serial is STM32 UID | Current Linux showed `/dev/serial/by-id/usb-ArduPilot_CUAVv5_2B0039000351383439353636-if00` and `if02`; descriptor gate `results/execution/chibios_product_sync_20260618T115640Z/descriptor.json` is GREEN | PROVEN |
| MAVLink CDC works on app `if00` | heartbeat, raw MAVLink bytes, and full param download | `param_download/param_download.json`: `945/945`, `1.695 s`, `557.4 params/s`, status 3, first response `0.205 s` | PROVEN |
| MAVLink CDC works without DTR / host-open race | Serial open/close stress recovers and full param download remains fast | `open_close_after_flash/cdc_open_close_stress.json`: 8 interrupted open/close rounds, final full param download recovered; snapshots after flash and after stress classify `GREEN_USB_COUNTERS_ACTIVE` with no TX ring drops | PROVEN |
| SLCAN CDC works on app `if02` | SLCAN command response; CAN1/DroneCAN traffic when debugger is present | `socketcan_dronecan/socketcan_dronecan_gate.json`: sudo slcand, standard/extended cansend OK, candump saw source node 10, official `dronecan` NodeStatus GREEN | PROVEN |
| MAVFTP works on app `if00` | list/read/write/remove and post-FTP heartbeat stability | `mavftp_rerun/mavftp_gate.json`: `mavftp_ok`, `@PARAM/param.pck` decoded `945`, `/APM/test_ftp.tmp` write/read/remove OK | PROVEN |
| Peripheral drivers work | IMU, mag, baro, logging, SD-backed APM paths produce sane data | `peripheral_after_flash2/peripheral_health.json`: `driver_verdict=GREEN`; gyro/accel/baro/logging/mag driver-level paths healthy; `calibration_verdict=RED` and `ahrs.healthy=false` remain | PROVEN for driver-level; NOT flight/prearm complete |
| OpenOCD runtime check | PC in app after halt, no HardFault evidence | `usb_snapshot_after_flash/20260618T095040Z/usb_debug_snapshot.json` and `usb_snapshot_after_open_close/20260618T095202Z/usb_debug_snapshot.json`: OpenOCD returncode `0`, USB counters active; no HardFault evidence reported | PROVEN |
| Windows sees MAVLink COM | Windows `VID_1209&PID_5740&MI_00 -> COMx`, or an explicitly classified current app MAVLink interface equivalent | must come from `rtt_windows_acceptance_all.ps1` or `rtt_windows_usb_diag.ps1` output. `YELLOW_MAVLINK_DATA_PATH_NO_MI_TAG`, no-MI `1209:5740`, and `WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE` are diagnostic data-path evidence only, not final identity acceptance | MISSING |
| Windows MAVLink direct gate | `rtt_windows_mavlink_acceptance.py` returns `GREEN` on `MI_00` COM and `raw_mavlink_probe` sees `0xfe/0xfd` MAVLink magic | must come from Windows `mavlink_acceptance.json` | MISSING |
| Mission Planner connects | Mission Planner connects to `MI_00` COM and completes parameter download | user-visible Mission Planner evidence or captured report | MISSING |
| Windows SLCAN COM | Windows `VID_1209&PID_5740&MI_02 -> COMx`, or an explicit non-final diagnostic/fallback record explaining why `MI_02` is unavailable | must come from Windows USB diagnostic output. A plain `YELLOW_MAVLINK_ONLY` run is not enough for final GREEN because the default target is dual CDC with SLCAN | MISSING |

## Windows Commands To Produce Missing Evidence

Preferred one-shot command:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1
```

If Python is exposed as `py`:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -Python py
```

If Python dependencies are missing:

```powershell
python -m pip install pymavlink pyserial
```

If Windows only shows one `ArduPilot` COM or keeps showing old ports, first
run the combined acceptance script in refresh preview mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb
```

After reviewing `removal_candidates.json`, remove stale non-present target nodes
and rerun USB/MAVLink acceptance:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshApply
```

For the specific current symptom where Windows shows exactly one online
`ArduPilot` COM and Mission Planner cannot connect, close Mission Planner and
preview removal of the present target nodes as well:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshRemovePresentTarget
```

Only after confirming `usb_refresh\...\removal_candidates.json` contains only
ArduPilot/CUAV `VID_1209&PID_5740` / `VID_1209&PID_5741` nodes, apply the
refresh and immediately rerun COM/MAVLink probing:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -RefreshUsb -RefreshRemovePresentTarget -RefreshApply -ProbeComOpen -Python py
```

If Windows only shows one `ArduPilot` COM, first close Mission Planner and run
the combined gate with COM-open probing:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen
```

When `build\rtt_cuav_v5\rtthread.bin` is present in the Windows checkout, the
combined gate also runs `Tools\scripts\rtt_usb_descriptor_gate.py` and stores
the result under `firmware_descriptor\descriptor.json`. This proves the exact
artifact being tested still has the ChibiOS-style `1209:5740`, `MI_00`/`MI_02`,
`ArduPilot`, `CUAVv5`, and 24-character serial descriptor structure. If the bin
is not present, this firmware descriptor step is skipped and the run remains a
Windows USB/MAVLink diagnostic rather than a complete firmware-plus-Windows
evidence bundle. To point at a copied artifact explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -FirmwareBin .\build\rtt_cuav_v5\rtthread.bin
```

When `-AllowDiagnostic5741` is used for the explicit single-CDC isolation image,
the combined gate switches the descriptor profile to `mavlink_only`. That can
produce `YELLOW_DIAGNOSTIC` evidence for Windows/Mission Planner isolation, but
it is not final dual-CDC acceptance and cannot satisfy the `1209:5740`/`MI_02`
completion rule below.

If you already know the single COM number, pass it explicitly so the script can
prove whether that exact port has MAVLink heartbeat and fast parameter download:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -MavlinkPort COMx
```

For final delivery after Mission Planner has connected and completed parameter
download on the MAVLink COM, pass the Mission Planner evidence JSON and require
the final audit to be green:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py -MissionPlannerEvidence .\mission_planner_evidence.json -RequireAuditGreen
```

If the MAVLink script reports
`multiple_1209_5740_com_ports_without_mi_tag_use_manual_port`, inspect the USB
diagnostic report and manually pass the current app MAVLink COM. Prefer
`MI_00`; for the current UID-serial build, a single `TARGET_5740_NO_MI_TAG` COM is only a
manual diagnostic candidate if it does not answer SLCAN ASCII probes. Use it
to prove raw bytes, heartbeat, and fast parameters, but do not treat that as
final Mission Planner acceptance until Windows exposes `MI_00` or an explicit
MAVLink interface string:

```powershell
python .\Tools\scripts\rtt_windows_mavlink_acceptance.py --port COMx
```

If a manually supplied `COMx` is not listed by Windows pyserial/CIM/PnP
enumeration, the MAVLink script records
`WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE`. That mode may prove raw MAVLink,
heartbeat, and parameter-download data flow, but it is not final MI_00 /
Mission Planner USB identity evidence.

## Diagnostic Single-CDC A/B Test

The final target remains default dual CDC `1209:5740`:

```text
VID_1209&PID_5740&MI_00  MAVLink / Mission Planner
VID_1209&PID_5740&MI_02  SLCAN
```

For the specific Windows symptom "only one ArduPilot COM is visible and
Mission Planner cannot connect", build the explicit MAVLink-only diagnostic
firmware:

```bash
scons --v=ArduCopter --target=cuav_v5 \
  --extra-hwdef=libraries/AP_HAL_RTT/hwdef/extras/windows_mavlink_only.dat \
  -j$(nproc)
```

That diagnostic firmware uses the same MAVLink CDC data path, but exposes one
ChibiOS-style single CDC device on `1209:5741` and does not expose SLCAN.
Windows acceptance must opt into that identity:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -AllowDiagnostic5741
```

Interpretation:

- Single CDC `1209:5741` works in Mission Planner while default dual CDC
  `1209:5740` fails: focus on Windows composite CDC / MI binding, stale device
  nodes, or Mission Planner selecting the wrong interface.
- Single CDC `1209:5741` also fails: focus on Windows `usbser.sys`, COM port
  occupancy, Mission Planner behavior, or CDC data path.

This diagnostic result alone does not close the final dual-CDC goal.  Restore
default dual CDC after the A/B test and close the goal only against
`VID_1209&PID_5740&MI_00` plus `MI_02` evidence.

Maintainers can verify the script's Windows COM selection rules without
hardware:

```bash
python3 Tools/scripts/rtt_windows_mavlink_acceptance.py --self-test
python3 Tools/scripts/rtt_windows_acceptance_audit.py --self-test
```

The evidence directory to keep is:

```text
rtt_windows_acceptance_evidence\<timestamp>\
```

The minimum files needed to close the Windows part are:

```text
summary.json
audit_summary.json
audit_stdout.txt
usb_diag\<timestamp>\summary.json
usb_diag\<timestamp>\target_pnp_devices.json
usb_diag\<timestamp>\target_usb_topology.json
usb_diag\<timestamp>\classified_flight_controller_com_ports.json
usb_diag\<timestamp>\target_or_named_flight_controller_com_ports.json
mavlink\<timestamp>\mavlink_acceptance.json
firmware_descriptor\descriptor.json
mission_planner_evidence.json
```

After the evidence directory is available, run:

```bash
python3 Tools/scripts/rtt_windows_acceptance_audit.py rtt_windows_acceptance_evidence/<timestamp>
```

The combined Windows script now writes the same audit result automatically to
`audit_summary.json` and `audit_stdout.txt`. The Windows/Mission Planner part is
not closed unless this audit returns `"verdict": "GREEN"` or equivalent
manually inspected evidence proves every audit check.

## Completion Rule

Do not mark the Windows/Mission Planner USB goal complete until:

1. Windows reports `VID_1209&PID_5740&MI_00` as a COM port, or an explicitly
   classified current app MAVLink interface equivalent. A no-MI `1209:5740`
   COM or `WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE` COM that passes MAVLink is
   useful diagnostic evidence, but not final dual-CDC identity acceptance by
   itself.
2. `rtt_windows_mavlink_acceptance.py` proves raw MAVLink bytes, heartbeat, and
   full parameter download on that MAVLink COM.
3. Mission Planner is verified to connect to the MAVLink COM, not
   `MI_02` / SLCAN.
4. `MI_02` SLCAN is visible as a second COM, or the run is explicitly recorded
   as a non-final diagnostic/fallback case. A default dual-CDC run that only
   proves `MI_00` MAVLink but has no `MI_02` / SLCAN evidence must remain RED
   in `rtt_windows_acceptance_audit.py`.
5. `firmware_descriptor\descriptor.json` exists and is GREEN for the
   ChibiOS-style dual CDC app identity: `1209:5740`, `bcdDevice=0x0200`,
   `MI_00`/`MI_02`, manufacturer `ArduPilot`, product `CUAVv5`, and a
   24-character serial descriptor structure.
