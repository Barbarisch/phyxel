# PowerShell Fast Test Script for Phyxel
# Builds ONLY the unit tests and runs them with an optional filter
# Usage: .\test_fast.ps1 [Filter]
# Example: .\test_fast.ps1 VoxelRaycaster*

param(
    [string]$Filter = "*"
)

# Store original location
$OriginalLocation = Get-Location

# Get the script's directory (project root)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

# Check if build directory exists
if (-not (Test-Path "build")) {
    Write-Host "Error: Build directory not found. Please run build_and_test.ps1 first to configure CMake." -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

# Navigate to build directory
Set-Location (Join-Path $ScriptDir "build")

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Fast Test: Building Unit Tests..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Build only the unit tests target
cmake --build . --config Debug --target phyxel_tests

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build failed!" -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Running Tests (Filter: $Filter)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Run tests from project root
Set-Location $ScriptDir

$UnitTestExe = Join-Path "build" (Join-Path "tests" (Join-Path "Debug" "phyxel_tests.exe"))

if (Test-Path $UnitTestExe) {
    & $UnitTestExe --gtest_filter=$Filter
} else {
    Write-Host "Error: Unit test executable not found at $UnitTestExe" -ForegroundColor Red
}

Set-Location $OriginalLocation
