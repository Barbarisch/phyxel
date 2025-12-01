# PowerShell Build and Test Script for Phyxel
# Sets up VS environment and builds/tests the project

param(
    [string]$Config = "Debug",
    [switch]$UnitOnly,
    [switch]$IntegrationOnly
)

# Store original location
$OriginalLocation = Get-Location

# Get the script's directory (project root)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Phyxel Build and Test System" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if ($UnitOnly) {
    Write-Host "Mode: Unit Tests Only" -ForegroundColor Yellow
} elseif ($IntegrationOnly) {
    Write-Host "Mode: Integration Tests Only" -ForegroundColor Yellow
} else {
    Write-Host "Mode: All Tests (Unit + Integration)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Setting up Visual Studio environment..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Try to find and import VS Developer environment
$VsDevShellPaths = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\Launch-VsDevShell.ps1",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\Launch-VsDevShell.ps1",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
)

$VsDevShell = $null
foreach ($path in $VsDevShellPaths) {
    if (Test-Path $path) {
        $VsDevShell = $path
        break
    }
}

if ($null -eq $VsDevShell) {
    Write-Host "Error: Could not find Visual Studio 2022 installation" -ForegroundColor Red
    Write-Host "Please install Visual Studio 2022 or update the script with your VS path" -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

& $VsDevShell -Arch amd64 -SkipAutomaticLocation

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
Set-Location (Join-Path $ScriptDir "build")

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building Project ($Config)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Build main executable first
Write-Host "Building VulkanCube..." -ForegroundColor Yellow
cmake --build . --config $Config --target VulkanCube

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Build failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building Tests ($Config)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Determine which tests to build
$BuildTargets = @()
if ($IntegrationOnly) {
    $BuildTargets += "phyxel_integration_tests"
} elseif ($UnitOnly) {
    $BuildTargets += "phyxel_tests"
} else {
    $BuildTargets += "phyxel_tests"
    $BuildTargets += "phyxel_integration_tests"
}

$BuildSuccess = $true
foreach ($BuildTarget in $BuildTargets) {
    Write-Host "Building $BuildTarget..." -ForegroundColor Yellow
    cmake --build . --config $Config --target $BuildTarget
    
    if ($LASTEXITCODE -ne 0) {
        $BuildSuccess = $false
        break
    }
}

if ($BuildSuccess) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    
    $TestsFailed = $false
    
    if (-not $IntegrationOnly) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Unit Tests (251 tests)..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $UnitTestExe = Join-Path "tests" (Join-Path $Config "phyxel_tests.exe")
        if (Test-Path $UnitTestExe) {
            & $UnitTestExe
            
            if ($LASTEXITCODE -ne 0) {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "UNIT TESTS FAILED!" -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                $TestsFailed = $true
            } else {
                Write-Host ""
                Write-Host "Unit tests passed!" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Unit test executable not found: $UnitTestExe" -ForegroundColor Yellow
        }
    }
    
    if (-not $UnitOnly) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Integration Tests..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $IntegrationTestExe = Join-Path "tests" (Join-Path "integration" (Join-Path $Config "phyxel_integration_tests.exe"))
        if (Test-Path $IntegrationTestExe) {
            & $IntegrationTestExe
            
            if ($LASTEXITCODE -ne 0) {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "INTEGRATION TESTS FAILED!" -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                $TestsFailed = $true
            } else {
                Write-Host ""
                Write-Host "Integration tests passed!" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Integration test executable not found: $IntegrationTestExe" -ForegroundColor Yellow
        }
    }
    
    if ($TestsFailed) {
        Set-Location $OriginalLocation
        exit 1
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "All tests passed! Build complete." -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Build failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Set-Location $OriginalLocation
    exit 1
}

Set-Location $OriginalLocation
Write-Host ""
Write-Host "Done!" -ForegroundColor Green
