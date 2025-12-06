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
    echo Compiling static voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\static_voxel.vert -o shaders\static_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile vertex shader
        pause
        exit /b 1
    )

    echo Compiling dynamic voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\dynamic_voxel.vert -o shaders\dynamic_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile dynamic voxel vertex shader
        pause
        exit /b 1
    )

    echo Compiling voxel fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\voxel.frag -o shaders\voxel.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile fragment shader
        pause
        exit /b 1
    )

    echo Compiling debug voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\debug_voxel.vert -o shaders\debug_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debug vertex shader
        pause
        exit /b 1
    )

    echo Compiling debug voxel fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\debug_voxel.frag -o shaders\debug_voxel.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debug fragment shader
        pause
        exit /b 1
    )

    echo Compiling compute shader...
    %GLSLANG% -fshader-stage=comp -I. shaders\frustum_cull.comp -o shaders\frustum_cull.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile compute shader
        pause
        exit /b 1
    )
) else (
    echo Using glslangValidator syntax...
    echo Compiling static voxel vertex shader...
    %GLSLANG% -V -I. shaders\static_voxel.vert -o shaders\static_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile vertex shader
        pause
        exit /b 1
    )

    echo Compiling dynamic voxel vertex shader...
    %GLSLANG% -V -I. shaders\dynamic_voxel.vert -o shaders\dynamic_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile dynamic voxel vertex shader
        pause
        exit /b 1
    )

    echo Compiling voxel fragment shader...
    %GLSLANG% -V -I. shaders\voxel.frag -o shaders\voxel.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile fragment shader
        pause
        exit /b 1
    )

    echo Compiling debug voxel vertex shader...
    %GLSLANG% -V -I. shaders\debug_voxel.vert -o shaders\debug_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debug vertex shader
        pause
        exit /b 1
    )

    echo Compiling debug voxel fragment shader...
    %GLSLANG% -V -I. shaders\debug_voxel.frag -o shaders\debug_voxel.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debug fragment shader
        pause
        exit /b 1
    )

    echo Compiling debug line vertex shader...
    %GLSLANG% -V -I. shaders\debug_line.vert -o shaders\debug_line.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debug line vertex shader
        pause
        exit /b 1
    )

    echo Compiling debug line fragment shader...
    %GLSLANG% -V -I. shaders\debug_line.frag -o shaders\debug_line.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debug line fragment shader
        pause
        exit /b 1
    )

    echo Compiling compute shader...
    %GLSLANG% -V -I. shaders\frustum_cull.comp -o shaders\frustum_cull.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile compute shader
        pause
        exit /b 1
    )
)

echo All shaders compiled successfully!
REM pause
