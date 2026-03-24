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

    echo Compiling character vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\character.vert -o shaders\character.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile character vertex shader
        pause
        exit /b 1
    )

    echo Compiling character fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\character.frag -o shaders\character.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile character fragment shader
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

    echo Compiling shadow vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\shadow.vert -o shaders\shadow.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile shadow vertex shader
        pause
        exit /b 1
    )

    echo Compiling shadow fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\shadow.frag -o shaders\shadow.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile shadow fragment shader
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

    echo Compiling post-process vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\post_process.vert -o shaders\post_process.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile post-process vertex shader
        pause
        exit /b 1
    )

    echo Compiling post-process fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\post_process.frag -o shaders\post_process.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile post-process fragment shader
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

    echo Compiling debris vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\debris.vert -o shaders\debris.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debris vertex shader
        pause
        exit /b 1
    )

    echo Compiling debris fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\debris.frag -o shaders\debris.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debris fragment shader
        pause
        exit /b 1
    )

    echo Compiling UI vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\ui.vert -o shaders\ui.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile UI vertex shader
        pause
        exit /b 1
    )

    echo Compiling UI fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\ui.frag -o shaders\ui.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile UI fragment shader
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

    echo Compiling character vertex shader...
    %GLSLANG% -V -I. shaders\character.vert -o shaders\character.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile character vertex shader
        pause
        exit /b 1
    )

    echo Compiling instanced character vertex shader...
    %GLSLANG% -V -I. shaders\character_instanced.vert -o shaders\character_instanced.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile instanced character vertex shader
        pause
        exit /b 1
    )

    echo Compiling character fragment shader...
    %GLSLANG% -V -I. shaders\character.frag -o shaders\character.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile character fragment shader
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

    echo Compiling shadow vertex shader...
    %GLSLANG% -V -I. shaders\shadow.vert -o shaders\shadow.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile shadow vertex shader
        pause
        exit /b 1
    )

    echo Compiling shadow fragment shader...
    %GLSLANG% -V -I. shaders\shadow.frag -o shaders\shadow.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile shadow fragment shader
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

    echo Compiling post-process vertex shader...
    %GLSLANG% -V -I. shaders\post_process.vert -o shaders\post_process.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile post-process vertex shader
        pause
        exit /b 1
    )

    echo Compiling post-process fragment shader...
    %GLSLANG% -V -I. shaders\post_process.frag -o shaders\post_process.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile post-process fragment shader
        pause
        exit /b 1
    )

    echo Compiling blur fragment shader...
    %GLSLANG% -V -I. shaders\blur.frag -o shaders\blur.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile blur fragment shader
        pause
        exit /b 1
    )
    echo Compiling debris vertex shader...
    %GLSLANG% -V -I. shaders\debris.vert -o shaders\debris.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debris vertex shader
        pause
        exit /b 1
    )

    echo Compiling debris fragment shader...
    %GLSLANG% -V -I. shaders\debris.frag -o shaders\debris.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile debris fragment shader
        pause
        exit /b 1
    )

    echo Compiling UI vertex shader...
    %GLSLANG% -V -I. shaders\ui.vert -o shaders\ui.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile UI vertex shader
        pause
        exit /b 1
    )

    echo Compiling UI fragment shader...
    %GLSLANG% -V -I. shaders\ui.frag -o shaders\ui.frag.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile UI fragment shader
        pause
        exit /b 1
    )
)

echo All shaders compiled successfully!
REM pause
