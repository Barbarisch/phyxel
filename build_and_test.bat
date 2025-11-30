@echo off
REM Windows Batch Build and Test Script for Phyxel
REM Builds the project and runs unit tests

REM Store original directory
set ORIGINAL_DIR=%CD%

REM Change to script directory (project root)
cd /d "%~dp0"

REM Navigate to build directory
cd build

echo Building VulkanCube...
cmake --build . --config Debug --target VulkanCube

if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    cd ..
    
    echo.
    echo ========================================
    echo Running Unit Tests...
    echo ========================================
    call run_tests.bat
    
    if %ERRORLEVEL% NEQ 0 (
        echo.
        echo ========================================
        echo UNIT TESTS FAILED!
        echo ========================================
        cd /d "%ORIGINAL_DIR%"
        exit /b 1
    )
    
    echo.
    echo ========================================
    echo All tests passed! Build complete.
    echo ========================================
) else (
    echo Build failed!
    cd /d "%ORIGINAL_DIR%"
    exit /b 1
)

REM Return to original directory
cd /d "%ORIGINAL_DIR%"