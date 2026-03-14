# install_driver.ps1
# MediaTek USB Serial Driver installer script
# Compatible with Windows 11 x64 and x86
# Works with SP Flash Tool and mtkclient

param(
    [string]$InfPath = ""
)

$ErrorActionPreference = "Stop"

function Write-Header {
    Write-Host ""
    Write-Host "=============================================" -ForegroundColor Cyan
    Write-Host "  MTK USB Serial Driver Installer" -ForegroundColor Cyan
    Write-Host "  For Preloader / BROM DA / Meta Mode" -ForegroundColor Cyan
    Write-Host "  Compatible: SP Flash Tool, mtkclient" -ForegroundColor Cyan
    Write-Host "=============================================" -ForegroundColor Cyan
    Write-Host ""
}

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-TestSigningEnabled {
    try {
        $bcdOutput = & bcdedit /enum "{current}" 2>&1
        foreach ($line in $bcdOutput) {
            if ($line -imatch "testsigning\s+Yes") {
                return $true
            }
        }
    } catch {
        # bcdedit not available or access denied
    }
    return $false
}

function Enable-TestSigning {
    Write-Host ""
    Write-Host "Enabling test signing mode..." -ForegroundColor Yellow
    $result = & bcdedit /set testsigning on 2>&1
    $exitCode = $LASTEXITCODE
    foreach ($line in $result) {
        Write-Host "  $line" -ForegroundColor Gray
    }
    if ($exitCode -eq 0) {
        Write-Host ""
        Write-Host "Test signing enabled successfully." -ForegroundColor Green
        Write-Host "A REBOOT is required for the change to take effect." -ForegroundColor Yellow
        Write-Host ""
        return $true
    } else {
        Write-Host "Failed to enable test signing (exit code $exitCode)." -ForegroundColor Red
        return $false
    }
}

function Find-InfFile {
    param([string]$BasePath)
    
    # Search order
    $candidates = @(
        (Join-Path $BasePath "mtk_usb2ser.inf"),
        (Join-Path $BasePath "driver\opensource\mtk_usb2ser.inf"),
        (Join-Path $BasePath "driver\CDC\mtk_preloader_opensource.inf")
    )
    
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Find-CdcInfFile {
    param([string]$BasePath)
    
    $candidates = @(
        (Join-Path $BasePath "mtk_preloader_opensource.inf"),
        (Join-Path $BasePath "driver\CDC\mtk_preloader_opensource.inf"),
        (Join-Path $BasePath "CDC\mtk_preloader_opensource.inf")
    )
    
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Install-DriverCertificate {
    param([string]$InfDir)

    # Look for the signing certificate (.cer) alongside the INF
    $cerCandidates = @(
        (Join-Path $InfDir "mtk_usb2ser.cer"),
        (Join-Path $InfDir "x64\mtk_usb2ser.cer")
    )

    $cerFile = $null
    foreach ($candidate in $cerCandidates) {
        if (Test-Path $candidate) {
            $cerFile = $candidate
            break
        }
    }

    if (-not $cerFile) {
        Write-Host "  No signing certificate (.cer) found — skipping certificate install" -ForegroundColor Gray
        return $false
    }

    Write-Host "  Found signing certificate: $cerFile" -ForegroundColor Gray

    $installed = $false
    try {
        # Install into Trusted Root Certification Authorities (required for self-signed root)
        $rootResult = & certutil.exe -addstore Root $cerFile 2>&1
        $rootExit = $LASTEXITCODE
        if ($rootExit -eq 0) {
            Write-Host "  Certificate added to Trusted Root store" -ForegroundColor Green
            $installed = $true
        } else {
            Write-Host "  certutil Root store returned code $rootExit" -ForegroundColor Yellow
            foreach ($line in $rootResult) {
                Write-Host "    $line" -ForegroundColor Gray
            }
        }
    } catch {
        Write-Host "  Failed to add to Root store: $($_.Exception.Message)" -ForegroundColor Yellow
    }

    try {
        # Install into Trusted Publisher (required for driver signing trust)
        $pubResult = & certutil.exe -addstore TrustedPublisher $cerFile 2>&1
        $pubExit = $LASTEXITCODE
        if ($pubExit -eq 0) {
            Write-Host "  Certificate added to Trusted Publisher store" -ForegroundColor Green
            $installed = $true
        } else {
            Write-Host "  certutil TrustedPublisher store returned code $pubExit" -ForegroundColor Yellow
            foreach ($line in $pubResult) {
                Write-Host "    $line" -ForegroundColor Gray
            }
        }
    } catch {
        Write-Host "  Failed to add to TrustedPublisher store: $($_.Exception.Message)" -ForegroundColor Yellow
    }

    return $installed
}

function Install-MtkDriver {
    param([string]$InfFile)
    
    Write-Host "[1/5] Validating driver package..." -ForegroundColor Yellow
    if (-not (Test-Path $InfFile)) {
        throw "INF file not found: $InfFile"
    }
    Write-Host "  INF: $InfFile" -ForegroundColor Gray
    
    # Check architecture
    $arch = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
    Write-Host "  Architecture: $arch" -ForegroundColor Gray
    Write-Host "  OS: Windows $([Environment]::OSVersion.Version)" -ForegroundColor Gray
    
    # Check for .sys file
    $infDir = Split-Path $InfFile -Parent
    $sysFile = Join-Path $infDir "$arch\mtk_usb2ser.sys"
    if (-not (Test-Path $sysFile)) {
        # Also check same directory
        $sysFile = Join-Path $infDir "mtk_usb2ser.sys"
    }
    if (Test-Path $sysFile) {
        $sysSize = (Get-Item $sysFile).Length
        Write-Host "  SYS: $sysFile ($sysSize bytes)" -ForegroundColor Gray
    }
    
    Write-Host ""
    Write-Host "[2/5] Installing signing certificate..." -ForegroundColor Yellow
    $certInstalled = Install-DriverCertificate -InfDir $infDir

    Write-Host ""
    Write-Host "[3/5] Adding driver to Windows Driver Store..." -ForegroundColor Yellow
    
    # Use pnputil to add the driver to the store
    $result = & pnputil.exe /add-driver $InfFile /install 2>&1
    $exitCode = $LASTEXITCODE
    
    foreach ($line in $result) {
        Write-Host "  $line" -ForegroundColor Gray
    }
    
    # Check for certificate trust error (0x800B0109)
    $trustError = $false
    foreach ($line in $result) {
        if ($line -match '0x800[Bb]0109|trust provider|not trusted') {
            $trustError = $true
            break
        }
    }

    if ($trustError -or $exitCode -ne 0) {
        if ($trustError) {
            Write-Host ""
            Write-Host "ERROR: The driver is signed with a test certificate that Windows does not trust." -ForegroundColor Red
            Write-Host "       (Error 0x800B0109: untrusted root certificate)" -ForegroundColor Red
            Write-Host ""
            Write-Host "This is expected for community/CI builds. You have three options:" -ForegroundColor Yellow
            Write-Host ""
            Write-Host "  Option 1 - Enable test signing (persistent, requires reboot):" -ForegroundColor White
            Write-Host "    bcdedit /set testsigning on" -ForegroundColor Cyan
            Write-Host "    Then reboot and run this installer again." -ForegroundColor White
            Write-Host ""
            Write-Host "  Option 2 - Disable signature enforcement (temporary, until next reboot):" -ForegroundColor White
            Write-Host "    Hold Shift + click Restart -> Troubleshoot -> Advanced Options" -ForegroundColor Cyan
            Write-Host "    -> Startup Settings -> Restart -> Press 7" -ForegroundColor Cyan
            Write-Host "    Then run this installer again." -ForegroundColor White
            Write-Host ""
            Write-Host "  Option 3 - Use the CDC-only driver (no signing needed):" -ForegroundColor White
            Write-Host "    Uses Windows' built-in usbser.sys — no custom binary, no certificate required." -ForegroundColor Cyan
            Write-Host "    Works immediately without test signing or rebooting." -ForegroundColor Cyan
            Write-Host ""

            # Try to find CDC INF for fallback
            $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
            $cdcInf = Find-CdcInfFile -BasePath $scriptDir
            if (-not $cdcInf) {
                $cdcInf = Find-CdcInfFile -BasePath (Get-Location).Path
            }

            Write-Host "What would you like to do?" -ForegroundColor Yellow
            if ($cdcInf) {
                Write-Host "  [1] Enable test signing now (reboot required)" -ForegroundColor White
                Write-Host "  [2] Install CDC-only driver instead (works now, no reboot)" -ForegroundColor White
                Write-Host "  [3] Exit" -ForegroundColor White
                $response = Read-Host "Select option [1/2/3]"
            } else {
                Write-Host "  [1] Enable test signing now (reboot required)" -ForegroundColor White
                Write-Host "  [2] Exit" -ForegroundColor White
                $response = Read-Host "Select option [1/2]"
            }

            if ($response -eq "1") {
                if (-not (Test-TestSigningEnabled)) {
                    $enabled = Enable-TestSigning
                    if ($enabled) {
                        Write-Host "Please REBOOT your PC and run this installer again." -ForegroundColor Yellow
                        Read-Host "Press Enter to exit"
                        exit 0
                    }
                } else {
                    Write-Host "Test signing is already enabled. Please REBOOT if you haven't yet." -ForegroundColor Yellow
                    Read-Host "Press Enter to exit"
                    exit 0
                }
            } elseif ($response -eq "2" -and $cdcInf) {
                Write-Host ""
                Write-Host "Installing CDC-only driver (inbox usbser.sys)..." -ForegroundColor Yellow
                Write-Host "  INF: $cdcInf" -ForegroundColor Gray
                $cdcResult = & pnputil.exe /add-driver $cdcInf /install 2>&1
                $cdcExitCode = $LASTEXITCODE
                foreach ($line in $cdcResult) {
                    Write-Host "  $line" -ForegroundColor Gray
                }
                if ($cdcExitCode -eq 0) {
                    Write-Host ""
                    Write-Host "CDC-only driver installed successfully!" -ForegroundColor Green
                    Write-Host "This uses Windows' built-in usbser.sys — fully functional for SP Flash Tool and mtkclient." -ForegroundColor White
                } else {
                    Write-Host ""
                    Write-Host "CDC driver installation returned code $cdcExitCode." -ForegroundColor Yellow
                    Write-Host "Try installing via Device Manager: right-click the device -> Update driver -> Browse -> select the CDC folder." -ForegroundColor Yellow
                }
            } else {
                Write-Host ""
                Write-Host "To install manually, enable test signing and reboot:" -ForegroundColor Yellow
                Write-Host "  bcdedit /set testsigning on" -ForegroundColor Cyan
                Read-Host "Press Enter to exit"
                exit 0
            }
        } else {
            Write-Host ""
            Write-Host "[4/5] pnputil returned code $exitCode" -ForegroundColor Yellow
            Write-Host "  Attempting alternative installation..." -ForegroundColor Yellow
            
            # Try with /subdirs flag
            $result2 = & pnputil.exe /add-driver $InfFile /install /subdirs 2>&1
            foreach ($line in $result2) {
                Write-Host "  $line" -ForegroundColor Gray
            }
        }
    } else {
        Write-Host ""
        Write-Host "[4/5] Driver installed successfully!" -ForegroundColor Green
    }
    
    Write-Host ""
    Write-Host "[5/5] Verifying installation..." -ForegroundColor Yellow
    
    # Check if driver is in the store
    $drivers = & pnputil.exe /enum-drivers 2>&1
    $found = $false
    foreach ($line in $drivers) {
        if ($line -match "mtk_usb2ser" -or $line -match "MediaTek") {
            Write-Host "  $line" -ForegroundColor Green
            $found = $true
        }
    }
    
    if ($found) {
        Write-Host ""
        Write-Host "Driver is ready!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Connect your MTK device in Preloader/BROM mode." -ForegroundColor White
        Write-Host "A new COM port will appear in Device Manager." -ForegroundColor White
        Write-Host ""
        Write-Host "Supported modes:" -ForegroundColor White
        Write-Host "  - BROM (Boot ROM)     -> PID 0003" -ForegroundColor Gray
        Write-Host "  - Preloader           -> PID 2000" -ForegroundColor Gray
        Write-Host "  - Download Agent (DA) -> PID 2001" -ForegroundColor Gray
        Write-Host "  - Meta Mode           -> PID 2007+" -ForegroundColor Gray
    } else {
        Write-Host "  Driver entry not found in store (may need reboot)" -ForegroundColor Yellow
    }
}

# Main
Write-Header

if (-not (Test-Admin)) {
    Write-Host "ERROR: Administrator privileges required." -ForegroundColor Red
    Write-Host "Please right-click and 'Run as Administrator'." -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Find INF file
if ([string]::IsNullOrEmpty($InfPath)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $InfPath = Find-InfFile -BasePath $scriptDir
    if (-not $InfPath) {
        $InfPath = Find-InfFile -BasePath (Get-Location).Path
    }
}

if ([string]::IsNullOrEmpty($InfPath) -or -not (Test-Path $InfPath)) {
    Write-Host "ERROR: Could not find driver INF file." -ForegroundColor Red
    Write-Host "Usage: .\install_driver.ps1 -InfPath <path\to\mtk_usb2ser.inf>" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

try {
    Install-MtkDriver -InfFile $InfPath
    Write-Host ""
    Write-Host "Installation complete." -ForegroundColor Green
} catch {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Read-Host "Press Enter to exit"
