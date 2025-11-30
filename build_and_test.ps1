# PowerShell Build and Test Script for Phyxel
# Sets up VS environment and builds/tests the project

param(
    [string]$Config = "Debug",
    [string]$Target = "phyxel_tests"
)

# Store original location
$OriginalLocation = Get-Location

# Ensure we're in the project root
Set-Location "G:\Github\phyxel"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Setting up Visual Studio environment..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Import VS Developer environment
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Configuring CMake..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Reconfigure CMake (important when new source files are added/removed)
cmake -B build -G "Visual Studio 17 2022" -A x64

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

# Navigate to build directory
Set-Location "G:\Github\phyxel\build"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building $Target ($Config)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Build the target
cmake --build . --config $Config --target $Target

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    
    # Run tests if we built the test target
    if ($Target -eq "phyxel_tests") {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Unit Tests..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $TestExe = ".\tests\$Config\phyxel_tests.exe"
        if (Test-Path $TestExe) {
            & $TestExe
            
            if ($LASTEXITCODE -eq 0) {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Green
                Write-Host "All tests passed! Build complete." -ForegroundColor Green
                Write-Host "========================================" -ForegroundColor Green
            } else {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "UNIT TESTS FAILED!" -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                Set-Location $OriginalLocation
                exit 1
            }
        } else {
            Write-Host "Test executable not found: $TestExe" -ForegroundColor Yellow
        }
    }
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Build failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

# Return to original location
Set-Location $OriginalLocation

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
