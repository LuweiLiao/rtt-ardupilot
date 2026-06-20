param(
    [string]$OutDir = ".\rtt_windows_usb_refresh_evidence",
    [string]$VidPid = "VID_1209&PID_5740",
    [string]$LegacyVidPid = "VID_1209&PID_5741",
    [switch]$Apply,
    [switch]$RemovePresentTarget
)

$ErrorActionPreference = "Continue"
$timestamp = Get-Date -Format "yyyyMMddTHHmmss"
$out = Join-Path $OutDir $timestamp
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Convert-Device {
    param($Device)
    $hardwareIds = @()
    $service = $null
    $driverInf = $null
    try {
        $hardwareIds = @(Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_HardwareIds" -ErrorAction Stop).Data
    } catch {
        $hardwareIds = @()
    }
    try {
        $service = (Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_Service" -ErrorAction Stop).Data
    } catch {
        $service = $null
    }
    try {
        $driverInf = (Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_DriverInfPath" -ErrorAction Stop).Data
    } catch {
        $driverInf = $null
    }

    [pscustomobject]@{
        Class = $Device.Class
        FriendlyName = $Device.FriendlyName
        Name = $Device.Name
        InstanceId = $Device.InstanceId
        Manufacturer = $Device.Manufacturer
        Present = $Device.Present
        Problem = $Device.Problem
        Status = $Device.Status
        Service = $service
        DriverInfPath = $driverInf
        HardwareIds = $hardwareIds
    }
}

function Test-TargetDevice {
    param($Device)
    $text = "$($Device.InstanceId) $($Device.FriendlyName) $($Device.Name)"
    return (($text -match [regex]::Escape($VidPid)) -or ($text -match [regex]::Escape($LegacyVidPid)))
}

Write-Output "RTT Windows USB refresh evidence: $out"
Write-Output "Target app: $VidPid"
Write-Output "Legacy/bootloader: $LegacyVidPid"
Write-Output "Apply removals: $Apply"
Write-Output "Remove present target devices: $RemovePresentTarget"
Write-Output ""

$allDevices = @(Get-PnpDevice -ErrorAction SilentlyContinue)
$targetDevicesRaw = @($allDevices | Where-Object { Test-TargetDevice $_ })
$targetDevices = @($targetDevicesRaw | ForEach-Object { Convert-Device $_ })

$removeCandidates = @($targetDevices | Where-Object {
    ($_.Present -eq $false) -or
    ($RemovePresentTarget -and ($_.InstanceId -match [regex]::Escape($VidPid) -or $_.InstanceId -match [regex]::Escape($LegacyVidPid)))
})

$removalResults = @()
foreach ($dev in $removeCandidates) {
    $result = [pscustomobject]@{
        InstanceId = $dev.InstanceId
        FriendlyName = $dev.FriendlyName
        Present = $dev.Present
        Removed = $false
        ExitCode = $null
        Output = $null
    }

    if ($Apply) {
        $pnputilOutput = & pnputil /remove-device "$($dev.InstanceId)" 2>&1
        $result.Removed = ($LASTEXITCODE -eq 0)
        $result.ExitCode = $LASTEXITCODE
        $result.Output = ($pnputilOutput | Out-String).Trim()
    }
    $removalResults += $result
}

$scanOutput = & pnputil /scan-devices 2>&1
$scanResult = [pscustomobject]@{
    ExitCode = $LASTEXITCODE
    Output = ($scanOutput | Out-String).Trim()
}

Start-Sleep -Seconds 2
$afterDevices = @(Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { Test-TargetDevice $_ } | ForEach-Object { Convert-Device $_ })

$summary = [pscustomobject]@{
    Timestamp = (Get-Date).ToString("o")
    EvidenceDir = $out
    Apply = [bool]$Apply
    RemovePresentTarget = [bool]$RemovePresentTarget
    BeforeTargetDeviceCount = $targetDevices.Count
    RemoveCandidateCount = $removeCandidates.Count
    RemovalAttemptCount = if ($Apply) { $removeCandidates.Count } else { 0 }
    RemovalSuccessCount = @($removalResults | Where-Object { $_.Removed }).Count
    AfterTargetDeviceCount = $afterDevices.Count
    ScanExitCode = $scanResult.ExitCode
    NextAction = if (-not $Apply -and $removeCandidates.Count -gt 0) {
        "Review removal_candidates.json, then rerun with -Apply to remove stale non-present ArduPilot USB nodes."
    } elseif ($Apply) {
        "Run rtt_windows_acceptance_all.ps1 again and use the MI_00 COM port in Mission Planner."
    } else {
        "No stale target nodes were found. Run rtt_windows_acceptance_all.ps1 and inspect MI_00/MI_02 classification."
    }
}

$targetDevices | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "target_devices_before.json")
$removeCandidates | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "removal_candidates.json")
$removalResults | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "removal_results.json")
$scanResult | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "scan_result.json")
$afterDevices | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "target_devices_after.json")
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "summary.json")

Write-Output "==== Summary ===="
$summary | Format-List | Out-String | Write-Output
Write-Output "Evidence directory: $out"

if ($Apply -and $summary.RemovalSuccessCount -lt $summary.RemovalAttemptCount) {
    exit 2
}
exit 0
