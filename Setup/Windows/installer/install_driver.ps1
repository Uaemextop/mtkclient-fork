# install_driver.ps1
# MediaTek USB Serial Driver installer script
# Supports:
#   - Test-certificate builds  (mtk_test_cert.cer bundled alongside the INF)
#   - Production-certificate builds (no .cer file needed)
#
# Usage:
#   .\install_driver.ps1                     # auto-detect INF in script directory
#   .\install_driver.ps1 -InfPath <path>     # explicit INF path
#   .\install_driver.ps1 -SkipCert           # skip certificate install
#   .\install_driver.ps1 -SkipTestSigning    # skip bcdedit testsigning on

param(
    [string]$InfPath         = "",
    [switch]$SkipCert        = $false,
    [switch]$SkipTestSigning = $false
)

$ErrorActionPreference = "Stop"

# ─────────────────────────────────────────────────────────────────────────────
function Write-Header {
    Write-Host ""
    Write-Host "=============================================" -ForegroundColor Cyan
    Write-Host "  MTK USB Serial Driver Installer" -ForegroundColor Cyan
    Write-Host "  For Preloader / BROM / DA / Meta Mode" -ForegroundColor Cyan
    Write-Host "  Compatible: SP Flash Tool, mtkclient" -ForegroundColor Cyan
    Write-Host "=============================================" -ForegroundColor Cyan
    Write-Host ""
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    ([Security.Principal.WindowsPrincipal]$id).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-InfFile ([string]$BasePath) {
    foreach ($candidate in @(
        (Join-Path $BasePath "mtk_usb2ser.inf"),
        (Join-Path $BasePath "driver\opensource\mtk_usb2ser.inf"),
        (Join-Path $BasePath "driver\CDC\mtk_preloader_opensource.inf")
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 1 — Install code-signing certificate
# ─────────────────────────────────────────────────────────────────────────────
function Install-DriverCertificate ([string]$InfFile) {
    $infDir  = Split-Path $InfFile -Parent
    $cerFile = Join-Path $infDir "mtk_test_cert.cer"

    if (-not (Test-Path $cerFile)) {
        Write-Host "[CERT] No test certificate found alongside INF." -ForegroundColor Gray
        Write-Host "       (Production-signed driver — no certificate install needed)" -ForegroundColor Gray
        return $false
    }

    Write-Host "[1/4] Installing code-signing certificate..." -ForegroundColor Yellow
    Write-Host "  Certificate: $cerFile" -ForegroundColor Gray

    $stores = @("Root", "TrustedPublisher")
    foreach ($store in $stores) {
        Write-Host "  Adding to store: Cert:\LocalMachine\$store" -ForegroundColor Gray
        try {
            # Use certutil — available on all Windows versions
            $out = & certutil.exe -addstore $store $cerFile 2>&1
            foreach ($line in $out) { Write-Host "    $line" -ForegroundColor Gray }

            if ($LASTEXITCODE -ne 0) {
                # Fallback to PowerShell cmdlet
                Import-Certificate -FilePath $cerFile `
                    -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
            }
            Write-Host "  OK: $store" -ForegroundColor Green
        } catch {
            Write-Host "  WARNING: Failed to add cert to $store : $_" -ForegroundColor Yellow
        }
    }
    return $true
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 2 — Enable Windows Test Signing
# ─────────────────────────────────────────────────────────────────────────────
function Enable-TestSigning {
    Write-Host "[2/4] Enabling Windows Test Signing mode..." -ForegroundColor Yellow
    Write-Host "  (Required for drivers signed with a self-signed test certificate)" -ForegroundColor Gray

    # Check current state
    $bcdedit = & bcdedit.exe /enum 2>&1 | Out-String
    if ($bcdedit -match "testsigning\s+Yes") {
        Write-Host "  Test Signing already enabled." -ForegroundColor Green
        return $true
    }

    $out = & bcdedit.exe /set testsigning on 2>&1
    foreach ($line in $out) { Write-Host "  $line" -ForegroundColor Gray }

    if ($LASTEXITCODE -eq 0) {
        Write-Host "  Test Signing enabled. ** A REBOOT IS REQUIRED. **" -ForegroundColor Yellow
        return $true
    } else {
        Write-Host "  WARNING: Could not enable Test Signing (Secure Boot may be active)." -ForegroundColor Yellow
        Write-Host "  If the driver fails to load, disable Secure Boot in UEFI firmware settings" -ForegroundColor Yellow
        Write-Host "  and re-run this installer." -ForegroundColor Yellow
        return $false
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 3 — Install driver via pnputil
# ─────────────────────────────────────────────────────────────────────────────
function Install-MtkDriver ([string]$InfFile) {
    Write-Host "[3/4] Installing driver via pnputil..." -ForegroundColor Yellow
    Write-Host "  INF  : $InfFile" -ForegroundColor Gray

    $arch    = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
    $infDir  = Split-Path $InfFile -Parent
    $sysFile = Join-Path $infDir "$arch\mtk_usb2ser.sys"
    if (-not (Test-Path $sysFile)) {
        $sysFile = Join-Path $infDir "mtk_usb2ser.sys"
    }
    if (Test-Path $sysFile) {
        Write-Host "  SYS  : $sysFile ($([Math]::Round((Get-Item $sysFile).Length/1KB,1)) KB)" -ForegroundColor Gray
    }
    Write-Host "  Arch : $arch" -ForegroundColor Gray
    Write-Host "  OS   : Windows $([Environment]::OSVersion.Version)" -ForegroundColor Gray

    $out = & pnputil.exe /add-driver $InfFile /install 2>&1
    $ec  = $LASTEXITCODE
    foreach ($line in $out) { Write-Host "  $line" -ForegroundColor Gray }

    if ($ec -eq 0) {
        Write-Host "  pnputil succeeded." -ForegroundColor Green
    } else {
        Write-Host "  pnputil returned $ec — trying with /subdirs..." -ForegroundColor Yellow
        $out2 = & pnputil.exe /add-driver $InfFile /install /subdirs 2>&1
        foreach ($line in $out2) { Write-Host "  $line" -ForegroundColor Gray }
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 4 — Verify installation
# ─────────────────────────────────────────────────────────────────────────────
function Confirm-Installation {
    Write-Host "[4/4] Verifying installation..." -ForegroundColor Yellow
    $drivers = & pnputil.exe /enum-drivers 2>&1
    $found = $false
    foreach ($line in $drivers) {
        if ($line -match "mtk_usb2ser|MediaTek") {
            Write-Host "  $line" -ForegroundColor Green
            $found = $true
        }
    }
    if ($found) {
        Write-Host ""
        Write-Host "Driver is ready!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Connect your MTK device in Preloader/BROM mode." -ForegroundColor White
        Write-Host "A new COM port will appear in Device Manager."   -ForegroundColor White
        Write-Host ""
        Write-Host "Supported modes:" -ForegroundColor White
        Write-Host "  BROM (Boot ROM)        PID 0003" -ForegroundColor Gray
        Write-Host "  Preloader              PID 2000, 2001, 20FF, 3000, 6000" -ForegroundColor Gray
        Write-Host "  Download Agent (DA)    PID 2001" -ForegroundColor Gray
        Write-Host "  Meta Mode              PID 2007+" -ForegroundColor Gray
    } else {
        Write-Host "  Driver entry not found in pnputil store." -ForegroundColor Yellow
        Write-Host "  This may require a reboot to complete." -ForegroundColor Yellow
    }
    return $found
}

# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────
Write-Header

if (-not (Test-Admin)) {
    Write-Host "ERROR: Administrator privileges required." -ForegroundColor Red
    Write-Host "Please right-click and 'Run as Administrator'." -ForegroundColor Red
    if ($Host.Name -eq "ConsoleHost") { Read-Host "Press Enter to exit" }
    exit 1
}

# Resolve INF path
if ([string]::IsNullOrEmpty($InfPath)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $InfPath   = Find-InfFile -BasePath $scriptDir
    if (-not $InfPath) { $InfPath = Find-InfFile -BasePath (Get-Location).Path }
}

if ([string]::IsNullOrEmpty($InfPath) -or -not (Test-Path $InfPath)) {
    Write-Host "ERROR: Could not find driver INF file." -ForegroundColor Red
    Write-Host "Usage: .\install_driver.ps1 -InfPath <path\to\mtk_usb2ser.inf>" -ForegroundColor Yellow
    if ($Host.Name -eq "ConsoleHost") { Read-Host "Press Enter to exit" }
    exit 1
}

try {
    $needsReboot = $false

    # Install certificate (test builds only)
    if (-not $SkipCert) {
        $certInstalled = Install-DriverCertificate -InfFile $InfPath
        Write-Host ""
    }

    # Enable Test Signing (test builds only)
    if (-not $SkipCert -and -not $SkipTestSigning) {
        $infDir  = Split-Path $InfPath -Parent
        $cerFile = Join-Path $infDir "mtk_test_cert.cer"
        if (Test-Path $cerFile) {
            $tsEnabled   = Enable-TestSigning
            $needsReboot = $tsEnabled
            Write-Host ""
        }
    }

    # Install driver
    Install-MtkDriver -InfFile $InfPath
    Write-Host ""

    # Verify
    Confirm-Installation
    Write-Host ""

    if ($needsReboot) {
        Write-Host "================================================================" -ForegroundColor Yellow
        Write-Host "  REBOOT REQUIRED" -ForegroundColor Yellow
        Write-Host "  Windows Test Signing mode has been enabled." -ForegroundColor Yellow
        Write-Host "  Please reboot and reconnect your MTK device." -ForegroundColor Yellow
        Write-Host "================================================================" -ForegroundColor Yellow
    }

    Write-Host "Installation complete." -ForegroundColor Green
} catch {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    if ($Host.Name -eq "ConsoleHost") { Read-Host "Press Enter to exit" }
    exit 1
}

if ($Host.Name -eq "ConsoleHost") { Read-Host "Press Enter to exit" }
