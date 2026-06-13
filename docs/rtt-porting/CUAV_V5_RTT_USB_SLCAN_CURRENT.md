# CUAV V5 RTT USB CDC + SLCAN Current State

Updated: 2026-06-13 (Asia/Shanghai)

This note tracks the follow-on USB composite validation after the main CDC
MAVLink acceptance. The user connected an external SLCAN debugger to the board
connector they identified as CAN1. Bench evidence shows that connector is reached
by ArduPilot's second internal CAN interface (`CAN_SLCAN_CPORT=2`) in the RTT
port. `CAN_SLCAN_CPORT=1` remains a useful red-path counterexample: it parses
SLCAN commands but never receives a physical ACK on this bench setup.

## Port Map

Current Linux device map:

| Function | Stable path | Kernel tty | Evidence |
|---|---|---|---|
| RT-Thread console / UART7 | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5565052973-if00` | `ttyACM0` | `results/execution/slcan_20260613T064543Z_param_probe_retry/params.json` context |
| Flight-controller MAVLink CDC | `/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if00` | `ttyACM1` | MAVLink parameter download and heartbeat gates |
| Flight-controller USB SLCAN | `/dev/serial/by-id/usb-APM_CUAV_V5_CDC_1_00001-if02` | `ttyACM2` | `V` command returns `V1010` |
| External SLCAN debugger on CAN1 | `/dev/serial/by-id/usb-STMicroelectronics_STM32_Virtual_ComPort_206E395C5446-if00` | `ttyACM3` | `V` command returns `2023 1010` |

Do not run MAVLink gates on `if02`, and do not run SLCAN/can-utils on `if00`.

## Host Tooling

Installed/available:

- `can-utils` from Ubuntu package manager: `slcand`, `candump`, `cansend`.
- `pyserial 3.5`.
- `pymavlink 2.4.48`.
- `dronecan 1.0.27`.

## Firmware Configuration

Current parameter evidence:

```text
results/execution/slcan_20260613T064543Z_param_probe_retry/params.json
```

Historical red-path values:

```text
SERIAL0_PROTOCOL = 2   # MAVLink2 on OTG1 / if00
SERIAL6_PROTOCOL = 22  # SLCAN on OTG2 / if02
CAN_P1_DRIVER    = 1
CAN_D1_PROTOCOL  = 1   # DroneCAN
CAN_P1_BITRATE   = 1000000
CAN_SLCAN_CPORT  = 1   # Route SLCAN to CAN1/CAN0 internal index
CAN_SLCAN_SERNUM = -1  # not using temporary serial-port takeover
```

Current bench-proven values:

```text
results/execution/slcan_20260613T075353Z_read_can_probe_params_after_set_error/params.json

SERIAL0_PROTOCOL = 2
SERIAL6_PROTOCOL = 22
CAN_P1_DRIVER    = 1
CAN_D1_PROTOCOL  = 1
CAN_P1_BITRATE   = 1000000
CAN_P2_DRIVER    = 1
CAN_D2_PROTOCOL  = 1
CAN_P2_BITRATE   = 1000000
CAN_SLCAN_CPORT  = 2
CAN_SLCAN_SERNUM = -1
```

The built defaults now select the proven USB SLCAN path:

```text
libraries/AP_HAL_RTT/hwdef/cuav_v5/defaults.parm
SERIAL6_PROTOCOL 22
CAN_P1_DRIVER 1
CAN_D1_PROTOCOL 1
CAN_P2_DRIVER 1
CAN_D2_PROTOCOL 1
CAN_SLCAN_CPORT 2
```

## What Is Proven

USB CDC/MAVLink remains fast after the CAN TX mailbox recovery fix:

```text
results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/param_download/param_download.json
verdict=GREEN
reported_count=942
unique_indices=942
elapsed_s=1.601
rate_params_s=588.3
missing_count=0
standby_ok=true
```

MAVLink FTP remains working:

```text
results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/mavftp/mavftp_gate.json
verdict=GREEN
reason=mavftp_ok
param_pck_decode.decoded_count=942
post_ftp_stability.stable=true
```

The current full serial acceptance suite after rebuilding and flashing the
worktree is:

```text
results/execution/acceptance_20260613T090123Z_after_align_flash_serial_full/acceptance_suite.json
verdict=GREEN
reason=acceptance_suite_ok
```

Earlier CDC evidence retained for comparison:

```text
results/execution/slcan_20260613T070313Z_cdc_param_if02_alive/param_download.json
verdict=GREEN
reported_count=940
unique_indices=940
elapsed_s=2.031
rate_params_s=462.7
missing_count=0
```

The second USB virtual device is a real SLCAN command endpoint:

```text
results/execution/slcan_20260613T065152Z_tty_probe/tty_probe.json
board_if02:
  V_resp_ascii = "V1010\r"
  S6/O/C responses = "\r"
```

Direct SLCAN command testing against the flight-controller `if02` also passed
the SLCAN command layer:

```text
results/execution/slcan_20260613T070249Z_ascii_direct_bus_retry/ascii_direct_bus.json
apm:
  C/S8/O responses = "\r"
  F response = "F00\r"
  t12381122334455667788 response = "z\r"
```

CAN manager initialized the physical CAN1 path and DroneCAN driver:

```text
results/execution/slcan_20260613T070754Z_can_stats_before_after_ascii/can_stats_before_after_ascii.json
@SYS/can_log.txt:
  INFO SLCAN :Setting SLCAN Passthrough for CAN0
  DEBUG CANIface :Bitrate 1000000 mode 1
  INFO CANMGR :CAN Interface 1 initialized well
  INFO DroneCANIface :DroneCANIfaceMgr: Successfully added interface 0
  INFO DroneCAN :DroneCAN: init done
```

OpenOCD register access after the failed CAN transmission did not find a
HardFault and was able to resume the target:

```text
results/execution/slcan_20260613T071007Z_can_stats_after_second_send/can_stats_after_second_send.json
heartbeat=true

results/execution/slcan_20260613T071007Z_can_stats_after_second_send/can_stats_after_second_send.json
@SYS/can0_stats.txt:
  tx_requests:    1
  tx_success:     0
  rx_received:    0
  ESR:            800033

results/execution/slcan_20260613T071112Z_openocd_can1_regs_ack_error/openocd_can1_regs.log
CAN1 ESR mdw 0x40006418 = 0x00800033
```

The CAN TX mailbox recovery bug was fixed in:

```text
libraries/AP_HAL_RTT/CanIface.cpp
```

Before the fix, one unacknowledged TX could clog recovery. After the fix, the
driver keeps recovering timed-out mailboxes instead of staying stuck:

```text
results/execution/slcan_20260613T074423Z_can0_stats_before_ascii_after_fix/mavftp_sys_read.json
@SYS/can0_stats.txt:
  tx_requests: 408
  tx_timedout: 407

results/execution/slcan_20260613T074548Z_can0_stats_after_ascii_after_fix/mavftp_sys_read.json
@SYS/can0_stats.txt:
  tx_requests: 507
  tx_timedout: 506
```

`CAN_SLCAN_CPORT=2` is the bench-proven physical bus path:

```text
results/execution/slcan_20260613T080644Z_can_stats_before_can2_probe_ascii/mavftp_sys_read.json
@SYS/can1_stats.txt:
  tx_requests: 3097
  tx_success:  3097
  rx_received: 14
  ESR:         0
```

Direct ASCII SLCAN standard CAN is GREEN on the proven path:

```text
results/execution/slcan_20260613T090627Z_ascii_after_full_acceptance/slcan_ascii_gate.json
verdict=GREEN
reason=slcan_ascii_bidir_ok
board_to_debugger: saw t123..t127
debugger_to_board: saw t321..t325
```

SocketCAN/can-utils standard CAN is GREEN on the proven path:

```text
results/execution/slcan_20260613T080914Z_socketcan_bidir_can2_probe/
can_apm saw 130..134 and 330..334
can_dbg saw 130..134 and 330..334
can_apm.after.link RX packets=348 TX packets=5 errors=0
can_dbg.after.link RX packets=346 TX packets=5 errors=0
```

DroneCAN is GREEN using official pydronecan:

```text
results/execution/dronecan_20260613T091439Z_after_full_acceptance_fixed_device/pydronecan.json
verdict=GREEN
reason=node_status_seen
dronecan_version=1.0.27
source_node_id=10
NodeStatus events=9 in 8.5 seconds
```

For official `dronecan 1.0.27`, pass the SocketCAN interface name directly:
`dronecan.make_node("can_dbg", node_id=127, bitrate=1000000)`.  The string
`"can:can_dbg"` is parsed as a literal SocketCAN interface and fails with
`OSError: [Errno 19] No such device`.

CDC and SLCAN coexistence is GREEN under CAN traffic:

```text
results/execution/coexist_20260613T091529Z_after_full_acceptance/
param_gate/param_download.json:
  verdict=GREEN
  reported_count=942
  elapsed_s=1.944
  rate_params_s=484.6

mavftp_gate/mavftp_gate.json:
  verdict=GREEN
  reason=mavftp_ok
  param_pck_decode.decoded_count=942
  post_ftp_stability.stable=true

candump:
  can_apm saw 9220 frames with ID 0x555
  can_dbg saw 9220 frames with ID 0x555
```

## Red-Path Counterexample: CPORT=1

Two attempts were made:

1. SocketCAN/can-utils:
   - `slcand` created `can_apm` from flight-controller `if02`.
   - `slcand` created `can_dbg` from the external STM32 SLCAN debugger.
   - `cansend` returned `0` in both directions.
   - `candump` saw no frame in either direction.
   - Evidence:
     `results/execution/slcan_20260613T070101Z_classic_can_bidir/`.

2. Direct ASCII SLCAN:
   - Flight-controller SLCAN accepted a standard frame and returned `z`.
   - External debugger accepted standard-frame send commands.
   - Neither endpoint observed the other endpoint's frame.
   - Evidence:
     `results/execution/slcan_20260613T070249Z_ascii_direct_bus_retry/ascii_direct_bus.json`.

These failures are retained because they explain the earlier confusion: SLCAN
commands on `if02` can return success while the selected physical CAN interface
does not receive ACK from the connected bus.

## Current Diagnosis

The current evidence points below the USB CDC/SLCAN virtual-device layer for the
red path, and confirms a working physical bus on the second internal CAN
interface.

Flight-controller USB `if02` parses SLCAN correctly, and CAN manager initializes
CAN1 at `1000000` bit/s. A frame sent through SLCAN reaches the physical CAN
driver once:

```text
tx_requests: 1
tx_success:  0
ESR:         0x00800033
```

After that, another SLCAN frame does not increase `tx_requests`; the first
unacknowledged transmission appears to keep the mailbox path busy. This is
consistent with CAN1 not receiving an ACK from another active node at the same
bitrate, not with USB `if02` failing to parse SLCAN commands.

Current interpretation:

- RTT `hwdef.dat` matches ChibiOS `fmuv5` CAN pins:
  `PI9/PH13` for CAN1 and `PB12/PB13` for CAN2.
- The connected bench port is healthy at 1 Mbit/s when ArduPilot routes SLCAN to
  internal CAN2 (`CAN_SLCAN_CPORT=2`).
- Internal CAN1 (`CAN_SLCAN_CPORT=1`) remains unacknowledged in this setup:
  `tx_success=0`, `rx_received=0`, `ESR=0x00800033`.
- Do not count `SLCAN t... -> z` alone as a physical bus pass; require external
  observation, SocketCAN receive, or CAN stats success/RX.

## Required Completion Evidence

The USB CDC + SLCAN goal can be considered passing for the current hardware
setup when using `CAN_SLCAN_CPORT=2`, because all of the following now have
current artifacts:

- CDC `if00` MAVLink heartbeat and parameter download remain GREEN.
- CDC `if00` MAVLink FTP remains GREEN.
- SLCAN `if02` command layer responds to SLCAN commands.
- Standard CAN frame sent from flight-controller SLCAN is observed by the
  external debugger.
- Standard CAN frame sent from the external debugger is observed by the
  flight-controller SLCAN endpoint.
- `@SYS/can1_stats.txt` shows TX success and RX received increasing.
- DroneCAN is observed via pydronecan and candump.
- CDC and SLCAN are run concurrently without CDC parameter/FTP regression.

Remaining caveat: if the requirement is specifically the internal CAN1
peripheral (`CAN_SLCAN_CPORT=1`), that path is still RED on this bench. If the
requirement is the physical connector currently attached by the user, that path
is GREEN as internal CAN2 (`CAN_SLCAN_CPORT=2`).
