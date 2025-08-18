@echo off
REM Build shaders script for Windows
REM Requires Vulkan SDK to be installed

echo Building shaders...

REM Try to find glslangValidator in common locations
set GLSLANG=""
if exist "%VULKAN_SDK%\Bin\glslangValidator.exe" (
    set GLSLANG="%VULKAN_SDK%\Bin\glslangValidator.exe"
) else if exist "%VULKAN_SDK%\bin\glslangValidator.exe" (
    set GLSLANG="%VULKAN_SDK%\bin\glslangValidator.exe"
) else (
    REM Try to find it in PATH
    where glslangValidator.exe >nul 2>nul
    if %errorlevel% equ 0 (
        set GLSLANG=glslangValidator.exe
    ) else (
        REM Try alternative names
        where glslc.exe >nul 2>nul
        if %errorlevel% equ 0 (
            set GLSLANG=glslc.exe
            set USE_GLSLC=1
        ) else (
            echo ERROR: Could not find glslangValidator or glslc
            echo Please ensure Vulkan SDK is installed and VULKAN_SDK environment variable is set
            echo Current VULKAN_SDK: %VULKAN_SDK%
            pause
            exit /b 1
        )
    )
)

echo Using shader compiler: %GLSLANG%

if defined USE_GLSLC (
    echo Using glslc syntax...
    echo Compiling vertex shader...
    %GLSLANG% -fshader-stage=vert shaders\cube.vert -o shaders\cube.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile vertex shader
        pause
        exit /b 1
    )

    echo Compiling dynamic subcube vertex shader...
    %GLSLANG% -fshader-stage=vert shaders\dynamic_subcube.vert -o shaders\dynamic_subcube.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile dynamic subcube vertex shader
        pause
        exit /b 1
    )

    echo Compiling fragment shader...
    %GLSLANG% -fshader-stage=frag shaders\cube.frag -o shaders\cube.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile fragment shader
        pause
        exit /b 1
    )

    echo Compiling compute shader...
    %GLSLANG% -fshader-stage=comp shaders\frustum_cull.comp -o shaders\frustum_cull.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile compute shader
        pause
        exit /b 1
    )
) else (
    echo Using glslangValidator syntax...
    echo Compiling vertex shader...
    %GLSLANG% -V shaders\cube.vert -o shaders\cube.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile vertex shader
        pause
        exit /b 1
    )

    echo Compiling dynamic subcube vertex shader...
    %GLSLANG% -V shaders\dynamic_subcube.vert -o shaders\dynamic_subcube.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile dynamic subcube vertex shader
        pause
        exit /b 1
    )

    echo Compiling fragment shader...
    %GLSLANG% -V shaders\cube.frag -o shaders\cube.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile fragment shader
        pause
        exit /b 1
    )

    echo Compiling compute shader...
    %GLSLANG% -V shaders\frustum_cull.comp -o shaders\frustum_cull.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile compute shader
        pause
        exit /b 1
    )
)

echo All shaders compiled successfully!
REM pause
