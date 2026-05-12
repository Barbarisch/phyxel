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

    echo Compiling particle integrate compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_integrate.comp -o shaders\particle_integrate.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_integrate.comp
        pause
        exit /b 1
    )

    echo Compiling particle collide compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_collide.comp -o shaders\particle_collide.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_collide.comp
        pause
        exit /b 1
    )

    echo Compiling particle expand compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_expand.comp -o shaders\particle_expand.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_expand.comp
        pause
        exit /b 1
    )

    echo Compiling particle grid clear compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_grid_clear.comp -o shaders\particle_grid_clear.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_grid_clear.comp
        pause
        exit /b 1
    )

    echo Compiling particle grid build compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_grid_build.comp -o shaders\particle_grid_build.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_grid_build.comp
        pause
        exit /b 1
    )

    echo Compiling particle sort scan compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_sort_scan.comp -o shaders\particle_sort_scan.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_sort_scan.comp
        pause
        exit /b 1
    )

    echo Compiling particle sort scatter compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_sort_scatter.comp -o shaders\particle_sort_scatter.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_sort_scatter.comp
        pause
        exit /b 1
    )

    echo Compiling solver_sync_in compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_sync_in.comp -o shaders\solver_sync_in.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_sync_in.comp & pause & exit /b 1 )

    echo Compiling solver_integrate compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_integrate.comp -o shaders\solver_integrate.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_integrate.comp & pause & exit /b 1 )

    echo Compiling solver_narrowphase compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_narrowphase.comp -o shaders\solver_narrowphase.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_narrowphase.comp & pause & exit /b 1 )

    echo Compiling solver_voxel compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_voxel.comp -o shaders\solver_voxel.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_voxel.comp & pause & exit /b 1 )

    echo Compiling solver_jacobi compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_jacobi.comp -o shaders\solver_jacobi.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_jacobi.comp & pause & exit /b 1 )

    echo Compiling solver_sync_out compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_sync_out.comp -o shaders\solver_sync_out.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_sync_out.comp & pause & exit /b 1 )

    echo Compiling solver_csr_clear compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_csr_clear.comp -o shaders\solver_csr_clear.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_csr_clear.comp & pause & exit /b 1 )

    echo Compiling solver_csr_count compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_csr_count.comp -o shaders\solver_csr_count.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_csr_count.comp & pause & exit /b 1 )

    echo Compiling solver_prefix_sum compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_prefix_sum.comp -o shaders\solver_prefix_sum.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_prefix_sum.comp & pause & exit /b 1 )

    echo Compiling solver_csr_scatter compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_csr_scatter.comp -o shaders\solver_csr_scatter.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_csr_scatter.comp & pause & exit /b 1 )

    echo Compiling solver_graph_color compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_graph_color.comp -o shaders\solver_graph_color.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_graph_color.comp & pause & exit /b 1 )

    echo Compiling solver_body_color compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_body_color.comp -o shaders\solver_body_color.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_body_color.comp & pause & exit /b 1 )

    echo Compiling solver_dual compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_dual.comp -o shaders\solver_dual.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_dual.comp & pause & exit /b 1 )

    echo Compiling solver_primal compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_primal.comp -o shaders\solver_primal.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_primal.comp & pause & exit /b 1 )

    echo Compiling solver_warmstart_save compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_warmstart_save.comp -o shaders\solver_warmstart_save.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_warmstart_save.comp & pause & exit /b 1 )

    echo Compiling solver_hardcontact compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\solver_hardcontact.comp -o shaders\solver_hardcontact.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_hardcontact.comp & pause & exit /b 1 )

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

    echo Compiling kinematic voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\kinematic_voxel.vert -o shaders\kinematic_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile kinematic voxel vertex shader
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
    echo Compiling particle integrate compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_integrate.comp -o shaders\particle_integrate.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_integrate.comp
        pause
        exit /b 1
    )

    echo Compiling particle collide compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_collide.comp -o shaders\particle_collide.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_collide.comp
        pause
        exit /b 1
    )

    echo Compiling particle expand compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_expand.comp -o shaders\particle_expand.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_expand.comp
        pause
        exit /b 1
    )

    echo Compiling particle grid clear compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_grid_clear.comp -o shaders\particle_grid_clear.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_grid_clear.comp
        pause
        exit /b 1
    )

    echo Compiling particle grid build compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_grid_build.comp -o shaders\particle_grid_build.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_grid_build.comp
        pause
        exit /b 1
    )

    echo Compiling particle sort scan compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_sort_scan.comp -o shaders\particle_sort_scan.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_sort_scan.comp
        pause
        exit /b 1
    )

    echo Compiling particle sort scatter compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_sort_scatter.comp -o shaders\particle_sort_scatter.comp.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile particle_sort_scatter.comp
        pause
        exit /b 1
    )

    echo Compiling solver_sync_in compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_sync_in.comp -o shaders\solver_sync_in.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_sync_in.comp & pause & exit /b 1 )

    echo Compiling solver_integrate compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_integrate.comp -o shaders\solver_integrate.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_integrate.comp & pause & exit /b 1 )

    echo Compiling solver_narrowphase compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_narrowphase.comp -o shaders\solver_narrowphase.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_narrowphase.comp & pause & exit /b 1 )

    echo Compiling solver_voxel compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_voxel.comp -o shaders\solver_voxel.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_voxel.comp & pause & exit /b 1 )

    echo Compiling solver_jacobi compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_jacobi.comp -o shaders\solver_jacobi.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_jacobi.comp & pause & exit /b 1 )

    echo Compiling solver_sync_out compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_sync_out.comp -o shaders\solver_sync_out.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_sync_out.comp & pause & exit /b 1 )

    echo Compiling solver_csr_clear compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_csr_clear.comp -o shaders\solver_csr_clear.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_csr_clear.comp & pause & exit /b 1 )

    echo Compiling solver_csr_count compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_csr_count.comp -o shaders\solver_csr_count.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_csr_count.comp & pause & exit /b 1 )

    echo Compiling solver_prefix_sum compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_prefix_sum.comp -o shaders\solver_prefix_sum.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_prefix_sum.comp & pause & exit /b 1 )

    echo Compiling solver_csr_scatter compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_csr_scatter.comp -o shaders\solver_csr_scatter.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_csr_scatter.comp & pause & exit /b 1 )

    echo Compiling solver_graph_color compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_graph_color.comp -o shaders\solver_graph_color.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_graph_color.comp & pause & exit /b 1 )

    echo Compiling solver_body_color compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_body_color.comp -o shaders\solver_body_color.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_body_color.comp & pause & exit /b 1 )

    echo Compiling solver_dual compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_dual.comp -o shaders\solver_dual.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_dual.comp & pause & exit /b 1 )

    echo Compiling solver_primal compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_primal.comp -o shaders\solver_primal.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_primal.comp & pause & exit /b 1 )

    echo Compiling solver_warmstart_save compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_warmstart_save.comp -o shaders\solver_warmstart_save.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_warmstart_save.comp & pause & exit /b 1 )

    echo Compiling solver_hardcontact compute shader...
    %GLSLANG% -V -Ishaders shaders\solver_hardcontact.comp -o shaders\solver_hardcontact.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile solver_hardcontact.comp & pause & exit /b 1 )

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

    echo Compiling kinematic voxel vertex shader...
    %GLSLANG% -V -I. shaders\kinematic_voxel.vert -o shaders\kinematic_voxel.vert.spv
    if %errorlevel% neq 0 (
        echo ERROR: Failed to compile kinematic voxel vertex shader
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

echo Compiling character shadow vertex shader...
%GLSLANG% -V -I. shaders\character_shadow.vert -o shaders\character_shadow.vert.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile character_shadow.vert
    pause
    exit /b 1
)

echo Compiling kinematic shadow vertex shader...
%GLSLANG% -V -I. shaders\kinematic_shadow.vert -o shaders\kinematic_shadow.vert.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile kinematic_shadow.vert
    pause
    exit /b 1
)

echo Compiling dynamic shadow vertex shader...
%GLSLANG% -V -I. shaders\dynamic_shadow.vert -o shaders\dynamic_shadow.vert.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile dynamic_shadow.vert
    pause
    exit /b 1
)

echo Compiling SSAO fragment shader...
%GLSLANG% -V -I. shaders\ssao.frag -o shaders\ssao.frag.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile ssao.frag
    pause
    exit /b 1
)

echo Compiling SSAO blur fragment shader...
%GLSLANG% -V -I. shaders\ssao_blur.frag -o shaders\ssao_blur.frag.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile ssao_blur.frag
    pause
    exit /b 1
)

echo Compiling transparent voxel fragment shader...
%GLSLANG% -V -I. shaders\transparent_voxel.frag -o shaders\transparent_voxel.frag.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile transparent_voxel.frag
    pause
    exit /b 1
)

echo Compiling mirror voxel fragment shader...
%GLSLANG% -V -I. shaders\mirror_voxel.frag -o shaders\mirror_voxel.frag.spv
if %errorlevel% neq 0 (
    echo ERROR: Failed to compile mirror_voxel.frag
    pause
    exit /b 1
)

echo All shaders compiled successfully!
REM pause
