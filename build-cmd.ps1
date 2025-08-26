# PowerShell build script for Windows
# This is the Windows-native equivalent of build-cmd.sh

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

# Enable strict error handling
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptDir

try {
    # Set up paths
    $top = Get-Location
    $stage = Join-Path $top "stage"
    
    # Extract version from vcpkg.json
    $vcpkgJson = Get-Content "vcpkg.json" | ConvertFrom-Json
    $ENTROPYCORE_VERSION = $vcpkgJson.version
    
    # Get build ID from environment or default to 0
    $build = if ($env:AUTOBUILD_BUILD_ID) { $env:AUTOBUILD_BUILD_ID } else { "0" }
    
    Write-Host "Building EntropyCore version $ENTROPYCORE_VERSION.$build" -ForegroundColor Green
    
    # Prepare staging directories
    $directories = @(
        "$stage\LICENSES",
        "$stage\include\EntropyCore",
        "$stage\include\EntropyCore\Core",
        "$stage\include\EntropyCore\Concurrency", 
        "$stage\include\EntropyCore\Debug",
        "$stage\include\EntropyCore\Graph",
        "$stage\include\EntropyCore\Logging",
        "$stage\include\EntropyCore\TypeSystem",
        "$stage\lib\release"
    )
    
    foreach ($dir in $directories) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    
    # Bootstrap vcpkg if not present
    $VCPKG_ROOT = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $top "vcpkg" }
    
    if (-not (Test-Path $VCPKG_ROOT)) {
        Write-Host "Bootstrapping vcpkg..." -ForegroundColor Yellow
        git clone https://github.com/Microsoft/vcpkg.git $VCPKG_ROOT
        & "$VCPKG_ROOT\bootstrap-vcpkg.bat" -disableMetrics
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to bootstrap vcpkg"
        }
    }
    
    # Set up build directory
    $BUILD_DIR = Join-Path $top "build_autobuild"
    New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null
    
    # Find Visual Studio installation
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath
        $vcVarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        
        if (Test-Path $vcVarsPath) {
            Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green
        }
    }
    
    # Configure with CMake
    Write-Host "Configuring with CMake..." -ForegroundColor Yellow
    $cmakeArgs = @(
        "-S", $top,
        "-B", $BUILD_DIR,
        "-G", "Visual Studio 17 2022",
        "-A", $Platform,
        "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake",
        "-DVCPKG_TARGET_TRIPLET=$Platform-windows-static",
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DENTROPY_BUILD_TESTS=OFF",
        "-DCMAKE_INSTALL_PREFIX=$stage"
    )
    
    & cmake $cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }
    
    # Build the project
    Write-Host "Building EntropyCore..." -ForegroundColor Yellow
    & cmake --build $BUILD_DIR --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
    
    # Install to staging directory
    Write-Host "Installing to staging directory..." -ForegroundColor Yellow
    & cmake --install $BUILD_DIR --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Installation failed"
    }
    
    # Move installed files to expected locations for autobuild
    Write-Host "Reorganizing installed files for autobuild..." -ForegroundColor Yellow
    
    # Ensure lib/release directory exists
    New-Item -ItemType Directory -Force -Path "$stage\lib\release" | Out-Null
    
    # Move the static library to release folder
    $libSource = Join-Path $stage "lib\EntropyCore.lib"
    if (Test-Path $libSource) {
        Move-Item $libSource "$stage\lib\release\" -Force
        Write-Host "Moved EntropyCore.lib to lib/release" -ForegroundColor Green
    } else {
        throw "EntropyCore.lib not found at $libSource"
    }
    
    # Move headers to EntropyCore subdirectory
    if (Test-Path "$stage\include") {
        $entropyInclude = "$stage\include\EntropyCore"
        
        # If EntropyCore directory already exists (from previous runs), clear it
        if (Test-Path $entropyInclude) {
            Remove-Item $entropyInclude -Recurse -Force
        }
        New-Item -ItemType Directory -Force -Path $entropyInclude | Out-Null
        
        # Move subdirectory headers
        $headerDirs = @("Core", "Concurrency", "Debug", "Graph", "Logging", "TypeSystem")
        foreach ($dir in $headerDirs) {
            $sourcePath = Join-Path $stage "include\$dir"
            $destPath = Join-Path $entropyInclude $dir
            if (Test-Path $sourcePath) {
                Move-Item $sourcePath $destPath -Force
            }
        }
        
        # Move root headers
        $mainHeaders = @("EntropyCore.h", "CoreCommon.h", "ServiceLocator.h")
        foreach ($header in $mainHeaders) {
            $headerPath = Join-Path $stage "include\$header"
            $destPath = Join-Path $entropyInclude $header
            if (Test-Path $headerPath) {
                Move-Item $headerPath $destPath -Force
            }
        }
    }
    
    # Clean up cmake files that aren't needed for autobuild
    $cmakeDir = Join-Path $stage "lib\cmake"
    if (Test-Path $cmakeDir) {
        Remove-Item $cmakeDir -Recurse -Force
        Write-Host "Cleaned up cmake config files" -ForegroundColor Gray
    }
    
    # Create VERSION.txt
    "$ENTROPYCORE_VERSION.$build" | Out-File -FilePath "$stage\VERSION.txt" -Encoding ASCII -NoNewline
    
    # Copy license files - both to LICENSES directory and root for autobuild package
    Copy-Item "$top\LICENSE.md" "$stage\LICENSES\EntropyCore.txt" -Force
    Copy-Item "$top\LICENSE.md" "$stage\LICENSE.md" -Force
    
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host "Artifacts staged in: $stage" -ForegroundColor Cyan
    
    # List what was staged
    Write-Host "`nStaged files:" -ForegroundColor Yellow
    Get-ChildItem -Path $stage -Recurse -File | ForEach-Object {
        $relativePath = $_.FullName.Substring($stage.Length + 1)
        Write-Host "  $relativePath" -ForegroundColor Gray
    }
}
catch {
    Write-Error "Build failed: $_"
    exit 1
}
finally {
    Pop-Location
}