@echo off
cd /d G:\Github\phyxel\build
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
        cd /d G:\Github\phyxel
        exit /b 1
    )
    
    echo.
    echo ========================================
    echo All tests passed! Build complete.
    echo ========================================
) else (
    echo Build failed!
    cd /d G:\Github\phyxel
    exit /b 1
)
cd /d G:\Github\phyxel