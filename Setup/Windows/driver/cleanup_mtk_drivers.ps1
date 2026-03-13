#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Remove old MediaTek USB drivers and device instances for clean installation.

.DESCRIPTION
  This script is called by the MSI installer (or manually) before installing
  new mtkclient drivers.  It removes:
    1. Existing MediaTek USB device instances from Device Manager
       (VID 0x0E8D, 0x0FCE, 0x1004, 0x22D9)
    2. Old MTK OEM drivers from the Windows Driver Store
       (cdc-acm, android_winusb, usb2ser, mtkmbim, etc.)

  Device removal disables each device first, then removes it from the PnP tree,
  ensuring it disappears from Device Manager.  Driver store cleanup uses
  Get-WindowsDriver for reliable enumeration regardless of locale.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

# VID patterns for all MTK-related USB devices
$vidList = @('VID_0E8D', 'VID_0FCE', 'VID_1004', 'VID_22D9')

# ── Step 1: Remove existing MTK USB device instances ──────────────────────
Write-Host "=== Step 1: Removing MediaTek USB device instances ==="

# Get-PnpDevice without -InstanceId wildcard (more reliable across Windows versions)
$allDevices = @()
try {
    $allDevices = Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $id = $_.InstanceId
            foreach ($vid in $vidList) {
                if ($id -like "*$vid*") { return $true }
            }
            return $false
        }
} catch {
    Write-Host "  Get-PnpDevice failed: $($_.Exception.Message)"
}

$deviceCount = 0
foreach ($dev in $allDevices) {
    $deviceCount++
    $status = $dev.Status
    $name   = if ($dev.FriendlyName) { $dev.FriendlyName } else { $dev.Description }
    Write-Host "  Found: $($dev.InstanceId)"
    Write-Host "         Name: $name  Status: $status  Class: $($dev.Class)"

    # Step 1a: Disable the device first (required for removal of active devices)
    if ($status -eq 'OK' -or $status -eq 'Started') {
        try {
            Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
            Write-Host "         -> Disabled"
        } catch {
            Write-Host "         -> Disable failed (non-critical): $($_.Exception.Message)"
        }
    }

    # Step 1b: Remove the device from Device Manager
    # Try pnputil /remove-device first (Windows 10 v2004+)
    $removed = $false
    $result = & pnputil /remove-device "$($dev.InstanceId)" /subtree 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "         -> Removed (pnputil)"
        $removed = $true
    }

    # Fallback: Use WMI/CIM for removal if pnputil didn't work
    if (-not $removed) {
        try {
            $wmiDev = Get-CimInstance -ClassName Win32_PnPEntity -Filter "DeviceID='$($dev.InstanceId -replace '\\','\\\\')'" -ErrorAction SilentlyContinue
            if ($wmiDev) {
                $wmiResult = Invoke-CimMethod -InputObject $wmiDev -MethodName 'Disable' -ErrorAction SilentlyContinue
                # CIM doesn't have a Remove method, but disabling hides it from Device Manager
                Write-Host "         -> Disabled via CIM (device hidden from Device Manager)"
                $removed = $true
            }
        } catch {
            Write-Host "         -> CIM fallback failed: $($_.Exception.Message)"
        }
    }

    if (-not $removed) {
        Write-Host "         -> WARNING: Could not remove device (may need reboot)"
    }
}

if ($deviceCount -eq 0) {
    Write-Host "  No MediaTek USB devices found in Device Manager."
} else {
    Write-Host "  Processed $deviceCount device(s)."
}

# ── Step 2: Remove old MTK OEM drivers from Driver Store ──────────────────
Write-Host ""
Write-Host "=== Step 2: Removing old MediaTek drivers from Driver Store ==="

# Patterns that identify old MTK driver INFs (matched against OriginalFileName and ProviderName)
$infPatterns = @(
    'cdc-acm',
    'android_winusb',
    'androidwinusb',
    'usb2ser',
    'mtkmbim',
    'tetherxp',
    'mtkclient'
)
$providerPatterns = @(
    'MediaTek',
    'MTK',
    'Android'
)

$driverCount = 0

# Method 1: Get-WindowsDriver (most reliable — locale-independent)
try {
    $drivers = Get-WindowsDriver -Online -ErrorAction SilentlyContinue |
        Where-Object {
            $origName = $_.OriginalFileName
            $provider = $_.ProviderName
            $oemInf   = $_.Driver

            # Match by original INF filename
            foreach ($pat in $infPatterns) {
                if ($origName -match $pat) { return $true }
            }
            # Match by provider name + USB class
            if ($_.ClassName -match 'USB|Ports|AndroidUsb|Modem|Net') {
                foreach ($pat in $providerPatterns) {
                    if ($provider -match $pat) { return $true }
                }
            }
            return $false
        }

    foreach ($drv in $drivers) {
        $driverCount++
        Write-Host "  Found driver: $($drv.Driver)"
        Write-Host "    Original: $($drv.OriginalFileName)"
        Write-Host "    Provider: $($drv.ProviderName)  Class: $($drv.ClassName)"

        $result = & pnputil /delete-driver $drv.Driver /force /uninstall 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "    -> Deleted from Driver Store"
        } else {
            Write-Host "    -> WARNING: delete-driver returned exit code $LASTEXITCODE"
            Write-Host "       $result"
        }
    }
} catch {
    Write-Host "  Get-WindowsDriver failed: $($_.Exception.Message)"
    Write-Host "  Falling back to pnputil /enum-drivers..."

    # Method 2: Parse pnputil output (fallback)
    $patternRegex = ($infPatterns + $providerPatterns) -join '|'
    $driverOutput = & pnputil /enum-drivers 2>&1 | Out-String

    # Split by "Published Name" lines (works regardless of locale field name)
    $lines = $driverOutput -split "`r?`n"
    $currentOem = $null
    $blockText = ''

    foreach ($line in $lines) {
        if ($line -match '^\s*(oem\d+\.inf)') {
            # Process previous block
            if ($currentOem -and $blockText -match "(?i)($patternRegex)") {
                $driverCount++
                Write-Host "  Removing: $currentOem"
                $result = & pnputil /delete-driver $currentOem /force /uninstall 2>&1
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "    WARNING: exit code $LASTEXITCODE"
                }
            }
            $currentOem = $Matches[1]
            $blockText = $line
        } elseif ($line -match '(oem\d+\.inf)') {
            if ($currentOem -and $blockText -match "(?i)($patternRegex)") {
                $driverCount++
                Write-Host "  Removing: $currentOem"
                $result = & pnputil /delete-driver $currentOem /force /uninstall 2>&1
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "    WARNING: exit code $LASTEXITCODE"
                }
            }
            $currentOem = $Matches[1]
            $blockText = $line
        } else {
            $blockText += "`n$line"
        }
    }
    # Process last block
    if ($currentOem -and $blockText -match "(?i)($patternRegex)") {
        $driverCount++
        Write-Host "  Removing: $currentOem"
        & pnputil /delete-driver $currentOem /force /uninstall 2>&1 | Out-Null
    }
}

if ($driverCount -eq 0) {
    Write-Host "  No old MediaTek drivers found in Driver Store."
} else {
    Write-Host "  Removed $driverCount driver(s) from Driver Store."
}

# ── Step 3: Force re-scan USB bus ─────────────────────────────────────────
Write-Host ""
Write-Host "=== Step 3: Rescanning USB bus ==="
try {
    # Trigger a re-scan of all USB host controllers so Device Manager refreshes
    $usbControllers = Get-PnpDevice -Class USB -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -like 'PCI\*' -or $_.InstanceId -like 'ACPI\*' }
    foreach ($hc in $usbControllers) {
        & pnputil /scan-devices 2>&1 | Out-Null
        break  # One scan is enough
    }
    Write-Host "  USB bus re-scan triggered."
} catch {
    Write-Host "  USB re-scan skipped: $($_.Exception.Message)"
}

Write-Host ""
Write-Host "=== Cleanup complete ==="
exit 0
