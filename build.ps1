# FrameForge Build Script (PowerShell)
# Usage: .\build.ps1 [-BuildType Debug|Release]

param(
    [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"

# Locate Visual Studio Build Tools
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    Write-Host "ERROR: vcvars64.bat not found at:" -ForegroundColor Red
    Write-Host "  $vcvars"
    Write-Host 'Install "Desktop development with C++" workload in VS Build Tools.'
    exit 1
}

Write-Host ""
Write-Host "FrameForge Build" -ForegroundColor Cyan
Write-Host "  Type: $BuildType"
Write-Host ""

# Set up VS environment by capturing only the PATH and INCLUDE/LIB variables
$envScript = "`"$vcvars`" >nul 2>&1 && set PATH && set INCLUDE && set LIB && set LIBPATH"
$envOutput = cmd.exe /c $envScript
foreach ($line in $envOutput) {
    if ($line -match '^PATH=(.*)$') {
        $env:PATH = $matches[1]
    }
    elseif ($line -match '^INCLUDE=(.*)$') {
        $env:INCLUDE = $matches[1]
    }
    elseif ($line -match '^LIB=(.*)$') {
        $env:LIB = $matches[1]
    }
    elseif ($line -match '^LIBPATH=(.*)$') {
        $env:LIBPATH = $matches[1]
    }
}

# Verify compiler is available
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Host "ERROR: cl.exe not found after vcvars64.bat" -ForegroundColor Red
    exit 1
}
Write-Host "  Compiler: $($cl.Source)"
Write-Host "  Ninja:    $((Get-Command ninja.exe -ErrorAction SilentlyContinue).Source)"
Write-Host ""

# Configure
if (-not (Test-Path "build")) { New-Item -ItemType Directory -Path "build" | Out-Null }
& cmake -G Ninja -S . -B build "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    exit 1
}

# Build
& cmake --build build
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Build successful." -ForegroundColor Green
Write-Host "  Executable: build\frameforge.exe"
Write-Host ""

# Run tests
Write-Host "Running tests..."
Write-Host ""
Push-Location build
& ctest --output-on-failure
$testResult = $LASTEXITCODE
Pop-Location

exit $testResult
