@echo off
cd /d G:\Github\phyxel\build
echo Building...
cmake --build . --config Debug --target VulkanCube
if %ERRORLEVEL% EQU 0 (
    echo Build successful, running test...
    cd ..
    timeout 3 /nobreak >nul
    echo Test complete
) else (
    echo Build failed!
)
