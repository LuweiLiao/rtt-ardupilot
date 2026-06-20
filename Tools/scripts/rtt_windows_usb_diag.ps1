param(
    [string]$OutDir = ".\rtt_windows_usb_evidence",
    [string]$VidPid = "VID_1209&PID_5740",
    [string]$LegacyVidPid = "VID_1209&PID_5741",
    [switch]$AllowDiagnostic5741,
    [switch]$ProbeComOpen
)

$ErrorActionPreference = "Continue"
$timestamp = Get-Date -Format "yyyyMMddTHHmmss"
$out = Join-Path $OutDir $timestamp
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Write-Section {
    param([string]$Name)
    "`n==== $Name ===="
}

function Get-DevicePropertyData {
    param(
        [string]$InstanceId,
        [string]$KeyName
    )
    try {
        $props = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop
        return $props.Data
    } catch {
        return $null
    }
}

function Get-MI {
    param([string]$Text)
    if ($Text -match "MI_([0-9A-Fa-f]{2})") {
        return "MI_$($matches[1].ToUpperInvariant())"
    }
    return $null
}

function Get-ComName {
    param($Device)
    $name = ""
    if ($null -ne $Device.FriendlyName) {
        $name = [string]$Device.FriendlyName
    } elseif ($null -ne $Device.Name) {
        $name = [string]$Device.Name
    }
    if ($name -match "\(COM[0-9]+\)") {
        return $matches[0].Trim("(", ")")
    }
    return $null
}

function Test-DeviceProblem {
    param($Problem)
    if ($null -eq $Problem) {
        return $false
    }
    $text = ([string]$Problem).Trim()
    if ($text.Length -eq 0) {
        return $false
    }
    if ($text -eq "0" -or $text -eq "CM_PROB_NONE") {
        return $false
    }
    return $true
}

function Convert-PnpDevice {
    param($Device)
    $hardwareIds = @(Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_HardwareIds")
    $compatibleIds = @(Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_CompatibleIds")
    $locationPaths = @(Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_LocationPaths")
    $busReportedDeviceDesc = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_BusReportedDeviceDesc"
    $locationInfo = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_LocationInfo"
    $parent = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_Parent"
    $containerId = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_ContainerId"
    $service = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_Service"
    $driverInf = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_DriverInfPath"
    $driverProvider = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_DriverProvider"
    $driverVersion = Get-DevicePropertyData -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_DriverVersion"
    $mi = Get-MI -Text $Device.InstanceId
    if ($null -eq $mi) {
        foreach ($candidate in @($hardwareIds + $compatibleIds + $locationPaths + @(
            $busReportedDeviceDesc,
            $locationInfo,
            $parent
        ))) {
            $mi = Get-MI -Text ([string]$candidate)
            if ($null -ne $mi) {
                break
            }
        }
    }

    [pscustomobject]@{
        Class = $Device.Class
        FriendlyName = $Device.FriendlyName
        Name = $Device.Name
        InstanceId = $Device.InstanceId
        Manufacturer = $Device.Manufacturer
        Present = $Device.Present
        Problem = $Device.Problem
        HasProblem = Test-DeviceProblem -Problem $Device.Problem
        Status = $Device.Status
        MI = $mi
        COM = Get-ComName -Device $Device
        Service = $service
        UsbSerBound = Test-UsbSerBinding -Service $service -DriverInfPath $driverInf -DriverProvider $driverProvider
        DriverInfPath = $driverInf
        DriverProvider = $driverProvider
        DriverVersion = $driverVersion
        HardwareIds = $hardwareIds
        CompatibleIds = $compatibleIds
        LocationPaths = $locationPaths
        BusReportedDeviceDesc = $busReportedDeviceDesc
        LocationInfo = $locationInfo
        Parent = $parent
        ContainerId = $containerId
    }
}

function Convert-SerialPort {
    param($Port)
    $hardwareIds = @(Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_HardwareIds")
    $compatibleIds = @(Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_CompatibleIds")
    $locationPaths = @(Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_LocationPaths")
    $busReportedDeviceDesc = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_BusReportedDeviceDesc"
    $locationInfo = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_LocationInfo"
    $parent = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_Parent"
    $containerId = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_ContainerId"
    $service = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_Service"
    $driverInf = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_DriverInfPath"
    $driverProvider = Get-DevicePropertyData -InstanceId $Port.PNPDeviceID -KeyName "DEVPKEY_Device_DriverProvider"
    $mi = Get-MI -Text $Port.PNPDeviceID
    if ($null -eq $mi) {
        foreach ($candidate in @($hardwareIds + $compatibleIds + $locationPaths + @(
            $busReportedDeviceDesc,
            $locationInfo,
            $parent
        ))) {
            $mi = Get-MI -Text ([string]$candidate)
            if ($null -ne $mi) {
                break
            }
        }
    }
    [pscustomobject]@{
        DeviceID = $Port.DeviceID
        Name = $Port.Name
        Description = $Port.Description
        Manufacturer = $Port.Manufacturer
        PNPDeviceID = $Port.PNPDeviceID
        MI = $mi
        Status = $Port.Status
        Service = $service
        UsbSerBound = Test-UsbSerBinding -Service $service -DriverInfPath $driverInf -DriverProvider $driverProvider
        DriverInfPath = $driverInf
        DriverProvider = $driverProvider
        HardwareIds = $hardwareIds
        CompatibleIds = $compatibleIds
        LocationPaths = $locationPaths
        BusReportedDeviceDesc = $busReportedDeviceDesc
        LocationInfo = $locationInfo
        Parent = $parent
        ContainerId = $containerId
    }
}

function Convert-PnpPortToSerialPort {
    param($Device)
    $converted = Convert-PnpDevice -Device $Device
    if ($null -eq $converted.COM) {
        return $null
    }
    [pscustomobject]@{
        DeviceID = $converted.COM
        Name = $converted.FriendlyName
        Description = $converted.BusReportedDeviceDesc
        Manufacturer = $converted.Manufacturer
        PNPDeviceID = $converted.InstanceId
        MI = $converted.MI
        Status = $converted.Status
        Service = $converted.Service
        UsbSerBound = $converted.UsbSerBound
        DriverInfPath = $converted.DriverInfPath
        DriverProvider = $converted.DriverProvider
        HardwareIds = $converted.HardwareIds
        CompatibleIds = $converted.CompatibleIds
        LocationPaths = $converted.LocationPaths
        BusReportedDeviceDesc = $converted.BusReportedDeviceDesc
        LocationInfo = $converted.LocationInfo
        Parent = $converted.Parent
        ContainerId = $converted.ContainerId
        Source = "PnpDevicePorts"
    }
}

function Convert-TargetTopology {
    param(
        $Devices,
        $Ports
    )
    $targetParentDevices = @($Devices | Where-Object { $null -eq $_.MI })
    $targetInterfaceDevices = @($Devices | Where-Object { $null -ne $_.MI })
    $mi00DevicesLocal = @($Devices | Where-Object { $_.MI -eq "MI_00" })
    $mi02DevicesLocal = @($Devices | Where-Object { $_.MI -eq "MI_02" })
    $mi00PortsLocal = @($Ports | Where-Object { $_.MI -eq "MI_00" })
    $mi02PortsLocal = @($Ports | Where-Object { $_.MI -eq "MI_02" })

    [pscustomobject]@{
        TargetParentDeviceCount = $targetParentDevices.Count
        TargetInterfaceDeviceCount = $targetInterfaceDevices.Count
        TargetNoMiDeviceCount = $targetParentDevices.Count
        MI00DeviceCount = $mi00DevicesLocal.Count
        MI02DeviceCount = $mi02DevicesLocal.Count
        MI00ComCount = $mi00PortsLocal.Count
        MI02ComCount = $mi02PortsLocal.Count
        MI00UsbserComCount = @($mi00PortsLocal | Where-Object { $_.UsbSerBound }).Count
        MI02UsbserComCount = @($mi02PortsLocal | Where-Object { $_.UsbSerBound }).Count
        MI00ProblemDeviceCount = @($mi00DevicesLocal | Where-Object { $_.HasProblem }).Count
        MI02ProblemDeviceCount = @($mi02DevicesLocal | Where-Object { $_.HasProblem }).Count
        ParentDevices = @($targetParentDevices | Select-Object Class, FriendlyName, Name, Status, Problem, HasProblem, Service, DriverInfPath, UsbSerBound, InstanceId)
        MI00Devices = @($mi00DevicesLocal | Select-Object Class, FriendlyName, Name, Status, Problem, HasProblem, Service, DriverInfPath, UsbSerBound, InstanceId)
        MI02Devices = @($mi02DevicesLocal | Select-Object Class, FriendlyName, Name, Status, Problem, HasProblem, Service, DriverInfPath, UsbSerBound, InstanceId)
        MI00Ports = @($mi00PortsLocal | Select-Object DeviceID, Name, Description, Status, Service, UsbSerBound, DriverInfPath, PNPDeviceID)
        MI02Ports = @($mi02PortsLocal | Select-Object DeviceID, Name, Description, Status, Service, UsbSerBound, DriverInfPath, PNPDeviceID)
    }
}

function Test-UsbSerBinding {
    param(
        $Service,
        $DriverInfPath,
        $DriverProvider
    )
    $text = "$Service $DriverInfPath $DriverProvider"
    return ($text -match "(?i)\busbser\b" -or $text -match "(?i)usbser\.inf")
}

function Test-ComOpen {
    param([string]$PortName)
    $result = [pscustomobject]@{
        Port = $PortName
        Opened = $false
        Error = $null
    }
    $sp = $null
    try {
        $sp = New-Object System.IO.Ports.SerialPort $PortName, 115200, "None", 8, "One"
        $sp.ReadTimeout = 300
        $sp.WriteTimeout = 300
        $sp.DtrEnable = $true
        $sp.RtsEnable = $true
        $sp.Open()
        $result.Opened = $true
    } catch {
        $result.Error = $_.Exception.Message
    } finally {
        if ($null -ne $sp) {
            try {
                if ($sp.IsOpen) {
                    $sp.Close()
                }
                $sp.Dispose()
            } catch {
            }
        }
    }
    return $result
}

function Test-SlcanAscii {
    param([string]$PortName)
    $result = [pscustomobject]@{
        Port = $PortName
        Opened = $false
        SlcanLike = $false
        Error = $null
        Replies = @()
    }
    $sp = $null
    try {
        $sp = New-Object System.IO.Ports.SerialPort $PortName, 115200, "None", 8, "One"
        $sp.ReadTimeout = 250
        $sp.WriteTimeout = 250
        $sp.DtrEnable = $true
        $sp.RtsEnable = $true
        $sp.Open()
        $result.Opened = $true
        foreach ($cmd in @("V", "F", "N")) {
            try {
                $sp.DiscardInBuffer()
                $sp.Write("$cmd`r")
                Start-Sleep -Milliseconds 180
                $reply = $sp.ReadExisting()
                $result.Replies += [pscustomobject]@{
                    Cmd = $cmd
                    Reply = $reply
                }
                if ($reply -match "V[0-9A-Fa-f]{4}|F[0-9A-Fa-f]{2}|N[0-9A-Fa-f]+") {
                    $result.SlcanLike = $true
                }
            } catch {
                $result.Replies += [pscustomobject]@{
                    Cmd = $cmd
                    Reply = ""
                    Error = $_.Exception.Message
                }
            }
        }
    } catch {
        $result.Error = $_.Exception.Message
    } finally {
        if ($null -ne $sp) {
            try {
                if ($sp.IsOpen) {
                    $sp.Close()
                }
                $sp.Dispose()
            } catch {
            }
        }
    }
    return $result
}

function Get-PortKind {
    param($Port)
    $text = "$($Port.Name) $($Port.Description) $($Port.Manufacturer) $($Port.PNPDeviceID) $($Port.BusReportedDeviceDesc) $($Port.LocationInfo) $($Port.Parent) $($Port.HardwareIds) $($Port.CompatibleIds)"
    if ($Port.PNPDeviceID -match [regex]::Escape($VidPid) -and $Port.MI -eq "MI_00") {
        return "MAVLINK_MI00"
    }
    if ($Port.PNPDeviceID -match [regex]::Escape($VidPid) -and $Port.MI -eq "MI_02") {
        return "SLCAN_MI02"
    }
    if ($Port.PNPDeviceID -match [regex]::Escape($VidPid) -and $text -match "(?i)MAVLink") {
        return "MAVLINK_INTERFACE_STRING"
    }
    if ($Port.PNPDeviceID -match [regex]::Escape($VidPid) -and $text -match "(?i)SLCAN") {
        return "SLCAN_INTERFACE_STRING"
    }
    if ($Port.PNPDeviceID -match [regex]::Escape($VidPid)) {
        return "TARGET_5740_NO_MI_TAG"
    }
    if ($Port.PNPDeviceID -match [regex]::Escape($LegacyVidPid) -and $AllowDiagnostic5741) {
        return "DIAGNOSTIC_5741_MAVLINK_SINGLE_CDC"
    }
    if ($Port.PNPDeviceID -match [regex]::Escape($LegacyVidPid)) {
        return "LEGACY_5741_BOOTLOADER_OR_SINGLE_CDC"
    }
    if ($text -match "ArduPilot|APM|CUAV|PX4|MAVLink|SLCAN") {
        return "NAME_MATCH_ONLY_NOT_TARGET"
    }
    return "OTHER"
}

$allPnp = @(Get-PnpDevice -ErrorAction SilentlyContinue)
$allPresent = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue)
$classFocus = @("Ports", "USB", "USBDevice", "Modem", "Unknown")
$focusedDevices = @($allPresent | Where-Object {
    ($_.InstanceId -match [regex]::Escape($VidPid)) -or
    ($_.InstanceId -match [regex]::Escape($LegacyVidPid)) -or
    ($classFocus -contains $_.Class)
})

$targetDevices = @($allPresent | Where-Object { $_.InstanceId -match [regex]::Escape($VidPid) })
$legacyDevices = @($allPresent | Where-Object { $_.InstanceId -match [regex]::Escape($LegacyVidPid) })
$targetDeviceObjects = @($targetDevices | ForEach-Object { Convert-PnpDevice $_ })
$legacyDeviceObjects = @($legacyDevices | ForEach-Object { Convert-PnpDevice $_ })

$serialPortsRaw = @(Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue)
$serialPortsFromCim = @($serialPortsRaw | ForEach-Object {
    $p = Convert-SerialPort $_
    if ($null -ne $p) {
        $p | Add-Member -NotePropertyName Source -NotePropertyValue "Win32_SerialPort" -Force
        $p
    }
})
$serialPortsFromPnp = @($allPresent | Where-Object {
    $_.Class -eq "Ports" -and (Get-ComName -Device $_)
} | ForEach-Object { Convert-PnpPortToSerialPort -Device $_ })
$serialPorts = @($serialPortsFromCim + $serialPortsFromPnp | Where-Object { $null -ne $_ } |
    Sort-Object DeviceID, PNPDeviceID -Unique)
$targetPorts = @($serialPorts | Where-Object { $_.PNPDeviceID -match [regex]::Escape($VidPid) })
$legacyPorts = @($serialPorts | Where-Object { $_.PNPDeviceID -match [regex]::Escape($LegacyVidPid) })
$namedFlightControllerPorts = @($serialPorts | Where-Object {
    $text = "$($_.Name) $($_.Description) $($_.Manufacturer) $($_.PNPDeviceID)"
    $text -match "ArduPilot|APM|CUAV|PX4|MAVLink|SLCAN"
})
$flightControllerRelevantPorts = @(
    $namedFlightControllerPorts + $targetPorts + $legacyPorts |
    Where-Object { $null -ne $_ -and $_.DeviceID } |
    Sort-Object DeviceID, PNPDeviceID -Unique
)

$classifiedFlightControllerPorts = @($flightControllerRelevantPorts | ForEach-Object {
    [pscustomobject]@{
        DeviceID = $_.DeviceID
        Kind = Get-PortKind -Port $_
        Name = $_.Name
        Description = $_.Description
        Manufacturer = $_.Manufacturer
        MI = $_.MI
        Service = $_.Service
        UsbSerBound = $_.UsbSerBound
        DriverInfPath = $_.DriverInfPath
        DriverProvider = $_.DriverProvider
        PNPDeviceID = $_.PNPDeviceID
        Status = $_.Status
    }
})
$singleNamedFlightControllerPort = ($flightControllerRelevantPorts.Count -eq 1)
$singleNamedFlightControllerPortKind = if ($singleNamedFlightControllerPort) {
    Get-PortKind -Port $flightControllerRelevantPorts[0]
} else {
    $null
}
$singleNamedFlightControllerPortInfo = if ($singleNamedFlightControllerPort) {
    $p = $flightControllerRelevantPorts[0]
    [pscustomobject]@{
        DeviceID = $p.DeviceID
        Kind = $singleNamedFlightControllerPortKind
        Name = $p.Name
        Description = $p.Description
        Manufacturer = $p.Manufacturer
        PNPDeviceID = $p.PNPDeviceID
        MI = $p.MI
        Service = $p.Service
        UsbSerBound = $p.UsbSerBound
        DriverInfPath = $p.DriverInfPath
        BusReportedDeviceDesc = $p.BusReportedDeviceDesc
        LocationInfo = $p.LocationInfo
        Parent = $p.Parent
    }
} else {
    $null
}

$mi00Ports = @($targetPorts | Where-Object { $_.MI -eq "MI_00" })
$mi02Ports = @($targetPorts | Where-Object { $_.MI -eq "MI_02" })
$mavlinkInterfaceStringPorts = @($targetPorts | Where-Object {
    $text = "$($_.Name) $($_.Description) $($_.Manufacturer) $($_.PNPDeviceID) $($_.BusReportedDeviceDesc) $($_.LocationInfo) $($_.Parent) $($_.HardwareIds) $($_.CompatibleIds)"
    $_.MI -ne "MI_02" -and $text -match "(?i)MAVLink"
})
$slcanInterfaceStringPorts = @($targetPorts | Where-Object {
    $text = "$($_.Name) $($_.Description) $($_.Manufacturer) $($_.PNPDeviceID) $($_.BusReportedDeviceDesc) $($_.LocationInfo) $($_.Parent) $($_.HardwareIds) $($_.CompatibleIds)"
    $_.MI -ne "MI_00" -and $text -match "(?i)SLCAN"
})
$targetNoMiPorts = @($targetPorts | Where-Object { $null -eq $_.MI })
$targetNoMiUsbserPorts = @($targetNoMiPorts | Where-Object { $_.UsbSerBound })
$targetNoMiManualProbePorts = @()
if ($mi00Ports.Count -eq 0 -and $mavlinkInterfaceStringPorts.Count -eq 0) {
    $targetNoMiManualProbePorts = @($targetNoMiUsbserPorts)
}
$diagnostic5741MavlinkPorts = @()
if ($AllowDiagnostic5741 -and
    $targetPorts.Count -eq 0 -and
    $legacyPorts.Count -eq 1 -and
    $legacyPorts[0].UsbSerBound) {
    $diagnostic5741MavlinkPorts = @($legacyPorts[0])
}
$missionPlannerPorts = @($mi00Ports + $mavlinkInterfaceStringPorts + $diagnostic5741MavlinkPorts | Sort-Object DeviceID -Unique)
$slcanPorts = @($mi02Ports + $slcanInterfaceStringPorts | Sort-Object DeviceID -Unique)
$mi00Devices = @($targetDeviceObjects | Where-Object { $_.MI -eq "MI_00" })
$mi02Devices = @($targetDeviceObjects | Where-Object { $_.MI -eq "MI_02" })
$targetProblemDevices = @($targetDeviceObjects | Where-Object { $_.HasProblem })
$mi00UsbserPorts = @($missionPlannerPorts | Where-Object { $_.UsbSerBound })
$mi02UsbserPorts = @($slcanPorts | Where-Object { $_.UsbSerBound })
$targetTopology = Convert-TargetTopology -Devices $targetDeviceObjects -Ports $targetPorts

$probePorts = @()
$comOpenProbeResults = @()
$slcanAsciiProbeResults = @()
if ($ProbeComOpen) {
    $probePorts = @($classifiedFlightControllerPorts | Where-Object {
        $_.DeviceID -and ($_.Kind -ne "OTHER")
    } | Select-Object -ExpandProperty DeviceID -Unique)
    $comOpenProbeResults = @($probePorts | ForEach-Object { Test-ComOpen -PortName $_ })
    $slcanAsciiProbeResults = @($probePorts | ForEach-Object { Test-SlcanAscii -PortName $_ })
}

$slcanLikeProbePorts = @($slcanAsciiProbeResults |
    Where-Object { $_.SlcanLike } |
    Select-Object -ExpandProperty Port -Unique)

$singleVisibleSlcanPort = $false
if ($singleNamedFlightControllerPort -and
    $slcanLikeProbePorts -contains [string]$singleNamedFlightControllerPortInfo.DeviceID) {
    $singleVisibleSlcanPort = $true
}

$verdict = if ($singleVisibleSlcanPort) {
    "RED_SINGLE_VISIBLE_SLCAN_NOT_MAVLINK"
} elseif ($missionPlannerPorts.Count -ge 1 -and $slcanPorts.Count -ge 1) {
    "GREEN_TWO_COM_PORTS"
} elseif ($diagnostic5741MavlinkPorts.Count -eq 1) {
    "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY"
} elseif ($missionPlannerPorts.Count -ge 1) {
    "YELLOW_MAVLINK_ONLY"
} elseif ($targetNoMiManualProbePorts.Count -eq 1) {
    "YELLOW_SINGLE_TARGET_COM_NO_MI_TAG"
} elseif ($mi00Devices.Count -ge 1) {
    "RED_MI00_PRESENT_NO_COM"
} elseif ($targetDevices.Count -ge 1) {
    "RED_TARGET_DEVICE_NO_MAVLINK_COM"
} elseif ($legacyDevices.Count -ge 1) {
    "RED_ONLY_LEGACY_5741_VISIBLE"
} else {
    "RED_TARGET_DEVICE_MISSING"
}

$nextAction = switch ($verdict) {
    "GREEN_TWO_COM_PORTS" {
        "Mission Planner must use the MI_00 COM port. MI_02 is SLCAN."
    }
    "RED_SINGLE_VISIBLE_SLCAN_NOT_MAVLINK" {
        "The only visible ArduPilot/CUAV/APM-like COM answered SLCAN ASCII. Do not use it in Mission Planner. It is the CAN/SLCAN pipe, not MAVLink. Bind or recover the VID_1209&PID_5740&MI_00 MAVLink COM and rerun the acceptance scripts."
    }
    "YELLOW_MAVLINK_ONLY" {
        "Use the MI_00 COM port for Mission Planner first. Investigate MI_02 driver binding separately for SLCAN."
    }
    "YELLOW_SINGLE_TARGET_COM_NO_MI_TAG" {
        "Windows shows exactly one current 1209:5740 COM without MI_00/MI_02 identity. Close Mission Planner. First run rtt_windows_acceptance_all.ps1 -BindUsbser -BindUsbserRescan -ProbeComOpen -Python py so Windows binds both MI_00 and MI_02 to usbser if possible. If it still reports TARGET_5740_NO_MI_TAG, run rtt_windows_acceptance_all.ps1 -MavlinkPort <COM> -ProbeComOpen -Python py and use this COM only if raw MAVLink, heartbeat, and full parameter download pass. Final acceptance still needs MI_00/equivalent identity or Mission Planner proof on the same COM."
    }
    "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY" {
        "Use the single 1209:5741 diagnostic MAVLink COM in Mission Planner. This intentionally has no SLCAN port and is for isolating Windows/Mission Planner CDC behavior."
    }
    "RED_MI00_PRESENT_NO_COM" {
        "Windows sees the MAVLink interface but did not create a COM port. Check Device Manager for this MI_00 node and bind it to usbser/USB Serial Device."
    }
    "RED_TARGET_DEVICE_NO_MAVLINK_COM" {
        "Windows sees VID/PID 1209:5740 but no MI_00 COM. Inspect target_pnp_devices.json for problem codes and driver service, then bind USB\\VID_1209&PID_5740&MI_00 to usbser/USB Serial Device or run rtt_windows_acceptance_all.ps1 -BindUsbser -ProbeComOpen."
    }
    "RED_ONLY_LEGACY_5741_VISIBLE" {
        "Only the bootloader/single-CDC path is visible. The app did not enumerate as 1209:5740 on Windows, or Windows is showing stale legacy state."
    }
    default {
        "No target ArduPilot USB device was visible. Confirm the app is running and the USB cable is attached to the flight controller."
    }
}

$singlePortDiagnosis = if ($singleNamedFlightControllerPort) {
    $singlePort = [string]$singleNamedFlightControllerPortInfo.DeviceID
    if ($slcanLikeProbePorts -contains $singlePort) {
        "SINGLE_VISIBLE_PORT_RESPONDS_SLCAN_ASCII_NOT_MAVLINK"
    } elseif ($singleNamedFlightControllerPortKind -eq "SLCAN_MI02" -or
              $singleNamedFlightControllerPortKind -eq "SLCAN_INTERFACE_STRING") {
        "SINGLE_VISIBLE_PORT_IS_SLCAN_NOT_MAVLINK"
    } elseif ($singleNamedFlightControllerPortKind -eq "MAVLINK_MI00" -or
              $singleNamedFlightControllerPortKind -eq "MAVLINK_INTERFACE_STRING") {
        "SINGLE_VISIBLE_PORT_IS_MAVLINK_CANDIDATE"
    } elseif ($singleNamedFlightControllerPortKind -eq "TARGET_5740_NO_MI_TAG") {
        "SINGLE_VISIBLE_TARGET_5740_NO_MI_TAG_REQUIRES_RAW_MAVLINK_GATE"
    } elseif ($singleNamedFlightControllerPortKind -eq "DIAGNOSTIC_5741_MAVLINK_SINGLE_CDC") {
        "SINGLE_VISIBLE_DIAGNOSTIC_5741_MAVLINK_ONLY"
    } elseif ($singleNamedFlightControllerPortKind -eq "LEGACY_5741_BOOTLOADER_OR_SINGLE_CDC") {
        "SINGLE_VISIBLE_LEGACY_5741_NOT_FINAL_DUAL_CDC"
    } elseif ($singleNamedFlightControllerPortKind -eq "NAME_MATCH_ONLY_NOT_TARGET") {
        "SINGLE_VISIBLE_NAME_MATCH_ONLY_NOT_TARGET_5740"
    } else {
        "SINGLE_VISIBLE_PORT_UNCLASSIFIED"
    }
} else {
    $null
}

$summary = [pscustomobject]@{
    Timestamp = (Get-Date).ToString("o")
    ExpectedVidPid = $VidPid
    LegacyVidPid = $LegacyVidPid
    AllowDiagnostic5741 = [bool]$AllowDiagnostic5741
    TargetDeviceCount = $targetDevices.Count
    TargetComCount = $targetPorts.Count
    LegacyDeviceCount = $legacyDevices.Count
    LegacyComCount = $legacyPorts.Count
    NamedFlightControllerComCount = $namedFlightControllerPorts.Count
    TargetOrNamedFlightControllerComCount = $flightControllerRelevantPorts.Count
    SingleNamedFlightControllerPort = $singleNamedFlightControllerPort
    SingleNamedFlightControllerPortKind = $singleNamedFlightControllerPortKind
    SingleNamedFlightControllerPortInfo = $singleNamedFlightControllerPortInfo
    TargetProblemDeviceCount = $targetProblemDevices.Count
    TargetParentDeviceCount = $targetTopology.TargetParentDeviceCount
    TargetInterfaceDeviceCount = $targetTopology.TargetInterfaceDeviceCount
    TargetNoMiDeviceCount = $targetTopology.TargetNoMiDeviceCount
    MI00DeviceCount = $mi00Devices.Count
    MI02DeviceCount = $mi02Devices.Count
    MI00ComCount = $mi00Ports.Count
    MI02ComCount = $mi02Ports.Count
    MavlinkInterfaceStringComCount = $mavlinkInterfaceStringPorts.Count
    SlcanInterfaceStringComCount = $slcanInterfaceStringPorts.Count
    MI00UsbserComCount = $mi00UsbserPorts.Count
    MI02UsbserComCount = $mi02UsbserPorts.Count
    TargetNoMiComCount = $targetNoMiPorts.Count
    TargetNoMiUsbserComCount = $targetNoMiUsbserPorts.Count
    TargetNoMiManualProbeComCount = $targetNoMiManualProbePorts.Count
    TargetNoMiManualProbeComCandidates = @($targetNoMiManualProbePorts | ForEach-Object { $_.DeviceID })
    Diagnostic5741MavlinkComCount = $diagnostic5741MavlinkPorts.Count
    TargetTopology = $targetTopology
    MissionPlannerComCandidates = @($missionPlannerPorts | ForEach-Object { $_.DeviceID })
    SlcanComCandidates = @($slcanPorts | ForEach-Object { $_.DeviceID })
    ProbeComOpen = [bool]$ProbeComOpen
    ComOpenProbeResults = $comOpenProbeResults
    SlcanAsciiProbeResults = $slcanAsciiProbeResults
    SlcanLikeProbePorts = $slcanLikeProbePorts
    SinglePortDiagnosis = $singlePortDiagnosis
    Verdict = $verdict
    WindowsAcceptanceMode = if ($verdict -eq "YELLOW_DIAGNOSTIC_5741_MAVLINK_ONLY") {
        "DIAGNOSTIC_SINGLE_CDC_5741_MAVLINK_ONLY"
    } elseif ($verdict -eq "GREEN_TWO_COM_PORTS") {
        "FINAL_DUAL_CDC_5740_MAVLINK_AND_SLCAN"
    } elseif ($verdict -eq "YELLOW_MAVLINK_ONLY") {
        "FINAL_OR_FALLBACK_MAVLINK_ONLY_NEEDS_MAVLINK_GATE"
    } elseif ($verdict -eq "YELLOW_SINGLE_TARGET_COM_NO_MI_TAG") {
        "FINAL_DUAL_CDC_5740_SINGLE_COM_NO_MI_TAG_RAW_GATE"
    } else {
        "NOT_ACCEPTED"
    }
    NextAction = $nextAction
}

$reportText = @()
$reportText += Write-Section "Summary"
$reportText += ($summary | Format-List | Out-String)
$reportText += Write-Section "Mission Planner"
if ($missionPlannerPorts.Count -ge 1) {
    $reportText += "Use this MAVLink COM port in Mission Planner. Prefer MAVLINK_MI00 for the final 1209:5740 firmware. DIAGNOSTIC_5741_MAVLINK_SINGLE_CDC is valid only for the explicit single-CDC diagnostic firmware after the MAVLink gate is GREEN:"
    $reportText += ($missionPlannerPorts | Select-Object DeviceID, Name, Description, BusReportedDeviceDesc, MI, Service, UsbSerBound, DriverInfPath, PNPDeviceID | Format-Table -AutoSize | Out-String)
    if ($mi00UsbserPorts.Count -lt $missionPlannerPorts.Count) {
        $reportText += "Warning: at least one MAVLink candidate is not reported as usbser-bound. Inspect target_com_ports.json and Device Manager driver details."
    }
    if ($diagnostic5741MavlinkPorts.Count -eq 1) {
        $reportText += "Diagnostic note: this is the single $LegacyVidPid MAVLink-only firmware. It intentionally has no MI_02/SLCAN COM and is not the final dual-CDC acceptance identity."
    }
} else {
    $reportText += "No MI_00/current-app MAVLink COM port was found for $VidPid."
    if ($targetNoMiManualProbePorts.Count -ge 1) {
        $reportText += "Current-app COM ports exist but have no MI_00/MI_02 identity. They are not automatically accepted for Mission Planner. Manually run rtt_windows_acceptance_all.ps1 -MavlinkPort COMx -ProbeComOpen and require raw MAVLink bytes, heartbeat, and fast parameter download before using Mission Planner."
        $reportText += ($targetNoMiManualProbePorts | Select-Object DeviceID, Name, Description, BusReportedDeviceDesc, MI, Service, UsbSerBound, DriverInfPath, PNPDeviceID | Format-Table -AutoSize | Out-String)
    }
    if ($mi00Devices.Count -ge 1) {
        $reportText += "MI_00 device nodes are present but not bound to COM:"
        $reportText += ($mi00Devices | Select-Object FriendlyName, Class, Status, Problem, Service, DriverInfPath, InstanceId | Format-List | Out-String)
    }
}
$reportText += Write-Section "Target Or ArduPilot/CUAV/APM-like COM Ports"
if ($flightControllerRelevantPorts.Count -ge 1) {
    $reportText += (
        $classifiedFlightControllerPorts |
        Select-Object DeviceID, Kind, Name, Description, Manufacturer, MI, Service, UsbSerBound, DriverInfPath, PNPDeviceID, Status |
        Format-Table -AutoSize |
        Out-String
    )
    if ($singleNamedFlightControllerPort) {
        $reportText += "Only one target or ArduPilot/CUAV/APM-like COM port is visible. Classification: $singleNamedFlightControllerPortKind"
        if ($singlePortDiagnosis) {
            $reportText += "Single-port diagnosis: $singlePortDiagnosis"
        }
        $reportText += "Single-port identity:"
        $reportText += ($singleNamedFlightControllerPortInfo | Format-List | Out-String)
        if ($singleVisibleSlcanPort) {
            $reportText += "This single visible COM answered SLCAN ASCII probes. It is the CAN/SLCAN pipe, not a Mission Planner MAVLink port."
        }
        if ($singleNamedFlightControllerPortKind -eq "TARGET_5740_NO_MI_TAG") {
            $reportText += "This COM uses the current RTT app VID/PID but Windows did not expose MI_00/MI_02 in Win32_SerialPort. It is not an automatic Mission Planner port. Run rtt_windows_acceptance_all.ps1 with -MavlinkPort COMx -ProbeComOpen and use it only if raw MAVLink, heartbeat, and fast parameter download are GREEN."
        }
        if ($singleNamedFlightControllerPortKind -eq "DIAGNOSTIC_5741_MAVLINK_SINGLE_CDC") {
            $reportText += "This is the RTT diagnostic single-CDC app if PNPDeviceID contains VID_1209&PID_5741 and the device serial/descriptor matches RTT5741M/CUAV V5 MAVLink CDC. It is valid only with -AllowDiagnostic5741 and only after the MAVLink gate is GREEN."
        }
    }
    $reportText += "Interpretation: Mission Planner must use MAVLINK_MI00. A TARGET_5740_NO_MI_TAG COM is only a manual diagnostic candidate until the same COM passes raw MAVLink, heartbeat, and fast parameter download. If the single visible COM answered SLCAN ASCII, it is not a Mission Planner port. With -AllowDiagnostic5741, DIAGNOSTIC_5741_MAVLINK_SINGLE_CDC is valid only for the MAVLink-only isolation firmware. SLCAN_MI02 is for SLCAN. LEGACY_5741_BOOTLOADER_OR_SINGLE_CDC without the diagnostic flag is not the current RTT app MAVLink CDC."
} else {
    $reportText += "No target VID/PID or ArduPilot/CUAV/APM-like COM ports were found by Win32_SerialPort/PnP Ports."
}
$reportText += Write-Section "SLCAN"
if ($slcanPorts.Count -ge 1) {
    $reportText += "Use this SLCAN COM port for SLCAN:"
    $reportText += ($slcanPorts | Select-Object DeviceID, Name, Description, BusReportedDeviceDesc, MI, Service, UsbSerBound, DriverInfPath, PNPDeviceID | Format-Table -AutoSize | Out-String)
} else {
    $reportText += "No MI_02 or SLCAN-interface-string COM port was found for $VidPid."
    if ($mi02Devices.Count -ge 1) {
        $reportText += "MI_02 device nodes are present but not bound to COM:"
        $reportText += ($mi02Devices | Select-Object FriendlyName, Class, Status, Problem, Service, DriverInfPath, InstanceId | Format-List | Out-String)
    }
}
$reportText += Write-Section "Target PnP Devices"
$reportText += ($targetDeviceObjects | Format-List | Out-String)
$reportText += Write-Section "Target USB Topology"
$reportText += ($targetTopology | Format-List | Out-String)
$reportText += Write-Section "Target COM Ports"
$reportText += ($targetPorts | Format-List | Out-String)
if ($ProbeComOpen) {
    $reportText += Write-Section "COM Open Probe"
    $reportText += ($comOpenProbeResults | Format-Table -AutoSize | Out-String)
    $reportText += Write-Section "SLCAN ASCII Probe"
    $reportText += ($slcanAsciiProbeResults | Format-List | Out-String)
    if (@($slcanAsciiProbeResults | Where-Object { $_.SlcanLike }).Count -gt 0) {
        $reportText += "At least one visible ArduPilot/CUAV/APM-like COM answered SLCAN ASCII commands. Do not use that COM in Mission Planner; it is the SLCAN/CAN pipe, not MAVLink."
    }
}
$reportText += Write-Section "Target Problem Devices"
$reportText += ($targetProblemDevices | Format-List | Out-String)
$reportText += Write-Section "Legacy 5741 Devices"
$reportText += ($legacyDeviceObjects | Format-List | Out-String)
$reportText += Write-Section "Legacy 5741 COM Ports"
$reportText += ($legacyPorts | Format-List | Out-String)
$reportText += Write-Section "Focused Present PnP Devices"
$reportText += (@($focusedDevices | ForEach-Object { Convert-PnpDevice $_ }) |
    Sort-Object Class, FriendlyName, InstanceId |
    Select-Object Class, FriendlyName, Status, Problem, MI, COM, Service, InstanceId |
    Format-Table -AutoSize | Out-String)

$summary | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "summary.json")
$targetDeviceObjects | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "target_pnp_devices.json")
$targetPorts | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "target_com_ports.json")
$targetProblemDevices | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "target_problem_devices.json")
$targetTopology | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "target_usb_topology.json")
$legacyDeviceObjects | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "legacy_5741_pnp_devices.json")
$legacyPorts | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "legacy_5741_com_ports.json")
$namedFlightControllerPorts | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "named_flight_controller_com_ports.json")
$flightControllerRelevantPorts | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "target_or_named_flight_controller_com_ports.json")
$classifiedFlightControllerPorts | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "classified_flight_controller_com_ports.json")
$comOpenProbeResults | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "com_open_probe_results.json")
$slcanAsciiProbeResults | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "slcan_ascii_probe_results.json")
$serialPorts | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "all_serial_ports.json")
$serialPortsFromCim | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "all_serial_ports_win32_serialport.json")
$serialPortsFromPnp | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "all_serial_ports_pnp_ports.json")
@($focusedDevices | ForEach-Object { Convert-PnpDevice $_ }) |
    ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $out "focused_present_pnp_devices.json")
try {
    $setupApiLog = Join-Path $env:windir "INF\setupapi.dev.log"
    if (Test-Path $setupApiLog) {
        $setupApiTail = Get-Content -Path $setupApiLog -Tail 2500 -ErrorAction Stop
        $setupApiTail | Set-Content -Encoding UTF8 (Join-Path $out "setupapi.dev.tail.txt")
        $setupApiTail | Where-Object {
            $_ -match "VID_1209&PID_5740" -or
            $_ -match "VID_1209&PID_5741" -or
            $_ -match "RTT5740" -or
            $_ -match "RTT5741" -or
            $_ -match "VID_1209&PID_5740.*[0-9A-Fa-f]{24}" -or
            $_ -match "ArduPilot.*[0-9A-Fa-f]{24}"
        } | Set-Content -Encoding UTF8 (Join-Path $out "setupapi_target_matches.txt")
    }
} catch {
    "Could not collect setupapi.dev.log: $($_.Exception.Message)" |
        Set-Content -Encoding UTF8 (Join-Path $out "setupapi_collect_error.txt")
}
$reportText | Set-Content -Encoding UTF8 (Join-Path $out "report.txt")

$reportText | ForEach-Object { Write-Output $_ }
Write-Output ""
Write-Output "Evidence directory: $out"
