#!/c/Windows/System32/WindowsPowerShell/v1.0/powershell
param (
    [switch] $h,
    [switch] $KeepImages
)

if ($h) {
    $fileName = [System.IO.Path]::GetFileName($PSCommandPath)
    Write-Output "Usage: $fileName [-h help] [-KeepImages]"
    Write-Output "  -h            Print usage and exit"
    Write-Output "  -KeepImages   Don't remove image files from System32"
    Write-Output ""
    Write-Output "This script removes USB components for Hailo-10H that were installed by"
    Write-Output "install_usb.ps1 and the Windows USB CI stage."
    Write-Output ""
    Write-Output "What gets removed:"
    Write-Output "  - Binaries from C:\Program Files\HailoRT\bin\"
    Write-Output "    (hailortcli.exe, libhailort.dll, hailo_usb_loader.exe,"
    Write-Output "     hailo_usb_service.exe, libusb-1.0.dll)"
    Write-Output "  - Image files from C:\Windows\System32\drivers\hailo\hailo10h_usb\"
    Write-Output "    (unless -KeepImages is specified)"
    Write-Output "  - C:\Program Files\HailoRT\bin from the system PATH"
    Write-Output ""
    Write-Output "Prerequisites:"
    Write-Output "  - Run as Administrator"
    Write-Output ""
    Write-Output "Examples:"
    Write-Output "  $fileName                 # Remove binaries and image files"
    Write-Output "  $fileName -KeepImages     # Remove binaries only, keep image files"
    Write-Output "  $fileName -h              # Print help"
    exit 0
}

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Check Administrator privileges
# ---------------------------------------------------------------------------

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: This script must be run as Administrator." -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

$InstallDir = "C:\Program Files\HailoRT\bin"
$HailoRTDir = "C:\Program Files\HailoRT"
$ImagePath = "C:\Windows\System32\drivers\hailo\hailo10h_usb"

$Binaries = @(
    "hailortcli.exe",
    "libhailort.dll",
    "hailo_usb_loader.exe",
    "hailo_usb_service.exe",
    "libusb-1.0.dll"
)

$ImageFiles = @(
    "core-image-hailo-hailo10-usb-dongle.ext4",
    "core-image-hailo-hailo10-usb-dongle-swu.ext4",
    "hailo-update-image-hailo10-usb-dongle.swu"
)

# Track what was done for the summary
$removedBinaries = @()
$skippedBinaries = @()
$failedBinaries = @()
$removedImages = @()
$skippedImages = @()
$failedImages = @()
$removedDirs = @()
$pathRemoved = $false

Write-Host "Starting USB uninstallation..." -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# Stop and remove hailo_usb_service (must happen before deleting binaries)
# ---------------------------------------------------------------------------

$svcName = "hailo_usb_service"
$svcExe = Join-Path $InstallDir "$svcName.exe"

Write-Host "`nChecking for running $svcName service..." -ForegroundColor Cyan

$existingSvc = Get-Service -Name $svcName -ErrorAction SilentlyContinue
if ($existingSvc) {
    Write-Host "  Stopping and removing $svcName service..." -ForegroundColor Cyan
    if (Test-Path $svcExe) {
        & $svcExe uninstall | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Stop-Service -Name $svcName -Force -ErrorAction SilentlyContinue
            & sc.exe delete $svcName | Out-Null
        }
    } else {
        Stop-Service -Name $svcName -Force -ErrorAction SilentlyContinue
        & sc.exe delete $svcName | Out-Null
    }
    Start-Sleep -Seconds 2
    Write-Host "  $svcName stopped and removed." -ForegroundColor Gray
} else {
    Write-Host "  $svcName service not found, skipping." -ForegroundColor Gray
}

# Kill any lingering processes
Get-Process -Name $svcName -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "  Killing lingering process: $($_.Name) (PID $($_.Id))" -ForegroundColor Yellow
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 2

# ---------------------------------------------------------------------------
# Remove binaries from C:\Program Files\HailoRT\bin\
# ---------------------------------------------------------------------------

Write-Host "`nRemoving binaries from $InstallDir..." -ForegroundColor Cyan

foreach ($binary in $Binaries) {
    $filePath = Join-Path $InstallDir $binary
    if (Test-Path $filePath) {
        Remove-Item -Path $filePath -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path $filePath)) {
            Write-Host "  Removed: $binary" -ForegroundColor Gray
            $removedBinaries += $binary
        } else {
            Write-Host "  WARNING: Failed to remove $binary" -ForegroundColor Yellow
            $failedBinaries += $binary
        }
    } else {
        Write-Host "  Already removed: $binary" -ForegroundColor Gray
        $skippedBinaries += $binary
    }
}

# Remove bin directory if empty
if (Test-Path $InstallDir) {
    $remaining = Get-ChildItem -Path $InstallDir -Force
    if ($null -eq $remaining -or $remaining.Count -eq 0) {
        Remove-Item -Path $InstallDir -Force -ErrorAction SilentlyContinue
        Write-Host "  Removed empty directory: $InstallDir" -ForegroundColor Gray
        $removedDirs += $InstallDir
    } else {
        Write-Host "  Directory not empty, keeping: $InstallDir" -ForegroundColor Gray
    }
}

# Remove HailoRT directory if empty
if (Test-Path $HailoRTDir) {
    $remaining = Get-ChildItem -Path $HailoRTDir -Force
    if ($null -eq $remaining -or $remaining.Count -eq 0) {
        Remove-Item -Path $HailoRTDir -Force -ErrorAction SilentlyContinue
        Write-Host "  Removed empty directory: $HailoRTDir" -ForegroundColor Gray
        $removedDirs += $HailoRTDir
    } else {
        Write-Host "  Directory not empty, keeping: $HailoRTDir" -ForegroundColor Gray
    }
}

# ---------------------------------------------------------------------------
# Remove image files from C:\Windows\System32\drivers\hailo\hailo10h_usb\
# ---------------------------------------------------------------------------

if ($KeepImages) {
    Write-Host "`nSkipping image file removal (-KeepImages specified)." -ForegroundColor Cyan
} else {
    Write-Host "`nRemoving image files from $ImagePath..." -ForegroundColor Cyan

    foreach ($imageFile in $ImageFiles) {
        $filePath = Join-Path $ImagePath $imageFile
        if (Test-Path $filePath) {
            Remove-Item -Path $filePath -Force -ErrorAction SilentlyContinue
            if (-not (Test-Path $filePath)) {
                Write-Host "  Removed: $imageFile" -ForegroundColor Gray
                $removedImages += $imageFile
            } else {
                Write-Host "  WARNING: Failed to remove $imageFile" -ForegroundColor Yellow
                $failedImages += $imageFile
            }
        } else {
            Write-Host "  Already removed: $imageFile" -ForegroundColor Gray
            $skippedImages += $imageFile
        }
    }

    # Remove hailo10h_usb directory if empty
    if (Test-Path $ImagePath) {
        $remaining = Get-ChildItem -Path $ImagePath -Force
        if ($null -eq $remaining -or $remaining.Count -eq 0) {
            Remove-Item -Path $ImagePath -Force -ErrorAction SilentlyContinue
            Write-Host "  Removed empty directory: $ImagePath" -ForegroundColor Gray
            $removedDirs += $ImagePath
        } else {
            Write-Host "  Directory not empty, keeping: $ImagePath" -ForegroundColor Gray
        }
    }
}

# ---------------------------------------------------------------------------
# Remove from system PATH
# ---------------------------------------------------------------------------

Write-Host "`nUpdating system PATH..." -ForegroundColor Cyan

try {
    $machinePath = [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
    $pathEntries = $machinePath -split ";" | Where-Object { $_ -ne "" }
    $filteredEntries = $pathEntries | Where-Object { $_.TrimEnd('\') -ne $InstallDir.TrimEnd('\') }

    if ($filteredEntries.Count -lt $pathEntries.Count) {
        $newPath = ($filteredEntries -join ";")
        [System.Environment]::SetEnvironmentVariable("PATH", $newPath, "Machine")
        Write-Host "  Removed $InstallDir from system PATH." -ForegroundColor Gray
        Write-Host "  NOTE: Restart your terminal for the PATH change to take effect." -ForegroundColor Yellow
        $pathRemoved = $true
    } else {
        Write-Host "  $InstallDir was not in system PATH, skipping." -ForegroundColor Gray
    }
} catch {
    Write-Host "  WARNING: Failed to update system PATH: $_" -ForegroundColor Yellow
    Write-Host "  You may need to manually remove $InstallDir from the system PATH." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Print summary
# ---------------------------------------------------------------------------

Write-Host "`n========== USB UNINSTALL SUMMARY ==========" -ForegroundColor Yellow

if ($removedBinaries.Count -gt 0) {
    Write-Host "Removed binaries:" -ForegroundColor Yellow
    foreach ($binary in $removedBinaries) {
        Write-Host "  [REMOVED] $binary" -ForegroundColor Green
    }
}

if ($skippedBinaries.Count -gt 0) {
    Write-Host "Already removed binaries:" -ForegroundColor Yellow
    foreach ($binary in $skippedBinaries) {
        Write-Host "  [SKIPPED] $binary" -ForegroundColor Gray
    }
}

if ($failedBinaries.Count -gt 0) {
    Write-Host "Failed to remove binaries:" -ForegroundColor Red
    foreach ($binary in $failedBinaries) {
        Write-Host "  [FAILED] $binary" -ForegroundColor Red
    }
}

if (-not $KeepImages) {
    if ($removedImages.Count -gt 0) {
        Write-Host "Removed image files:" -ForegroundColor Yellow
        foreach ($imageFile in $removedImages) {
            Write-Host "  [REMOVED] $imageFile" -ForegroundColor Green
        }
    }

    if ($skippedImages.Count -gt 0) {
        Write-Host "Already removed image files:" -ForegroundColor Yellow
        foreach ($imageFile in $skippedImages) {
            Write-Host "  [SKIPPED] $imageFile" -ForegroundColor Gray
        }
    }

    if ($failedImages.Count -gt 0) {
        Write-Host "Failed to remove image files:" -ForegroundColor Red
        foreach ($imageFile in $failedImages) {
            Write-Host "  [FAILED] $imageFile" -ForegroundColor Red
        }
    }
} else {
    Write-Host "Image files: kept (-KeepImages)" -ForegroundColor Yellow
}

if ($removedDirs.Count -gt 0) {
    Write-Host "Removed directories:" -ForegroundColor Yellow
    foreach ($dir in $removedDirs) {
        Write-Host "  [REMOVED] $dir" -ForegroundColor Green
    }
}

if ($pathRemoved) {
    Write-Host "System PATH: removed $InstallDir" -ForegroundColor Yellow
} else {
    Write-Host "System PATH: no change needed" -ForegroundColor Yellow
}

Write-Host "============================================" -ForegroundColor Yellow
Write-Host "USB uninstallation complete." -ForegroundColor Green

if ($failedBinaries.Count -gt 0 -or $failedImages.Count -gt 0) {
    exit 1
}
