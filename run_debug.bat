@echo off
echo Running VulkanCube with debug output...
echo ================================================

REM Run the application and capture output
VulkanCube.exe > debug_output.txt 2>&1

REM Show the first part of the output
echo Debug output captured. First 100 lines:
echo ================================================
head -n 100 debug_output.txt 2>nul || (
    REM If head command is not available, use more
    more /e +1 debug_output.txt | findstr /n "^" | findstr "^[1-9][0-9]\?:"
)

echo ================================================
echo Full output saved to debug_output.txt
echo Press any key to continue...
pause >nul
