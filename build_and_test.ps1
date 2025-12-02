# PowerShell Build and Test Script for Phyxel
# Sets up VS environment and builds/tests the project

param(
    [string]$Config = "Debug",
    [switch]$RunTests,
    [switch]$UnitOnly,
    [switch]$IntegrationOnly,
    [switch]$BenchmarkOnly,
    [switch]$StressOnly,
    [switch]$E2EOnly,
    [switch]$SkipBuild,
    [switch]$HeavyTests
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
} elseif ($BenchmarkOnly) {
    Write-Host "Mode: Benchmark Tests Only" -ForegroundColor Yellow
} elseif ($StressOnly) {
    Write-Host "Mode: Stress Tests Only" -ForegroundColor Yellow
} elseif ($E2EOnly) {
    Write-Host "Mode: End-to-End Tests Only" -ForegroundColor Yellow
} elseif ($RunTests) {
    Write-Host "Mode: Build + Fast Unit Tests" -ForegroundColor Yellow
} else {
    Write-Host "Mode: Build Only (no tests)" -ForegroundColor Yellow
    Write-Host "       Use -RunTests to run tests after build" -ForegroundColor Gray
}

if ($HeavyTests) {
    Write-Host "Configuration: HEAVY Stress Tests Enabled" -ForegroundColor Red
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

if (-not $SkipBuild) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Configuring CMake..." -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    # Reconfigure CMake (important when new source files are added/removed)
    $CMakeArgs = @("-B", "build", "-G", "Visual Studio 17 2022", "-A", "x64")
    if ($HeavyTests) {
        $CMakeArgs += "-DPHYXEL_HEAVY_TESTS=ON"
    } else {
        $CMakeArgs += "-DPHYXEL_HEAVY_TESTS=OFF"
    }
    cmake @CMakeArgs

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
} elseif ($BenchmarkOnly) {
    $BuildTargets += "phyxel_benchmarks"
} elseif ($StressOnly) {
    $BuildTargets += "phyxel_stress_tests"
} elseif ($E2EOnly) {
    $BuildTargets += "phyxel_e2e_tests"
} else {
    $BuildTargets += "phyxel_tests"
    $BuildTargets += "phyxel_integration_tests"
    $BuildTargets += "phyxel_benchmarks"
    $BuildTargets += "phyxel_stress_tests"
    $BuildTargets += "phyxel_e2e_tests"
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

} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "Skipping build (using existing binaries)" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Yellow
    Set-Location (Join-Path $ScriptDir "build")
}

$BuildSuccess = $true

if ($BuildSuccess -and ($RunTests -or $UnitOnly -or $IntegrationOnly -or $BenchmarkOnly -or $StressOnly -or $E2EOnly)) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Running Tests..." -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    $TestsFailed = $false
    
    if ($UnitOnly -or ($RunTests -and -not $IntegrationOnly -and -not $BenchmarkOnly -and -not $StressOnly -and -not $E2EOnly)) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Unit Tests (276 core tests, ~5-10 seconds)..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $UnitTestExe = Join-Path "tests" (Join-Path $Config "phyxel_tests.exe")
        if (Test-Path $UnitTestExe) {
            # Exclude slow benchmark tests by default (ChunkBenchmarks takes ~88s)
            if ($UnitOnly) {
                # When explicitly requested, run all unit tests including benchmarks
                & $UnitTestExe
            } else {
                # By default, skip the slow benchmark tests
                & $UnitTestExe --gtest_filter=-ChunkBenchmarks.*:PhysicsBenchmarks.*
            }
            
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
    
    if ($IntegrationOnly) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Integration Tests (36 tests)..." -ForegroundColor Cyan
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
    
    if ($BenchmarkOnly) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Benchmark Tests (11 tests)..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $BenchmarkTestExe = Join-Path "tests" (Join-Path "benchmark" (Join-Path $Config "phyxel_benchmarks.exe"))
        if (Test-Path $BenchmarkTestExe) {
            & $BenchmarkTestExe
            
            if ($LASTEXITCODE -ne 0) {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "BENCHMARK TESTS FAILED!" -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                $TestsFailed = $true
            } else {
                Write-Host ""
                Write-Host "Benchmark tests passed!" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Benchmark test executable not found: $BenchmarkTestExe" -ForegroundColor Yellow
        }
    }
    
    if ($StressOnly) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running Stress Tests (24 tests)..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $StressTestExe = Join-Path "tests" (Join-Path "stress" (Join-Path $Config "phyxel_stress_tests.exe"))
        if (Test-Path $StressTestExe) {
            & $StressTestExe
            
            if ($LASTEXITCODE -ne 0) {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "STRESS TESTS FAILED!" -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                $TestsFailed = $true
            } else {
                Write-Host ""
                Write-Host "Stress tests passed!" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Stress test executable not found: $StressTestExe" -ForegroundColor Yellow
        }
    }
    
    if ($E2EOnly) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host "Running End-to-End Tests (25 tests)..." -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        $E2ETestExe = Join-Path "tests" (Join-Path "e2e" (Join-Path $Config "phyxel_e2e_tests.exe"))
        if (Test-Path $E2ETestExe) {
            $E2ETestDir = Join-Path "tests" (Join-Path "e2e" $Config)
            Push-Location $E2ETestDir
            & ".\phyxel_e2e_tests.exe"
            $TestExitCode = $LASTEXITCODE
            Pop-Location
            
            if ($TestExitCode -ne 0) {
                Write-Host ""
                Write-Host "========================================" -ForegroundColor Red
                Write-Host "END-TO-END TESTS FAILED!" -ForegroundColor Red
                Write-Host "========================================" -ForegroundColor Red
                $TestsFailed = $true
            } else {
                Write-Host ""
                Write-Host "End-to-end tests passed!" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: E2E test executable not found: $E2ETestExe" -ForegroundColor Yellow
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
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
}

Set-Location $OriginalLocation
Write-Host ""
Write-Host "Done!" -ForegroundColor Green
