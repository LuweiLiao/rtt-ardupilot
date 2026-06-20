# RTT ArduPilot CUAV V5 Windows CDC Driver Binding

This directory contains a fallback INF for Windows machines that do not bind
the RTT ArduPilot composite CDC interfaces to the built-in `usbser.sys` driver.

Normal Windows 10/11 systems should not need this file. Use it only when
`Tools\scripts\rtt_windows_usb_diag.ps1` reports one of these states:

- `RED_MI00_PRESENT_NO_COM`
- `RED_TARGET_DEVICE_NO_MAVLINK_COM`
- `UsbSerBound` is `false` for `VID_1209&PID_5740&MI_00`

Expected interface mapping:

```text
USB\VID_1209&PID_5740&MI_00  MAVLink CDC for Mission Planner
USB\VID_1209&PID_5740&MI_02  SLCAN CDC
```

Do not install this INF to "make Mission Planner work" on the wrong port.
If Windows only shows one `ArduPilot` COM, first classify it with
`SinglePortDiagnosis` and `WindowsMissionPlannerDiagnosis`. If it is
`SLCAN_MI02`, Mission Planner should not use it. If it is
`TARGET_5740_NO_MI_TAG`, use the raw MAVLink gate first, then Mission Planner
only on the same COM after heartbeat and full parameter download are GREEN.

Run diagnostics first:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen
```

If the diagnostic says the `MI_00` or `MI_02` node is present but not bound to
`usbser`, install or manually select the built-in USB serial driver for the
interface node. On systems that allow local INF installation:

```powershell
pnputil /add-driver .\Tools\windows_drivers\rtt_ardupilot_cdc\rtt_ardupilot_cdc_usbser.inf /install
```

You can also use the helper script in this repository to perform the install,
rescan, and follow-up diagnostic in one step:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_usb_bind_fix.ps1 -Install -Rescan -ProbeComOpen
```

Helper exit codes:

```text
0  Binding/install path produced a usable MI_00 MAVLink COM; MI_02 may still
   need separate SLCAN follow-up if the verdict is YELLOW_MI00_AFTER_BIND_MI02_MISSING.
1  pnputil succeeded, but the nested USB diagnostic is still yellow/red; inspect
   usb_diag\...\summary.json and the helper summary.json.
2  Binding is still incomplete or no diagnostic summary was produced.
3  PnP rescan failed.
```

Or use the combined acceptance entrypoint:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py
```

If Windows rejects unsigned local INF installation, use Device Manager on the
specific `VID_1209&PID_5740&MI_00` node, choose to update the driver manually,
and select the built-in `USB Serial Device` / `usbser.sys` driver.

After binding, rerun:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen
```

Mission Planner must connect to the COM port classified as `MAVLINK_MI00`.
Do not connect Mission Planner to `SLCAN_MI02`.
