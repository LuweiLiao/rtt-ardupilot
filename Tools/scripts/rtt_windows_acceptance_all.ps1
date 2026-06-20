param(
    [string]$OutDir = ".\rtt_windows_acceptance_evidence",
    [string]$Python = "python",
    [string]$MavlinkPort = "auto",
    [switch]$RefreshUsb,
    [switch]$RefreshApply,
    [switch]$RefreshRemovePresentTarget,
    [switch]$BindUsbser,
    [switch]$BindUsbserRescan,
    [switch]$ProbeComOpen,
    [switch]$AllowDiagnostic5741,
    [string]$MissionPlannerEvidence = "",
    [string]$FirmwareBin = ".\build\rtt_cuav_v5\rtthread.bin",
    [switch]$RequireAuditGreen,
    [switch]$SkipMavlink
)

$ErrorActionPreference = "Continue"
$timestamp = Get-Date -Format "yyyyMMddTHHmmss"
$out = Join-Path $OutDir $timestamp
New-Item -ItemType Directory -Force -Path $out | Out-Null

$usbDiagDir = Join-Path $out "usb_diag"
$usbRefreshDir = Join-Path $out "usb_refresh"
$usbBindDir = Join-Path $out "usb_bind"
$mavlinkDir = Join-Path $out "mavlink"
$firmwareDescriptorDir = Join-Path $out "firmware_descriptor"
$refreshLog = Join-Path $out "usb_refresh_stdout.txt"
$bindLog = Join-Path $out "usb_bind_stdout.txt"
$usbLog = Join-Path $out "usb_diag_stdout.txt"
$mavlinkLog = Join-Path $out "mavlink_stdout.txt"
$firmwareDescriptorLog = Join-Path $out "firmware_descriptor_stdout.txt"
$auditLog = Join-Path $out "audit_stdout.txt"
$summaryPath = Join-Path $out "summary.json"
$auditSummaryPath = Join-Path $out "audit_summary.json"
$missionPlannerTemplatePath = Join-Path $out "mission_planner_evidence.template.json"
$missionPlannerEvidencePath = Join-Path $out "mission_planner_evidence.json"
$firmwareSnapshotCommandPath = Join-Path $out "firmware_snapshot_command.txt"

function Resolve-PowerShellExe {
    foreach ($candidate in @("powershell.exe", "powershell", "pwsh.exe", "pwsh")) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -ne $cmd) {
            return $cmd.Source
        }
    }
    return "powershell"
}

$powershellExe = Resolve-PowerShellExe

Write-Output "RTT Windows acceptance evidence: $out"
Write-Output ""
Write-Output "RefreshUsb: $RefreshUsb"
Write-Output "RefreshApply: $RefreshApply"
Write-Output "RefreshRemovePresentTarget: $RefreshRemovePresentTarget"
Write-Output "BindUsbser: $BindUsbser"
Write-Output "BindUsbserRescan: $BindUsbserRescan"
Write-Output "ProbeComOpen: $ProbeComOpen"
Write-Output "AllowDiagnostic5741: $AllowDiagnostic5741"
Write-Output "MissionPlannerEvidence: $MissionPlannerEvidence"
Write-Output "FirmwareBin: $FirmwareBin"
Write-Output "RequireAuditGreen: $RequireAuditGreen"
Write-Output "MavlinkPort: $MavlinkPort"
Write-Output "PowerShellExe: $powershellExe"

$firmwareDescriptorSummary = $null
$firmwareDescriptorJsonPath = Join-Path $firmwareDescriptorDir "descriptor.json"
$firmwareDescriptorRc = $null
$firmwareDescriptorProfile = if ($AllowDiagnostic5741) { "mavlink_only" } else { "chibios_dualcdc" }
if ($FirmwareBin -ne "" -and (Test-Path $FirmwareBin)) {
    Write-Output ""
    Write-Output "Step 0a/3: Firmware USB descriptor gate ($firmwareDescriptorProfile)"
    New-Item -ItemType Directory -Force -Path $firmwareDescriptorDir | Out-Null
    & $Python ".\Tools\scripts\rtt_usb_descriptor_gate.py" `
        "--bin" $FirmwareBin `
        "--profile" $firmwareDescriptorProfile `
        "--json-out" $firmwareDescriptorJsonPath |
        Tee-Object -FilePath $firmwareDescriptorLog
    $firmwareDescriptorRc = $LASTEXITCODE
    if (Test-Path $firmwareDescriptorJsonPath) {
        try {
            $firmwareDescriptorSummary = Get-Content $firmwareDescriptorJsonPath -Raw | ConvertFrom-Json
        } catch {
            Write-Output "Could not parse firmware descriptor summary: $($_.Exception.Message)"
        }
    }
} else {
    Write-Output ""
    Write-Output "Step 0a/3: Firmware USB descriptor gate skipped; firmware bin not found: $FirmwareBin"
}

$refreshSummary = $null
$refreshSummaryPath = $null
if ($RefreshUsb) {
    Write-Output ""
    Write-Output "Step 0/2: USB PnP refresh"
    $refreshArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\Tools\scripts\rtt_windows_usb_refresh.ps1",
        "-OutDir", $usbRefreshDir
    )
    if ($RefreshApply) {
        $refreshArgs += "-Apply"
    }
    if ($RefreshRemovePresentTarget) {
        $refreshArgs += "-RemovePresentTarget"
    }
    & $powershellExe @refreshArgs | Tee-Object -FilePath $refreshLog
    $refreshSummaryPath = Get-ChildItem -Path $usbRefreshDir -Recurse -Filter "summary.json" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -ne $refreshSummaryPath) {
        try {
            $refreshSummary = Get-Content $refreshSummaryPath.FullName -Raw | ConvertFrom-Json
        } catch {
            Write-Output "Could not parse USB refresh summary: $($_.Exception.Message)"
        }
    }
}

$bindSummary = $null
$bindSummaryPath = $null
if ($BindUsbser) {
    Write-Output ""
    Write-Output "Step 0b/2: USB usbser binding helper"
    $bindArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", ".\Tools\scripts\rtt_windows_usb_bind_fix.ps1",
        "-OutDir", $usbBindDir,
        "-Install"
    )
    if ($BindUsbserRescan) {
        $bindArgs += "-Rescan"
    }
    if ($ProbeComOpen) {
        $bindArgs += "-ProbeComOpen"
    }
    & $powershellExe @bindArgs | Tee-Object -FilePath $bindLog
    $bindSummaryPath = Get-ChildItem -Path $usbBindDir -Recurse -Filter "summary.json" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -ne $bindSummaryPath) {
        try {
            $bindSummary = Get-Content $bindSummaryPath.FullName -Raw | ConvertFrom-Json
        } catch {
            Write-Output "Could not parse USB bind summary: $($_.Exception.Message)"
        }
    }
}

Write-Output ""
Write-Output "Step 1/2: USB PnP/COM diagnostic"
$usbDiagArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", ".\Tools\scripts\rtt_windows_usb_diag.ps1",
    "-OutDir", $usbDiagDir
)
if ($ProbeComOpen) {
    $usbDiagArgs += "-ProbeComOpen"
}
if ($AllowDiagnostic5741) {
    $usbDiagArgs += "-AllowDiagnostic5741"
}
& $powershellExe @usbDiagArgs |
    Tee-Object -FilePath $usbLog
$usbSummaryPath = Get-ChildItem -Path $usbDiagDir -Recurse -Filter "summary.json" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$usbSummary = $null
if ($null -ne $usbSummaryPath) {
    try {
        $usbSummary = Get-Content $usbSummaryPath.FullName -Raw | ConvertFrom-Json
    } catch {
        Write-Output "Could not parse USB summary: $($_.Exception.Message)"
    }
}

$mavlinkSummary = $null
$mavlinkRc = $null
$mavlinkJsonPath = $null
$effectiveMavlinkPort = $MavlinkPort
if ($MavlinkPort -eq "auto" -and
    $null -ne $usbSummary -and
    $usbSummary.Verdict -eq "YELLOW_SINGLE_TARGET_COM_NO_MI_TAG" -and
    ($null -eq $usbSummary.SinglePortDiagnosis -or
     $usbSummary.SinglePortDiagnosis -ne "SINGLE_VISIBLE_PORT_RESPONDS_SLCAN_ASCII_NOT_MAVLINK") -and
    $null -ne $usbSummary.TargetNoMiManualProbeComCandidates -and
    $usbSummary.TargetNoMiManualProbeComCandidates.Count -eq 1) {
    $effectiveMavlinkPort = [string]$usbSummary.TargetNoMiManualProbeComCandidates[0]
}
if ($SkipMavlink) {
    Write-Output ""
    Write-Output "Step 2/2: MAVLink acceptance skipped by request."
} else {
    Write-Output ""
    Write-Output "Step 2/2: MAVLink heartbeat + parameter download"
    $mavlinkArgs = @(
        ".\Tools\scripts\rtt_windows_mavlink_acceptance.py",
        "--port", $effectiveMavlinkPort,
        "--outdir", $mavlinkDir
    )
    if ($AllowDiagnostic5741) {
        $mavlinkArgs += "--allow-diagnostic-5741"
    }
    & $Python @mavlinkArgs |
        Tee-Object -FilePath $mavlinkLog
    $mavlinkRc = $LASTEXITCODE
    $mavlinkJsonPath = Get-ChildItem -Path $mavlinkDir -Recurse -Filter "mavlink_acceptance.json" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -ne $mavlinkJsonPath) {
        try {
            $mavlinkSummary = Get-Content $mavlinkJsonPath.FullName -Raw | ConvertFrom-Json
        } catch {
            Write-Output "Could not parse MAVLink summary: $($_.Exception.Message)"
        }
    }
}

$overall = "RED"
$usbDiagnosticOnly = $false
$rawMavlinkGreen = $false
$rawMagicCount = 0
$rawProbeReason = $null
$rawProbeBytes = $null
$rawNoBytesSeen = $false
$rawNaturalVerdict = $null
$rawNaturalBytes = $null
$rawNaturalMagicCount = $null
$rawForcedVerdict = $null
$rawForcedBytes = $null
$rawForcedMagicCount = $null
if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.raw_mavlink_probe) {
    if ($null -ne $mavlinkSummary.raw_mavlink_probe.mavlink_magic_count) {
        $rawMagicCount = [int]$mavlinkSummary.raw_mavlink_probe.mavlink_magic_count
    } else {
        $rawMagicCount = [int]$mavlinkSummary.raw_mavlink_probe.mavlink_v1_magic_count +
            [int]$mavlinkSummary.raw_mavlink_probe.mavlink_v2_magic_count
    }
    $rawProbeReason = $mavlinkSummary.raw_mavlink_probe.reason
    $rawProbeBytes = $mavlinkSummary.raw_mavlink_probe.bytes
    if ($null -ne $mavlinkSummary.raw_mavlink_probe.probes) {
        $rawProbeBytes = 0
        foreach ($probe in $mavlinkSummary.raw_mavlink_probe.probes) {
            $rawProbeBytes += [int]($probe.bytes)
        }
    }
    if ($null -ne $mavlinkSummary.raw_mavlink_probe.natural_open) {
        $rawNaturalVerdict = $mavlinkSummary.raw_mavlink_probe.natural_open.verdict
        $rawNaturalBytes = $mavlinkSummary.raw_mavlink_probe.natural_open.bytes
        $rawNaturalMagicCount = [int]($mavlinkSummary.raw_mavlink_probe.natural_open.mavlink_v1_magic_count) +
            [int]($mavlinkSummary.raw_mavlink_probe.natural_open.mavlink_v2_magic_count)
    }
    if ($null -ne $mavlinkSummary.raw_mavlink_probe.forced_dtr_rts) {
        $rawForcedVerdict = $mavlinkSummary.raw_mavlink_probe.forced_dtr_rts.verdict
        $rawForcedBytes = $mavlinkSummary.raw_mavlink_probe.forced_dtr_rts.bytes
        $rawForcedMagicCount = [int]($mavlinkSummary.raw_mavlink_probe.forced_dtr_rts.mavlink_v1_magic_count) +
            [int]($mavlinkSummary.raw_mavlink_probe.forced_dtr_rts.mavlink_v2_magic_count)
    }
    $rawNoBytesSeen = (
        $mavlinkSummary.raw_mavlink_probe.reason -eq "no_bytes_seen" -or
        $mavlinkSummary.raw_mavlink_probe.reason -eq "no_mavlink_magic_seen" -or
        ($null -ne $rawProbeBytes -and [int]$rawProbeBytes -eq 0)
    )
    $rawMavlinkGreen = ($mavlinkSummary.raw_mavlink_probe.verdict -eq "GREEN" -and $rawMagicCount -gt 0)
}
$mavlinkAcceptanceMode = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.acceptance_mode) {
    $mavlinkSummary.acceptance_mode
} else {
    $null
}
$mavlinkAcceptedUsbIdentity = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.accepted_usb_identity) {
    $mavlinkSummary.accepted_usb_identity
} else {
    $null
}
$mavlinkFinalDualCdc = (
    $null -ne $mavlinkSummary -and
    $mavlinkSummary.is_final_dual_cdc_acceptance -eq $true
)
$mavlinkDiagnosticSingleCdc = (
    $null -ne $mavlinkSummary -and
    $mavlinkSummary.acceptance_mode -eq "DIAGNOSTIC_SINGLE_CDC_5741_MAVLINK_ONLY"
)
$mavlinkNoMiRawGateOnly = (
    $null -ne $mavlinkSummary -and
    $mavlinkSummary.acceptance_mode -eq "FINAL_DUAL_CDC_5740_MAVLINK_NO_MI_TAG_RAW_GATE"
)
$mavlinkManualUnlistedRawGate = (
    $null -ne $mavlinkSummary -and
    $mavlinkSummary.acceptance_mode -eq "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE"
)
$usbDiagnosticOnly = (
    $AllowDiagnostic5741 -and
    $null -ne $usbSummary -and
    $usbSummary.Verdict -eq "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY"
)
if ($null -ne $mavlinkSummary -and $mavlinkSummary.verdict -eq "GREEN" -and $rawMavlinkGreen -and $usbDiagnosticOnly -and $mavlinkDiagnosticSingleCdc) {
    $overall = "GREEN_DIAGNOSTIC_MAVLINK"
} elseif ($null -ne $mavlinkSummary -and $mavlinkSummary.verdict -eq "GREEN" -and $rawMavlinkGreen -and $mavlinkFinalDualCdc) {
    $overall = "GREEN_MAVLINK"
} elseif ($null -ne $mavlinkSummary -and $mavlinkSummary.verdict -eq "GREEN" -and $rawMavlinkGreen -and $mavlinkManualUnlistedRawGate) {
    $overall = "YELLOW_MAVLINK_DATA_PATH_UNLISTED_COM"
} elseif ($null -ne $mavlinkSummary -and $mavlinkSummary.verdict -eq "GREEN" -and $rawMavlinkGreen -and $mavlinkNoMiRawGateOnly) {
    $overall = "YELLOW_MAVLINK_DATA_PATH_NO_MI_TAG"
} elseif ($null -ne $usbSummary -and
          ($usbSummary.Verdict -eq "GREEN_TWO_COM_PORTS" -or
           $usbSummary.Verdict -eq "YELLOW_MAVLINK_ONLY" -or
           $usbSummary.Verdict -eq "YELLOW_SINGLE_TARGET_COM_NO_MI_TAG" -or
           $usbSummary.Verdict -eq "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY")) {
    $overall = "YELLOW_USB_MAVLINK_COM_PRESENT"
}

$mavlinkFailureClass = "UNKNOWN"
if ($SkipMavlink) {
    $mavlinkFailureClass = "SKIPPED"
} elseif ($null -eq $mavlinkSummary) {
    $mavlinkFailureClass = "NO_MAVLINK_JSON"
} elseif ($mavlinkSummary.verdict -eq "GREEN") {
    $mavlinkFailureClass = "MAVLINK_GREEN"
} elseif ($null -ne $mavlinkSummary.raw_mavlink_probe -and $rawNoBytesSeen) {
    $mavlinkFailureClass = "COM_OPENED_NO_RAW_MAVLINK_BYTES"
} elseif ($null -ne $mavlinkSummary.raw_mavlink_probe -and
          $mavlinkSummary.raw_mavlink_probe.reason -match "Access.*denied|PermissionError|UnauthorizedAccess|busy|denied") {
    $mavlinkFailureClass = "COM_BUSY_OR_ACCESS_DENIED"
} elseif ($null -ne $mavlinkSummary.raw_mavlink_probe -and
          $mavlinkSummary.raw_mavlink_probe.verdict -eq "GREEN") {
    $mavlinkFailureClass = "RAW_BYTES_PRESENT_HIGHER_LEVEL_FAILED"
} elseif ($null -ne $mavlinkSummary.reason -and
          $mavlinkSummary.reason -match "could not open|PermissionError|UnauthorizedAccess|Access.*denied") {
    $mavlinkFailureClass = "COM_OPEN_FAILED"
}

$singlePortDiagnosis = if ($null -ne $usbSummary) { $usbSummary.SinglePortDiagnosis } else { $null }
$slcanLikeProbePorts = if ($null -ne $usbSummary) { $usbSummary.SlcanLikeProbePorts } else { @() }
$windowsMissionPlannerDiagnosis = if (($null -ne $usbSummary -and $usbSummary.Verdict -eq "RED_SINGLE_VISIBLE_SLCAN_NOT_MAVLINK") -or
                                    $singlePortDiagnosis -eq "SINGLE_VISIBLE_PORT_RESPONDS_SLCAN_ASCII_NOT_MAVLINK" -or
                                    $singlePortDiagnosis -eq "SINGLE_VISIBLE_PORT_IS_SLCAN_NOT_MAVLINK") {
    "ONLY_VISIBLE_ARDUPILOT_COM_IS_SLCAN"
} elseif ($singlePortDiagnosis -eq "SINGLE_VISIBLE_TARGET_5740_NO_MI_TAG_REQUIRES_RAW_MAVLINK_GATE") {
    "ONLY_VISIBLE_ARDUPILOT_COM_HAS_NO_MI_TAG"
} elseif ($singlePortDiagnosis -eq "SINGLE_VISIBLE_PORT_IS_MAVLINK_CANDIDATE" -and
          ($mavlinkFailureClass -eq "COM_OPENED_NO_RAW_MAVLINK_BYTES")) {
    "MAVLINK_COM_OPENS_BUT_NO_RAW_MAVLINK_BYTES"
} elseif ($singlePortDiagnosis -eq "SINGLE_VISIBLE_PORT_IS_MAVLINK_CANDIDATE" -and
          ($mavlinkFailureClass -eq "COM_BUSY_OR_ACCESS_DENIED" -or $mavlinkFailureClass -eq "COM_OPEN_FAILED")) {
    "MAVLINK_COM_BUSY_OR_ACCESS_DENIED"
} elseif ($null -ne $usbSummary -and [int]($usbSummary.MI00DeviceCount) -ge 1 -and [int]($usbSummary.MI00ComCount) -eq 0) {
    "MI00_DEVICE_PRESENT_BUT_NO_COM"
} elseif ($null -ne $usbSummary -and [int]($usbSummary.TargetDeviceCount) -ge 1 -and [int]($usbSummary.MI00ComCount) -eq 0) {
    "TARGET_5740_VISIBLE_BUT_NO_MAVLINK_COM"
} elseif ($overall -eq "GREEN_MAVLINK") {
    "MAVLINK_MI00_GATE_GREEN"
} elseif ($overall -eq "YELLOW_MAVLINK_DATA_PATH_NO_MI_TAG") {
    "RAW_MAVLINK_GREEN_BUT_USB_IDENTITY_NOT_FINAL"
} elseif ($overall -eq "YELLOW_MAVLINK_DATA_PATH_UNLISTED_COM") {
    "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE"
} elseif ($mavlinkSummary -and $mavlinkSummary.acceptance_mode -eq "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE") {
    "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE"
} else {
    "UNRESOLVED"
}

$firmwareSnapshotCommand = "nohup timeout 90 python3 Tools/scripts/rtt_usb_debug_snapshot.py --outdir results/execution/windows_mp_firmware_snapshot_after_windows_attempt > results/execution/windows_mp_firmware_snapshot_after_windows_attempt.log 2>&1"
$firmwareSnapshotCommand | Set-Content -Encoding UTF8 $firmwareSnapshotCommandPath

$windowsRetestCommandHint = if ($AllowDiagnostic5741) {
    "powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -AllowDiagnostic5741 -Python py"
} else {
    "powershell -ExecutionPolicy Bypass -File .\Tools\scripts\rtt_windows_acceptance_all.ps1 -ProbeComOpen -Python py"
}
$evidenceFilesToCopy = @(
    "summary.json",
    "firmware_descriptor\descriptor.json",
    "usb_diag\<timestamp>\summary.json",
    "usb_diag\<timestamp>\classified_flight_controller_com_ports.json",
    "usb_diag\<timestamp>\target_or_named_flight_controller_com_ports.json",
    "usb_diag\<timestamp>\target_pnp_devices.json",
    "usb_diag\<timestamp>\target_usb_topology.json",
    "usb_diag\<timestamp>\legacy_5741_pnp_devices.json",
    "mavlink\<timestamp>\mavlink_acceptance.json",
    "mavlink_stdout.txt",
    "usb_diag_stdout.txt",
    "mission_planner_evidence.json"
)

$summary = [pscustomobject]@{
    Timestamp = (Get-Date).ToString("o")
    EvidenceDir = $out
    UsbRefreshSummaryPath = if ($null -ne $refreshSummaryPath) { $refreshSummaryPath.FullName } else { $null }
    UsbRefreshApply = [bool]$RefreshApply
    UsbRefreshRemoveCandidateCount = if ($null -ne $refreshSummary) { $refreshSummary.RemoveCandidateCount } else { $null }
    UsbRefreshRemovalSuccessCount = if ($null -ne $refreshSummary) { $refreshSummary.RemovalSuccessCount } else { $null }
    UsbBindSummaryPath = if ($null -ne $bindSummaryPath) { $bindSummaryPath.FullName } else { $null }
    UsbBindInstall = [bool]$BindUsbser
    UsbBindRescan = [bool]$BindUsbserRescan
    UsbBindInstallExitCode = if ($null -ne $bindSummary -and $null -ne $bindSummary.InstallResult) { $bindSummary.InstallResult.ExitCode } else { $null }
    UsbBindScanExitCode = if ($null -ne $bindSummary -and $null -ne $bindSummary.ScanResult) { $bindSummary.ScanResult.ExitCode } else { $null }
    UsbBindFixVerdict = if ($null -ne $bindSummary) { $bindSummary.BindFixVerdict } else { $null }
    UsbBindNextAction = if ($null -ne $bindSummary) { $bindSummary.NextAction } else { $null }
    UsbBindDiagVerdict = if ($null -ne $bindSummary) { $bindSummary.UsbDiagVerdict } else { $null }
    UsbBindDiagSummaryPath = if ($null -ne $bindSummary) { $bindSummary.UsbDiagSummaryPath } else { $null }
    FirmwareBin = $FirmwareBin
    FirmwareDescriptorRequestedProfile = $firmwareDescriptorProfile
    FirmwareDescriptorSummaryPath = if (Test-Path $firmwareDescriptorJsonPath) { (Resolve-Path $firmwareDescriptorJsonPath).Path } else { $null }
    FirmwareDescriptorReturnCode = $firmwareDescriptorRc
    FirmwareDescriptorVerdict = if ($null -ne $firmwareDescriptorSummary) { $firmwareDescriptorSummary.verdict } else { $null }
    FirmwareDescriptorProfile = if ($null -ne $firmwareDescriptorSummary -and $null -ne $firmwareDescriptorSummary.audit) { $firmwareDescriptorSummary.audit.profile } else { $null }
    FirmwareDescriptorDevice = if ($null -ne $firmwareDescriptorSummary -and $null -ne $firmwareDescriptorSummary.descriptor) { $firmwareDescriptorSummary.descriptor.device } else { $null }
    FirmwareDescriptorStrings = if ($null -ne $firmwareDescriptorSummary -and $null -ne $firmwareDescriptorSummary.descriptor) { $firmwareDescriptorSummary.descriptor.strings } else { @() }
    UsbSummaryPath = if ($null -ne $usbSummaryPath) { $usbSummaryPath.FullName } else { $null }
    UsbVerdict = if ($null -ne $usbSummary) { $usbSummary.Verdict } else { $null }
    MissionPlannerComCandidates = if ($null -ne $usbSummary) { $usbSummary.MissionPlannerComCandidates } else { @() }
    SlcanComCandidates = if ($null -ne $usbSummary) { $usbSummary.SlcanComCandidates } else { @() }
    NamedFlightControllerComCount = if ($null -ne $usbSummary) { $usbSummary.NamedFlightControllerComCount } else { $null }
    SingleNamedFlightControllerPort = if ($null -ne $usbSummary) { $usbSummary.SingleNamedFlightControllerPort } else { $null }
    SingleNamedFlightControllerPortKind = if ($null -ne $usbSummary) { $usbSummary.SingleNamedFlightControllerPortKind } else { $null }
    SingleNamedFlightControllerPortInfo = if ($null -ne $usbSummary) { $usbSummary.SingleNamedFlightControllerPortInfo } else { $null }
    SinglePortDiagnosis = $singlePortDiagnosis
    SlcanLikeProbePorts = $slcanLikeProbePorts
    WindowsMissionPlannerDiagnosis = $windowsMissionPlannerDiagnosis
    WindowsAcceptanceMode = if ($null -ne $usbSummary) { $usbSummary.WindowsAcceptanceMode } else { $null }
    MI00ComCount = if ($null -ne $usbSummary) { $usbSummary.MI00ComCount } else { $null }
    MI02ComCount = if ($null -ne $usbSummary) { $usbSummary.MI02ComCount } else { $null }
    MI00UsbserComCount = if ($null -ne $usbSummary) { $usbSummary.MI00UsbserComCount } else { $null }
    MI02UsbserComCount = if ($null -ne $usbSummary) { $usbSummary.MI02UsbserComCount } else { $null }
    TargetNoMiComCount = if ($null -ne $usbSummary) { $usbSummary.TargetNoMiComCount } else { $null }
    TargetParentDeviceCount = if ($null -ne $usbSummary) { $usbSummary.TargetParentDeviceCount } else { $null }
    TargetInterfaceDeviceCount = if ($null -ne $usbSummary) { $usbSummary.TargetInterfaceDeviceCount } else { $null }
    TargetNoMiDeviceCount = if ($null -ne $usbSummary) { $usbSummary.TargetNoMiDeviceCount } else { $null }
    TargetNoMiUsbserComCount = if ($null -ne $usbSummary) { $usbSummary.TargetNoMiUsbserComCount } else { $null }
    TargetNoMiManualProbeComCount = if ($null -ne $usbSummary) { $usbSummary.TargetNoMiManualProbeComCount } else { $null }
    Diagnostic5741MavlinkComCount = if ($null -ne $usbSummary) { $usbSummary.Diagnostic5741MavlinkComCount } else { $null }
    TargetTopology = if ($null -ne $usbSummary) { $usbSummary.TargetTopology } else { $null }
    UsbDiagnosticOnly = $usbDiagnosticOnly
    ProbeComOpen = [bool]$ProbeComOpen
    AllowDiagnostic5741 = [bool]$AllowDiagnostic5741
    PowerShellExe = $powershellExe
    MavlinkPortRequested = $MavlinkPort
    MavlinkPortEffective = $effectiveMavlinkPort
    ComOpenProbeResults = if ($null -ne $usbSummary) { $usbSummary.ComOpenProbeResults } else { @() }
    SlcanAsciiProbeResults = if ($null -ne $usbSummary) { $usbSummary.SlcanAsciiProbeResults } else { @() }
    MavlinkReturnCode = $mavlinkRc
    MavlinkSummaryPath = if ($null -ne $mavlinkJsonPath) { $mavlinkJsonPath.FullName } else { $null }
    MavlinkVerdict = if ($null -ne $mavlinkSummary) { $mavlinkSummary.verdict } else { $null }
    MavlinkPort = if ($null -ne $mavlinkSummary) { $mavlinkSummary.port } else { $null }
    MavlinkReason = if ($null -ne $mavlinkSummary) { $mavlinkSummary.reason } else { $null }
    MavlinkAcceptanceMode = $mavlinkAcceptanceMode
    MavlinkAcceptedUsbIdentity = $mavlinkAcceptedUsbIdentity
    MavlinkIsFinalDualCdcAcceptance = $mavlinkFinalDualCdc
    MavlinkDataPathDiagnosticOnly = [bool]($mavlinkNoMiRawGateOnly -or $mavlinkManualUnlistedRawGate)
    RawMavlinkProbeVerdict = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.raw_mavlink_probe) { $mavlinkSummary.raw_mavlink_probe.verdict } else { $null }
    RawMavlinkProbeReason = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.raw_mavlink_probe) { $rawProbeReason } else { $null }
    RawMavlinkProbeBytes = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.raw_mavlink_probe) { $rawProbeBytes } else { $null }
    RawMavlinkProbeMagicCount = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.raw_mavlink_probe) { $rawMagicCount } else { $null }
    RawNaturalOpenVerdict = $rawNaturalVerdict
    RawNaturalOpenBytes = $rawNaturalBytes
    RawNaturalOpenMagicCount = $rawNaturalMagicCount
    RawForcedDtrRtsVerdict = $rawForcedVerdict
    RawForcedDtrRtsBytes = $rawForcedBytes
    RawForcedDtrRtsMagicCount = $rawForcedMagicCount
    MavlinkFailureClass = $mavlinkFailureClass
    ManualPortCheckVerdict = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.manual_port_check) { $mavlinkSummary.manual_port_check.verdict } else { $null }
    ManualPortCheckReason = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.manual_port_check) { $mavlinkSummary.manual_port_check.reason } else { $null }
    ManualPortCheckInfo = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.manual_port_check) { $mavlinkSummary.manual_port_check.port_info } else { $null }
    FirmwareSnapshotCommand = $firmwareSnapshotCommand
    WindowsRetestCommandHint = $windowsRetestCommandHint
    TargetTopologyHint = if ($null -ne $usbSummary -and $null -ne $usbSummary.TargetTopology) {
        [pscustomobject]@{
            TargetParentDeviceCount = $usbSummary.TargetTopology.TargetParentDeviceCount
            TargetInterfaceDeviceCount = $usbSummary.TargetTopology.TargetInterfaceDeviceCount
            MI00ComCount = $usbSummary.TargetTopology.MI00ComCount
            MI02ComCount = $usbSummary.TargetTopology.MI02ComCount
            MI00UsbserComCount = $usbSummary.TargetTopology.MI00UsbserComCount
            MI02UsbserComCount = $usbSummary.TargetTopology.MI02UsbserComCount
        }
    } else {
        $null
    }
    EvidenceFilesToCopy = $evidenceFilesToCopy
    Overall = $overall
    NextAction = if ($overall -eq "GREEN_DIAGNOSTIC_MAVLINK") {
        "Diagnostic 1209:5741 single-CDC MAVLink passed. Test this COM in Mission Planner to isolate Windows CDC behavior; this is not final dual-CDC/SLCAN acceptance."
    } elseif ($overall -eq "GREEN_MAVLINK") {
        "Open Mission Planner and connect the listed MAVLink COM port. Do not select the SLCAN COM port."
    } elseif ($windowsMissionPlannerDiagnosis -eq "ONLY_VISIBLE_ARDUPILOT_COM_IS_SLCAN") {
        "The only visible ArduPilot/CUAV/APM-like COM is classified as SLCAN or answered SLCAN ASCII. Do not use it in Mission Planner. Bind or recover the VID_1209&PID_5740&MI_00 MAVLink COM with -BindUsbser -BindUsbserRescan -ProbeComOpen."
    } elseif ($windowsMissionPlannerDiagnosis -eq "ONLY_VISIBLE_ARDUPILOT_COM_HAS_NO_MI_TAG") {
        "The only visible 1209:5740 COM has no MI_00/MI_02 identity. Rerun with -BindUsbser -BindUsbserRescan -ProbeComOpen; if still no MI tag, run -MavlinkPort <COM> and require raw MAVLink, heartbeat, and full parameter download before Mission Planner testing."
    } elseif ($windowsMissionPlannerDiagnosis -eq "MI00_DEVICE_PRESENT_BUT_NO_COM") {
        "Windows sees the MI_00 MAVLink interface but did not create a COM port. Bind USB\\VID_1209&PID_5740&MI_00 to usbser/USB Serial Device, then rerun this acceptance script."
    } elseif ($overall -eq "YELLOW_MAVLINK_DATA_PATH_UNLISTED_COM") {
        "The manually selected COM was not present in Windows pyserial/CIM/PnP enumeration. It passed raw MAVLink, heartbeat, and parameter download, but it remains data-path evidence only; recover a Windows-visible MI_00 COM before final Mission Planner acceptance."
    } elseif ($overall -eq "YELLOW_MAVLINK_DATA_PATH_NO_MI_TAG") {
        "The selected COM passes raw MAVLink, heartbeat, and parameter download, but Windows did not expose a final MI_00/equivalent MAVLink identity. Keep using it only as data-path evidence; fix/confirm USB identity before final Mission Planner acceptance."
    } elseif ($mavlinkSummary -and $mavlinkSummary.acceptance_mode -eq "WINDOWS_MANUAL_COM_UNLISTED_RAW_GATE") {
        "The manually selected COM was not present in Windows pyserial/CIM/PnP enumeration. It may still prove raw MAVLink, heartbeat, and parameter download, but it remains data-path evidence only; recover a Windows-visible MI_00 COM before final Mission Planner acceptance."
    } elseif ($mavlinkFailureClass -eq "COM_OPENED_NO_RAW_MAVLINK_BYTES") {
        "The COM opened but no MAVLink bytes were seen. Immediately run the firmware snapshot command on the Linux/OpenOCD host and compare DTR, line coding, bulk_out, tx_start, and bulk_in counters."
    } elseif ($mavlinkFailureClass -eq "COM_BUSY_OR_ACCESS_DENIED" -or $mavlinkFailureClass -eq "COM_OPEN_FAILED") {
        "The selected COM could not be opened. Close Mission Planner/serial monitors, rerun with -ProbeComOpen, and verify the selected port is the MAVLink COM."
    } elseif ($mavlinkFailureClass -eq "RAW_BYTES_PRESENT_HIGHER_LEVEL_FAILED") {
        "Raw MAVLink bytes exist but higher-level connection failed. Check baud/port selection, mavlink_stdout.txt, and Mission Planner log behavior."
    } elseif ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.manual_port_check -and $mavlinkSummary.manual_port_check.verdict -eq "RED") {
        "The manually selected COM port is not the current MAVLink MI_00 app port. Inspect ManualPortCheckReason and classified_flight_controller_com_ports.json, then select the MAVLINK_MI00 COM."
    } elseif ($overall -eq "YELLOW_USB_MAVLINK_COM_PRESENT") {
        "A MAVLink COM candidate exists but the MAVLink script did not pass. Check RawMavlinkProbe*, mavlink_stdout.txt, and make sure no app is holding the COM port."
    } elseif ($null -ne $usbSummary -and $usbSummary.SingleNamedFlightControllerPort -eq $true) {
        "Only one ArduPilot/CUAV/APM-like COM is visible. Close Mission Planner and rerun with -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py, then inspect SingleNamedFlightControllerPortKind and classified_flight_controller_com_ports.json before using Mission Planner."
    } else {
        "Inspect usb_diag report and target_pnp_devices.json; Windows has not proven a usable MAVLink COM."
    }
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $summaryPath

$mpEvidenceTemplate = [pscustomobject]@{
    verdict = "RED"
    source = "Mission Planner"
    port = if ($null -ne $usbSummary -and $usbSummary.MissionPlannerComCandidates.Count -ge 1) {
        $usbSummary.MissionPlannerComCandidates[0]
    } elseif ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.port) {
        $mavlinkSummary.port
    } else {
        "COMx"
    }
    usb_identity = if ($null -ne $mavlinkSummary -and $null -ne $mavlinkSummary.accepted_usb_identity) {
        $mavlinkSummary.accepted_usb_identity
    } elseif ($usbDiagnosticOnly) {
        "VID_1209&PID_5741&DIAGNOSTIC_MAVLINK_SINGLE_CDC"
    } else {
        "VID_1209&PID_5740&MI_00"
    }
    connected_at = ""
    mission_planner_connected = $false
    param_download_complete = $false
    param_count = if ($null -ne $mavlinkSummary -and
                     $null -ne $mavlinkSummary.param_download -and
                     $null -ne $mavlinkSummary.param_download.reported_count) {
        $mavlinkSummary.param_download.reported_count
    } else {
        0
    }
    notes = if ($usbDiagnosticOnly) {
        "Diagnostic 1209:5741 single-CDC firmware: rename this file to mission_planner_evidence.json after Mission Planner connects and completes parameter download. This proves the diagnostic isolation gate only; final 1209:5740 dual-CDC/SLCAN still needs separate evidence."
    } elseif ($mavlinkNoMiRawGateOnly -or $mavlinkManualUnlistedRawGate) {
        "This run proved only a MAVLink data path without final MI_00 identity. Keep this RED unless Mission Planner connects to a Windows-visible current-app MAVLink COM and final MI_00/equivalent identity is proven."
    } else {
        "Rename this file to mission_planner_evidence.json after Mission Planner connects to the MAVLink COM and completes parameter download. Keep RED if Mission Planner cannot connect."
    }
}
$mpEvidenceTemplate | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $missionPlannerTemplatePath

if ($MissionPlannerEvidence -ne "") {
    Write-Output ""
    Write-Output "Step 2b/3: Copy Mission Planner evidence"
    $resolvedMpEvidence = Resolve-Path -Path $MissionPlannerEvidence -ErrorAction SilentlyContinue
    if ($null -ne $resolvedMpEvidence) {
        try {
            Copy-Item -Force -Path $resolvedMpEvidence.Path -Destination $missionPlannerEvidencePath
            Write-Output "Mission Planner evidence copied to: $missionPlannerEvidencePath"
        } catch {
            Write-Output "Could not copy Mission Planner evidence: $($_.Exception.Message)"
        }
    } else {
        Write-Output "Mission Planner evidence file not found: $MissionPlannerEvidence"
    }
}

$auditRc = $null
$auditSummary = $null
Write-Output ""
Write-Output "Step 3/3: Final evidence audit"
$auditArgs = @(
    ".\Tools\scripts\rtt_windows_acceptance_audit.py",
    $out,
    "--out",
    $auditSummaryPath
)
& $Python @auditArgs |
    Tee-Object -FilePath $auditLog
$auditRc = $LASTEXITCODE
if (Test-Path $auditSummaryPath) {
    try {
        $auditSummary = Get-Content $auditSummaryPath -Raw | ConvertFrom-Json
    } catch {
        Write-Output "Could not parse audit summary: $($_.Exception.Message)"
    }
}

Write-Output ""
Write-Output "==== Overall Summary ===="
$summary | Format-List | Out-String | Write-Output
Write-Output "Summary JSON: $summaryPath"
Write-Output "Audit JSON: $auditSummaryPath"
Write-Output "Audit log: $auditLog"
if ($null -ne $auditSummary) {
    Write-Output "Audit verdict: $($auditSummary.verdict)"
    Write-Output "Audit reason: $($auditSummary.reason)"
} else {
    Write-Output "Audit return code: $auditRc"
}
Write-Output "Mission Planner evidence template: $missionPlannerTemplatePath"
Write-Output "Firmware snapshot command: $firmwareSnapshotCommandPath"

if ($RequireAuditGreen) {
    if ($null -ne $auditSummary -and $auditSummary.verdict -eq "GREEN") {
        exit 0
    }
    exit 2
}

if ($overall -eq "GREEN_MAVLINK" -or $overall -eq "GREEN_DIAGNOSTIC_MAVLINK") {
    exit 0
}
exit 2
