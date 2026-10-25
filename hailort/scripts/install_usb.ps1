#!/c/Windows/System32/WindowsPowerShell/v1.0/powershell
param (
    [switch] $h,
    [ValidateSet("Release", "Debug")] [string] $b = "Release",
    [switch] $c,
    [string] $ImageSource = $null
)

if ($h) {
    $fileName = [System.IO.Path]::GetFileName($PSCommandPath)
    Write-Output "Usage: $fileName [-h help] [-b <build type>] [-c] [-ImageSource <path>]"
    Write-Output "  -h              Print usage and exit"
    Write-Output "  -b              Set build type (Release or Debug). Default: Release"
    Write-Output "  -c              Force CMake reconfiguration"
    Write-Output "  -ImageSource    Source directory containing image files. Default: current dir"
    Write-Output ""
    Write-Output "This script builds USB components for Hailo-10H and installs them to"
    Write-Output "C:\Program Files\HailoRT\bin\ (consistent with the PCIe MSI install)."
    Write-Output ""
    Write-Output "Image files will be copied from the current directory (or -ImageSource path)"
    Write-Output "to C:\Windows\System32\drivers\hailo\hailo10h_usb\ if not already there."
    Write-Output "(hailo_usb_loader.exe expects them at that location at runtime.)"
    Write-Output ""
    Write-Output "Prerequisites:"
    Write-Output "  - Run as Administrator"
    Write-Output ""
    Write-Output "Examples:"
    Write-Output "  $fileName                              # Build Release, copy images from cwd"
    Write-Output "  $fileName -b Debug                     # Build Debug, copy images from cwd"
    Write-Output "  $fileName -c                           # Force reconfigure + build"
    Write-Output "  $fileName -ImageSource C:\my\images    # Copy images from specified path"
    exit 0
}

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------

$ScriptDir = Split-Path -Parent $PSCommandPath
$RepoRoot = (Resolve-Path "$ScriptDir\..\..").Path
$BinDir = "$RepoRoot\bin\$b"
$BuildDir = "$RepoRoot\build"
$LibPath = "$RepoRoot\lib"
$BinPath = "$RepoRoot\bin"
$InstallDir = "C:\Program Files\HailoRT\bin"

# ---------------------------------------------------------------------------
# VS dev shell setup
# ---------------------------------------------------------------------------

$BUILD_TOOLS = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"

function source-vcvars {
    Import-Module "$BUILD_TOOLS\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $BUILD_TOOLS -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
}

function Remove-HailoUsbService {
    param([string]$ServiceName, [string]$ExePath)

    $existingSvc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($existingSvc) {
        Write-Host "Stopping existing $ServiceName service..." -ForegroundColor Cyan
        if (Test-Path $ExePath) {
            & $ExePath uninstall | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
                & sc.exe delete $ServiceName | Out-Null
            }
        } else {
            Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
            & sc.exe delete $ServiceName | Out-Null
        }
        Start-Sleep -Seconds 2
        Write-Host "  $ServiceName stopped and removed." -ForegroundColor Gray
    }

    # Kill any lingering processes
    Get-Process -Name $ServiceName -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "Killing lingering process: $($_.Name) (PID $($_.Id))" -ForegroundColor Yellow
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
}

# ---------------------------------------------------------------------------
# Check Administrator privileges
# ---------------------------------------------------------------------------

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: This script must be run as Administrator." -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Stop existing hailo_usb_service (if running/installed)
# ---------------------------------------------------------------------------
# If a previous install registered the service, the exe is locked and the
# linker cannot overwrite it. Stop + remove the service before the build.

$svcName = "hailo_usb_service"
Remove-HailoUsbService -ServiceName $svcName -ExePath "C:\Program Files\HailoRT\bin\$svcName.exe"

# ---------------------------------------------------------------------------
# Prepare image files
# ---------------------------------------------------------------------------
# The hailo_usb_loader hardcodes RFS_SWU_DIR to C:\Windows\System32\drivers\hailo\hailo10h_usb\
# so the image files must end up there. The script will look for them in the
# user-provided source dir (or current working dir by default) and copy them.

$ImagePath = "C:\Windows\System32\drivers\hailo\hailo10h_usb"

$ImageFiles = @(
    "core-image-hailo-hailo10-usb-dongle.ext4",
    "core-image-hailo-hailo10-usb-dongle-swu.ext4",
    "hailo-update-image-hailo10-usb-dongle.swu"
)

# Default image source to current working directory if not provided
if (-not $ImageSource) {
    $ImageSource = (Get-Location).Path
}

Write-Host "`nPreparing image files..." -ForegroundColor Cyan
Write-Host "  Source:      $ImageSource" -ForegroundColor Gray
Write-Host "  Destination: $ImagePath" -ForegroundColor Gray

# Ensure target directory exists
if (-not (Test-Path $ImagePath -PathType Container)) {
    New-Item -ItemType Directory -Path $ImagePath -Force | Out-Null
    Write-Host "  Created directory: $ImagePath" -ForegroundColor Gray
}

# Check where each image file lives
$missing = @()
foreach ($imageFile in $ImageFiles) {
    $srcPath = Join-Path $ImageSource $imageFile
    $dstPath = Join-Path $ImagePath $imageFile

    if (Test-Path $srcPath) {
        # Copy from source dir to destination
        Copy-Item -Path $srcPath -Destination $dstPath -Force
        Write-Host "  Copied: $imageFile" -ForegroundColor Gray
    } elseif (Test-Path $dstPath) {
        # Already in place from a previous run
        Write-Host "  Already in place: $imageFile" -ForegroundColor Gray
    } else {
        $missing += $imageFile
    }
}

if ($missing.Count -gt 0) {
    Write-Host "`nERROR: The following image files were not found in either location:" -ForegroundColor Red
    foreach ($f in $missing) {
        Write-Host "  - $f" -ForegroundColor Red
    }
    Write-Host "`nPlace them in one of these locations:" -ForegroundColor Gray
    Write-Host "  - $ImageSource (current dir or -ImageSource path)" -ForegroundColor Gray
    Write-Host "  - $ImagePath (the final destination)" -ForegroundColor Gray
    exit 1
}

Write-Host "All image files are ready in: $ImagePath" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Set up Visual Studio environment
# ---------------------------------------------------------------------------

Write-Host "`nInitializing Visual Studio dev shell..." -ForegroundColor Cyan
try {
    source-vcvars
} catch {
    Write-Host "ERROR: Failed to initialize Visual Studio dev shell: $_" -ForegroundColor Red
    Write-Host "  Ensure VS2019 BuildTools are installed at: $BUILD_TOOLS" -ForegroundColor Gray
    exit 1
}

# ---------------------------------------------------------------------------
# CMake configure
# ---------------------------------------------------------------------------

$CMAKE_GENERATOR = "Ninja Multi-Config"

$cmakeArgs = @(
    "-S$RepoRoot",
    "-B$BuildDir",
    "-G", $CMAKE_GENERATOR,
    "-DHAILO_BUILD_USB=ON",
    "-DHAILO_BUILD_TOOLS=ON",
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$LibPath",
    "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$LibPath",
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$BinPath"
)

if ($c -or -not (Test-Path -Path "$BuildDir")) {
    Write-Host "Configuring CMake..." -ForegroundColor Cyan
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: CMake configuration failed." -ForegroundColor Red
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Build USB targets
# ---------------------------------------------------------------------------

Write-Host "Building USB targets ($b)..." -ForegroundColor Cyan
cmake --build "$BuildDir" --config "$b" --target hailortcli libhailort hailo_usb_loader hailo_usb_service --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed with exit code $LASTEXITCODE." -ForegroundColor Red
    exit 1
}

Write-Host "Build completed successfully." -ForegroundColor Green

# ---------------------------------------------------------------------------
# Create install directory
# ---------------------------------------------------------------------------

if (-not (Test-Path $InstallDir)) {
    Write-Host "`nCreating install directory: $InstallDir" -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# ---------------------------------------------------------------------------
# Copy binaries to install directory
# ---------------------------------------------------------------------------

Write-Host "`nCopying binaries to $InstallDir..." -ForegroundColor Cyan

$BuiltBinaries = @(
    "hailortcli.exe",
    "libhailort.dll",
    "hailo_usb_loader.exe",
    "hailo_usb_service.exe"
)

foreach ($binary in $BuiltBinaries) {
    $srcPath = Join-Path $BinDir $binary
    if (-not (Test-Path $srcPath)) {
        Write-Host "ERROR: Built binary not found: $srcPath" -ForegroundColor Red
        exit 1
    }
    Copy-Item -Path $srcPath -Destination $InstallDir -Force
    Write-Host "  Copied: $binary" -ForegroundColor Gray
}

# libusb is built from source via FetchContent as a SHARED library. A POST_BUILD
# step in hailort/tools/usb/usb_image_scripts/CMakeLists.txt places libusb-1.0.dll
# next to hailo_usb_loader.exe in $BinDir.
$LibusbDll = Join-Path $BinDir "libusb-1.0.dll"
Copy-Item -Path $LibusbDll -Destination $InstallDir -Force
Write-Host "  Copied: libusb-1.0.dll" -ForegroundColor Gray

# ---------------------------------------------------------------------------
# Verify all expected files
# ---------------------------------------------------------------------------

Write-Host "`nVerifying output files..." -ForegroundColor Cyan

$ExpectedBinaries = @(
    "hailortcli.exe",
    "libhailort.dll",
    "hailo_usb_loader.exe",
    "hailo_usb_service.exe",
    "libusb-1.0.dll"
)

$allPresent = $true
foreach ($binary in $ExpectedBinaries) {
    $filePath = Join-Path $InstallDir $binary
    if (-not (Test-Path $filePath)) {
        Write-Host "  [MISSING] $InstallDir\$binary" -ForegroundColor Red
        $allPresent = $false
    }
}

foreach ($imageFile in $ImageFiles) {
    $filePath = Join-Path $ImagePath $imageFile
    if (-not (Test-Path $filePath)) {
        Write-Host "  [MISSING] $ImagePath\$imageFile" -ForegroundColor Red
        $allPresent = $false
    }
}

if (-not $allPresent) {
    Write-Host "`nERROR: Some expected files are missing" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Register hailo_usb_service
# ---------------------------------------------------------------------------
# If a previous registration exists (shouldn't after pre-build cleanup, but
# be defensive), uninstall it first before running the fresh install.

$svcName = "hailo_usb_service"
$installedSvcExe = Join-Path $InstallDir "$svcName.exe"

Write-Host "`nRegistering $svcName service..." -ForegroundColor Cyan

# Defensive: remove any stale registration before installing
Remove-HailoUsbService -ServiceName $svcName -ExePath $installedSvcExe

# Install + start the service
& $installedSvcExe install
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: $svcName install failed with exit code $LASTEXITCODE." -ForegroundColor Red
    exit 1
}

# Verify service is running
Start-Sleep -Seconds 2
$svc = Get-Service -Name $svcName -ErrorAction SilentlyContinue
if ($svc -and $svc.Status -eq 'Running') {
    Write-Host "  $svcName is running." -ForegroundColor Green
} elseif ($svc) {
    Write-Host "  WARNING: $svcName registered but status is $($svc.Status)" -ForegroundColor Yellow
} else {
    Write-Host "ERROR: $svcName was not registered." -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Print summary
# ---------------------------------------------------------------------------

Write-Host "`n========== USB INSTALL SUMMARY ==========" -ForegroundColor Yellow
Write-Host "Build type:  $b" -ForegroundColor Gray
Write-Host "Binaries:    $InstallDir" -ForegroundColor Gray
Write-Host "Image files: $ImagePath" -ForegroundColor Gray

Write-Host "`nInstalled binaries:" -ForegroundColor Yellow
foreach ($binary in $ExpectedBinaries) {
    $filePath = Join-Path $InstallDir $binary
    $fileInfo = Get-Item $filePath
    $sizeMB = [math]::Round($fileInfo.Length / 1MB, 2)
    Write-Host "  [OK] $binary ($sizeMB MB)" -ForegroundColor Green
}

Write-Host "`nImage files (in place):" -ForegroundColor Yellow
foreach ($imageFile in $ImageFiles) {
    $filePath = Join-Path $ImagePath $imageFile
    $fileInfo = Get-Item $filePath
    $sizeMB = [math]::Round($fileInfo.Length / 1MB, 2)
    Write-Host "  [OK] $imageFile ($sizeMB MB)" -ForegroundColor Green
}

Write-Host "`n=========================================" -ForegroundColor Yellow
Write-Host "USB install complete." -ForegroundColor Green
