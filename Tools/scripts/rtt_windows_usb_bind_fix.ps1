param(
    [string]$InfPath = ".\Tools\windows_drivers\rtt_ardupilot_cdc\rtt_ardupilot_cdc_usbser.inf",
    [switch]$Install,
    [switch]$Rescan,
    [switch]$ProbeComOpen,
    [switch]$SelfTest,
    [string]$OutDir = ".\rtt_windows_usb_bind_fix_evidence"
)

$ErrorActionPreference = "Continue"

function Get-BindFixVerdict {
    param(
        [bool]$Install,
        $InstallExitCode,
        [bool]$Rescan,
        $ScanExitCode,
        $DiagSummary
    )
    if ($Install -and $InstallExitCode -ne 0) {
        return "RED_INF_INSTALL_FAILED"
    }
    if ($Rescan -and $ScanExitCode -ne 0) {
        return "RED_PNP_RESCAN_FAILED"
    }
    if ($null -eq $DiagSummary) {
        return "RED_NO_USB_DIAG_SUMMARY"
    }
    if ($DiagSummary.Verdict -eq "GREEN_TWO_COM_PORTS") {
        return "GREEN_TWO_COM_PORTS_AFTER_BIND"
    }
    if ($DiagSummary.Verdict -eq "YELLOW_MAVLINK_ONLY") {
        return "YELLOW_MI00_AFTER_BIND_MI02_MISSING"
    }
    if ($DiagSummary.Verdict -eq "RED_MI00_PRESENT_NO_COM" -or
        $DiagSummary.Verdict -eq "RED_TARGET_DEVICE_NO_MAVLINK_COM") {
        return "RED_USB_SERIAL_BINDING_STILL_INCOMPLETE"
    }
    return "YELLOW_OR_RED_SEE_USB_DIAG"
}

function Get-BindFixNextAction {
    param(
        [string]$BindFixVerdict,
        $DiagNextAction
    )
    switch ($BindFixVerdict) {
        "GREEN_TWO_COM_PORTS_AFTER_BIND" {
            return "Run rtt_windows_acceptance_all.ps1 -ProbeComOpen -Python py, then connect Mission Planner to the MI_00 COM candidate listed here. Use MI_02 only for SLCAN."
        }
        "YELLOW_MI00_AFTER_BIND_MI02_MISSING" {
            return "Run the MAVLink acceptance gate and Mission Planner on the MI_00 COM first. Then inspect MI_02 driver binding separately for SLCAN."
        }
        "RED_USB_SERIAL_BINDING_STILL_INCOMPLETE" {
            return "Windows still sees the target USB identity without a usable MI_00 COM. Open Device Manager on USB\VID_1209&PID_5740&MI_00 and manually choose the built-in USB Serial Device/usbser.sys driver, then rerun this script with -ProbeComOpen."
        }
        "RED_INF_INSTALL_FAILED" {
            return "INF install failed. Read InstallResult.Output; if Windows rejects the unsigned INF, use Device Manager to manually select the built-in USB Serial Device driver for MI_00."
        }
        "RED_PNP_RESCAN_FAILED" {
            return "PnP rescan failed. Read ScanResult.Output, then rerun diagnostics without changing the firmware."
        }
        "RED_NO_USB_DIAG_SUMMARY" {
            return "The post-bind USB diagnostic did not produce summary.json. Inspect usb_diag_stdout.txt for PowerShell or permission errors."
        }
        default {
            if ($null -ne $DiagNextAction) {
                return $DiagNextAction
            }
            return "Inspect the nested usb_diag summary and classified_flight_controller_com_ports.json."
        }
    }
}

if ($SelfTest) {
    $cases = @(
        @{
            Name = "green_two_com"
            Install = $true
            InstallExitCode = 0
            Rescan = $true
            ScanExitCode = 0
            Diag = [pscustomobject]@{ Verdict = "GREEN_TWO_COM_PORTS"; NextAction = "diag" }
            Want = "GREEN_TWO_COM_PORTS_AFTER_BIND"
        },
        @{
            Name = "mavlink_only"
            Install = $true
            InstallExitCode = 0
            Rescan = $true
            ScanExitCode = 0
            Diag = [pscustomobject]@{ Verdict = "YELLOW_MAVLINK_ONLY"; NextAction = "diag" }
            Want = "YELLOW_MI00_AFTER_BIND_MI02_MISSING"
        },
        @{
            Name = "binding_still_bad"
            Install = $true
            InstallExitCode = 0
            Rescan = $true
            ScanExitCode = 0
            Diag = [pscustomobject]@{ Verdict = "RED_MI00_PRESENT_NO_COM"; NextAction = "diag" }
            Want = "RED_USB_SERIAL_BINDING_STILL_INCOMPLETE"
        },
        @{
            Name = "install_failed"
            Install = $true
            InstallExitCode = 1
            Rescan = $false
            ScanExitCode = $null
            Diag = [pscustomobject]@{ Verdict = "GREEN_TWO_COM_PORTS"; NextAction = "diag" }
            Want = "RED_INF_INSTALL_FAILED"
        },
        @{
            Name = "no_diag_summary"
            Install = $false
            InstallExitCode = $null
            Rescan = $false
            ScanExitCode = $null
            Diag = $null
            Want = "RED_NO_USB_DIAG_SUMMARY"
        }
    )
    $results = @()
    foreach ($case in $cases) {
        $got = Get-BindFixVerdict `
            -Install $case.Install `
            -InstallExitCode $case.InstallExitCode `
            -Rescan $case.Rescan `
            -ScanExitCode $case.ScanExitCode `
            -DiagSummary $case.Diag
        $results += [pscustomobject]@{
            name = $case.Name
            want = $case.Want
            got = $got
            passed = ($got -eq $case.Want)
        }
    }
    $payload = [pscustomobject]@{
        verdict = if (@($results | Where-Object { -not $_.passed }).Count -eq 0) { "GREEN" } else { "RED" }
        tests = $results
    }
    $payload | ConvertTo-Json -Depth 8 | Write-Output
    if ($payload.verdict -ne "GREEN") {
        exit 2
    }
    exit 0
}

$timestamp = Get-Date -Format "yyyyMMddTHHmmss"
$out = Join-Path $OutDir $timestamp
New-Item -ItemType Directory -Force -Path $out | Out-Null

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

Write-Output "RTT Windows USB bind fix evidence: $out"
Write-Output "INF path: $InfPath"
Write-Output "Install: $Install"
Write-Output "Rescan: $Rescan"
Write-Output "ProbeComOpen: $ProbeComOpen"
Write-Output "PowerShellExe: $powershellExe"

$installResult = [pscustomobject]@{
    Attempted = [bool]$Install
    ExitCode = $null
    Output = $null
}

if ($Install) {
    $installOutput = & pnputil /add-driver $InfPath /install 2>&1
    $installResult.ExitCode = $LASTEXITCODE
    $installResult.Output = ($installOutput | Out-String).Trim()
}

$scanResult = [pscustomobject]@{
    Attempted = [bool]$Rescan
    ExitCode = $null
    Output = $null
}

if ($Rescan) {
    $scanOutput = & pnputil /scan-devices 2>&1
    $scanResult.ExitCode = $LASTEXITCODE
    $scanResult.Output = ($scanOutput | Out-String).Trim()
}

$diagArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", ".\Tools\scripts\rtt_windows_usb_diag.ps1",
    "-OutDir", (Join-Path $out "usb_diag")
)
if ($ProbeComOpen) {
    $diagArgs += "-ProbeComOpen"
}

Write-Output ""
Write-Output "Step: USB diagnostic after binding fix"
& $powershellExe @diagArgs | Tee-Object -FilePath (Join-Path $out "usb_diag_stdout.txt")

$diagSummaryPath = Get-ChildItem -Path (Join-Path $out "usb_diag") -Recurse -Filter "summary.json" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
$diagSummary = $null
if ($null -ne $diagSummaryPath) {
    try {
        $diagSummary = Get-Content $diagSummaryPath.FullName -Raw | ConvertFrom-Json
    } catch {
        Write-Output "Could not parse USB diagnostic summary: $($_.Exception.Message)"
    }
}

$diagVerdict = if ($null -ne $diagSummary) { $diagSummary.Verdict } else { $null }
$diagNextAction = if ($null -ne $diagSummary) { $diagSummary.NextAction } else { $null }
$mi00ComCount = if ($null -ne $diagSummary) { $diagSummary.MI00ComCount } else { $null }
$mi02ComCount = if ($null -ne $diagSummary) { $diagSummary.MI02ComCount } else { $null }
$mi00UsbserComCount = if ($null -ne $diagSummary) { $diagSummary.MI00UsbserComCount } else { $null }
$mi02UsbserComCount = if ($null -ne $diagSummary) { $diagSummary.MI02UsbserComCount } else { $null }
$singleNamedKind = if ($null -ne $diagSummary) { $diagSummary.SingleNamedFlightControllerPortKind } else { $null }
$missionPlannerComCandidates = if ($null -ne $diagSummary) { @($diagSummary.MissionPlannerComCandidates) } else { @() }
$slcanComCandidates = if ($null -ne $diagSummary) { @($diagSummary.SlcanComCandidates) } else { @() }

$bindFixVerdict = Get-BindFixVerdict `
    -Install ([bool]$Install) `
    -InstallExitCode $installResult.ExitCode `
    -Rescan ([bool]$Rescan) `
    -ScanExitCode $scanResult.ExitCode `
    -DiagSummary $diagSummary
$bindFixNextAction = Get-BindFixNextAction `
    -BindFixVerdict $bindFixVerdict `
    -DiagNextAction $diagNextAction

$summary = [pscustomobject]@{
    Timestamp = (Get-Date).ToString("o")
    EvidenceDir = $out
    InfPath = $InfPath
    Install = [bool]$Install
    Rescan = [bool]$Rescan
    ProbeComOpen = [bool]$ProbeComOpen
    PowerShellExe = $powershellExe
    InstallResult = $installResult
    ScanResult = $scanResult
    UsbDiagSummaryPath = if ($null -ne $diagSummaryPath) { $diagSummaryPath.FullName } else { $null }
    UsbDiagVerdict = $diagVerdict
    UsbDiagNextAction = $diagNextAction
    MissionPlannerComCandidates = $missionPlannerComCandidates
    SlcanComCandidates = $slcanComCandidates
    MI00ComCount = $mi00ComCount
    MI02ComCount = $mi02ComCount
    MI00UsbserComCount = $mi00UsbserComCount
    MI02UsbserComCount = $mi02UsbserComCount
    SingleNamedFlightControllerPortKind = $singleNamedKind
    BindFixVerdict = $bindFixVerdict
    NextAction = $bindFixNextAction
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out "summary.json")
Write-Output ""
Write-Output "==== Summary ===="
$summary | Format-List | Out-String | Write-Output
Write-Output "Evidence directory: $out"

if ($Install -and $installResult.ExitCode -ne 0) {
    exit 2
}
if ($Rescan -and $scanResult.ExitCode -ne 0) {
    exit 3
}
if ($bindFixVerdict -eq "GREEN_TWO_COM_PORTS_AFTER_BIND" -or
    $bindFixVerdict -eq "YELLOW_MI00_AFTER_BIND_MI02_MISSING") {
    exit 0
}
if ($bindFixVerdict -eq "YELLOW_OR_RED_SEE_USB_DIAG") {
    exit 1
}
exit 2
