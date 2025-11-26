@echo off
REM Build and run unit tests for Phyxel

echo ========================================
echo Building Phyxel Unit Tests...
echo ========================================
cmake --build build --config Debug --target phyxel_tests

if %ERRORLEVEL% neq 0 (
    echo.
    echo ========================================
    echo BUILD FAILED
    echo ========================================
    exit /b 1
)

echo.
echo ========================================
echo Running Unit Tests...
echo ========================================
.\build\tests\Debug\phyxel_tests.exe

if %ERRORLEVEL% neq 0 (
    echo.
    echo ========================================
    echo TESTS FAILED
    echo ========================================
    exit /b 1
)

echo.
echo ========================================
echo ALL TESTS PASSED!
echo ========================================
