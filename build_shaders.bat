@echo off
REM Build shaders script for Windows
REM Requires Vulkan SDK to be installed

REM ---------------------------------------------------------------------------------------
REM ERROR HANDLING IS LOAD-BEARING. Compiles use `%GLSLANG% ... || ( ... exit /b 1 )`.
REM
REM Do NOT rewrite these as a following `if %errorlevel% neq 0`. Most compiles live inside a
REM single `if defined USE_GLSLC ( ... )` block, and cmd expands %VAR% when it PARSES a block,
REM not when it executes each line -- so every such check read ONE stale value captured before
REM any shader compiled. A shader could fail, print its glslc errors, and the script would
REM sail past the check and print "All shaders compiled successfully!" with exit code 0,
REM leaving a STALE committed .spv on disk while CI stayed green. That is the pink-world
REM failure mode (20341333) with the extra twist that the source did not even compile.
REM
REM `setlocal enabledelayedexpansion` + !errorlevel! is NOT the fix: with setlocal active,
REM `exit /b` triggers an implicit endlocal that restores the PREVIOUS errorlevel, so the
REM script halts correctly and still returns 0. `||` tests the command's real exit status as
REM it runs, needs no expansion, and leaves exit /b free to propagate.
REM
REM Set SHADERS_NONINTERACTIVE=1 to skip the `pause` on failure (CI, tests, agents).
REM ---------------------------------------------------------------------------------------

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
            if not defined SHADERS_NONINTERACTIVE pause
            exit /b 1
        )
    )
)

echo Using shader compiler: %GLSLANG%

if defined USE_GLSLC (
    echo Using glslc syntax...
    echo Compiling static voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\static_voxel.vert -o shaders\static_voxel.vert.spv || goto :shader_error

    echo Compiling dynamic voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\dynamic_voxel.vert -o shaders\dynamic_voxel.vert.spv || goto :shader_error

    echo Compiling character vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\character.vert -o shaders\character.vert.spv || goto :shader_error

    echo Compiling character fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\character.frag -o shaders\character.frag.spv || goto :shader_error

    echo Compiling voxel fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\voxel.frag -o shaders\voxel.frag.spv || goto :shader_error

    echo Compiling sky shaders ^(atmosphere^)...
    %GLSLANG% -fshader-stage=vert -I. shaders\sky.vert -o shaders\sky.vert.spv || goto :shader_error
    %GLSLANG% -fshader-stage=frag -I. shaders\sky.frag -o shaders\sky.frag.spv || goto :shader_error

    echo Compiling shadow vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\shadow.vert -o shaders\shadow.vert.spv || goto :shader_error

    echo Compiling shadow fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\shadow.frag -o shaders\shadow.frag.spv || goto :shader_error

    echo Compiling debug voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\debug_voxel.vert -o shaders\debug_voxel.vert.spv || goto :shader_error

    echo Compiling debug voxel fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\debug_voxel.frag -o shaders\debug_voxel.frag.spv || goto :shader_error

    echo Compiling sky shaders ^(atmosphere^)...
    %GLSLANG% -fshader-stage=vert -I. shaders\sky.vert -o shaders\sky.vert.spv || goto :shader_error
    %GLSLANG% -fshader-stage=frag -I. shaders\sky.frag -o shaders\sky.frag.spv || goto :shader_error

    echo Compiling post-process vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\post_process.vert -o shaders\post_process.vert.spv || goto :shader_error

    echo Compiling post-process fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\post_process.frag -o shaders\post_process.frag.spv
    %GLSLANG% -fshader-stage=frag -I. shaders\blit.frag -o shaders\blit.frag.spv || goto :shader_error

    echo Compiling compute shader...
    %GLSLANG% -fshader-stage=comp -I. shaders\frustum_cull.comp -o shaders\frustum_cull.comp.spv || goto :shader_error

    echo Compiling GI probe field compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\gi_probe.comp -o shaders\gi_probe.comp.spv || goto :shader_error

    echo Compiling particle integrate compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_integrate.comp -o shaders\particle_integrate.comp.spv || goto :shader_error

    echo Compiling particle collide compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_collide.comp -o shaders\particle_collide.comp.spv || goto :shader_error

    echo Compiling particle expand compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_expand.comp -o shaders\particle_expand.comp.spv || goto :shader_error

    echo Compiling particle grid clear compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_grid_clear.comp -o shaders\particle_grid_clear.comp.spv || goto :shader_error

    echo Compiling particle grid build compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_grid_build.comp -o shaders\particle_grid_build.comp.spv || goto :shader_error

    echo Compiling particle sort scan compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_sort_scan.comp -o shaders\particle_sort_scan.comp.spv || goto :shader_error

    echo Compiling particle sort scatter compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_sort_scatter.comp -o shaders\particle_sort_scatter.comp.spv || goto :shader_error

    echo Compiling particle scan block compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_scan_block.comp -o shaders\particle_scan_block.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile particle_scan_block.comp & pause & exit /b 1 )

    echo Compiling particle scan blocksums compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_scan_blocksums.comp -o shaders\particle_scan_blocksums.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile particle_scan_blocksums.comp & pause & exit /b 1 )

    echo Compiling particle scan add compute shader...
    %GLSLANG% -fshader-stage=comp -Ishaders shaders\particle_scan_add.comp -o shaders\particle_scan_add.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile particle_scan_add.comp & pause & exit /b 1 )

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
    %GLSLANG% -fshader-stage=vert -I. shaders\debris.vert -o shaders\debris.vert.spv || goto :shader_error

    echo Compiling debris fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\debris.frag -o shaders\debris.frag.spv || goto :shader_error

    echo Compiling kinematic voxel vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\kinematic_voxel.vert -o shaders\kinematic_voxel.vert.spv || goto :shader_error

    echo Compiling UI vertex shader...
    %GLSLANG% -fshader-stage=vert -I. shaders\ui.vert -o shaders\ui.vert.spv || goto :shader_error

    echo Compiling UI fragment shader...
    %GLSLANG% -fshader-stage=frag -I. shaders\ui.frag -o shaders\ui.frag.spv || goto :shader_error
) else (
    echo Using glslangValidator syntax...
    echo Compiling static voxel vertex shader...
    %GLSLANG% -V -I. shaders\static_voxel.vert -o shaders\static_voxel.vert.spv || goto :shader_error

    echo Compiling dynamic voxel vertex shader...
    %GLSLANG% -V -I. shaders\dynamic_voxel.vert -o shaders\dynamic_voxel.vert.spv || goto :shader_error

    echo Compiling character vertex shader...
    %GLSLANG% -V -I. shaders\character.vert -o shaders\character.vert.spv || goto :shader_error

    echo Compiling instanced character vertex shader...
    %GLSLANG% -V -I. shaders\character_instanced.vert -o shaders\character_instanced.vert.spv || goto :shader_error

    echo Compiling character fragment shader...
    %GLSLANG% -V -I. shaders\character.frag -o shaders\character.frag.spv || goto :shader_error

    echo Compiling voxel fragment shader...
    %GLSLANG% -V -I. shaders\voxel.frag -o shaders\voxel.frag.spv || goto :shader_error

    echo Compiling sky shaders ^(atmosphere^)...
    %GLSLANG% -V -I. shaders\sky.vert -o shaders\sky.vert.spv || goto :shader_error
    %GLSLANG% -V -I. shaders\sky.frag -o shaders\sky.frag.spv || goto :shader_error

    echo Compiling shadow vertex shader...
    %GLSLANG% -V -I. shaders\shadow.vert -o shaders\shadow.vert.spv || goto :shader_error

    echo Compiling shadow fragment shader...
    %GLSLANG% -V -I. shaders\shadow.frag -o shaders\shadow.frag.spv || goto :shader_error

    echo Compiling debug voxel vertex shader...
    %GLSLANG% -V -I. shaders\debug_voxel.vert -o shaders\debug_voxel.vert.spv || goto :shader_error

    echo Compiling debug voxel fragment shader...
    %GLSLANG% -V -I. shaders\debug_voxel.frag -o shaders\debug_voxel.frag.spv || goto :shader_error

    echo Compiling sky shaders ^(atmosphere^)...
    %GLSLANG% -V -I. shaders\sky.vert -o shaders\sky.vert.spv || goto :shader_error
    %GLSLANG% -V -I. shaders\sky.frag -o shaders\sky.frag.spv || goto :shader_error

    echo Compiling debug line vertex shader...
    %GLSLANG% -V -I. shaders\debug_line.vert -o shaders\debug_line.vert.spv || goto :shader_error

    echo Compiling debug line fragment shader...
    %GLSLANG% -V -I. shaders\debug_line.frag -o shaders\debug_line.frag.spv || goto :shader_error

    echo Compiling post-process vertex shader...
    %GLSLANG% -V -I. shaders\post_process.vert -o shaders\post_process.vert.spv || goto :shader_error

    echo Compiling post-process fragment shader...
    %GLSLANG% -V -I. shaders\post_process.frag -o shaders\post_process.frag.spv
    %GLSLANG% -V -I. shaders\blit.frag -o shaders\blit.frag.spv || goto :shader_error

    echo Compiling blur fragment shader...
    %GLSLANG% -V -I. shaders\blur.frag -o shaders\blur.frag.spv || goto :shader_error
    echo Compiling GI probe field compute shader...
    %GLSLANG% -V -Ishaders shaders\gi_probe.comp -o shaders\gi_probe.comp.spv || goto :shader_error

    echo Compiling particle integrate compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_integrate.comp -o shaders\particle_integrate.comp.spv || goto :shader_error

    echo Compiling particle collide compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_collide.comp -o shaders\particle_collide.comp.spv || goto :shader_error

    echo Compiling particle expand compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_expand.comp -o shaders\particle_expand.comp.spv || goto :shader_error

    echo Compiling particle grid clear compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_grid_clear.comp -o shaders\particle_grid_clear.comp.spv || goto :shader_error

    echo Compiling particle grid build compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_grid_build.comp -o shaders\particle_grid_build.comp.spv || goto :shader_error

    echo Compiling particle sort scan compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_sort_scan.comp -o shaders\particle_sort_scan.comp.spv || goto :shader_error

    echo Compiling particle sort scatter compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_sort_scatter.comp -o shaders\particle_sort_scatter.comp.spv || goto :shader_error

    echo Compiling particle scan block compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_scan_block.comp -o shaders\particle_scan_block.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile particle_scan_block.comp & pause & exit /b 1 )

    echo Compiling particle scan blocksums compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_scan_blocksums.comp -o shaders\particle_scan_blocksums.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile particle_scan_blocksums.comp & pause & exit /b 1 )

    echo Compiling particle scan add compute shader...
    %GLSLANG% -V -Ishaders shaders\particle_scan_add.comp -o shaders\particle_scan_add.comp.spv
    if %errorlevel% neq 0 ( echo ERROR: Failed to compile particle_scan_add.comp & pause & exit /b 1 )

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
    %GLSLANG% -V -I. shaders\debris.vert -o shaders\debris.vert.spv || goto :shader_error

    echo Compiling debris fragment shader...
    %GLSLANG% -V -I. shaders\debris.frag -o shaders\debris.frag.spv || goto :shader_error

    echo Compiling kinematic voxel vertex shader...
    %GLSLANG% -V -I. shaders\kinematic_voxel.vert -o shaders\kinematic_voxel.vert.spv || goto :shader_error

    echo Compiling UI vertex shader...
    %GLSLANG% -V -I. shaders\ui.vert -o shaders\ui.vert.spv || goto :shader_error

    echo Compiling UI fragment shader...
    %GLSLANG% -V -I. shaders\ui.frag -o shaders\ui.frag.spv || goto :shader_error
)

echo Compiling character shadow vertex shader...
%GLSLANG% -V -I. shaders\character_shadow.vert -o shaders\character_shadow.vert.spv || goto :shader_error

echo Compiling kinematic shadow vertex shader...
%GLSLANG% -V -I. shaders\kinematic_shadow.vert -o shaders\kinematic_shadow.vert.spv || goto :shader_error

echo Compiling dynamic shadow vertex shader...
%GLSLANG% -V -I. shaders\dynamic_shadow.vert -o shaders\dynamic_shadow.vert.spv || goto :shader_error

echo Compiling SSAO fragment shader...
%GLSLANG% -V -I. shaders\ssao.frag -o shaders\ssao.frag.spv || goto :shader_error

echo Compiling SSAO blur fragment shader...
%GLSLANG% -V -I. shaders\ssao_blur.frag -o shaders\ssao_blur.frag.spv || goto :shader_error

echo Compiling transparent voxel fragment shader...
%GLSLANG% -V -I. shaders\transparent_voxel.frag -o shaders\transparent_voxel.frag.spv || goto :shader_error

    echo Compiling sky shaders ^(atmosphere^)...
    %GLSLANG% -V -I. shaders\sky.vert -o shaders\sky.vert.spv || goto :shader_error
    %GLSLANG% -V -I. shaders\sky.frag -o shaders\sky.frag.spv || goto :shader_error

echo Compiling mirror voxel fragment shader...
%GLSLANG% -V -I. shaders\mirror_voxel.frag -o shaders\mirror_voxel.frag.spv || goto :shader_error

    echo Compiling sky shaders ^(atmosphere^)...
    %GLSLANG% -V -I. shaders\sky.vert -o shaders\sky.vert.spv || goto :shader_error
    %GLSLANG% -V -I. shaders\sky.frag -o shaders\sky.frag.spv || goto :shader_error

echo Compiling VFX particle vertex shader...
%GLSLANG% -V -I. shaders\vfx.vert -o shaders\vfx.vert.spv || goto :shader_error

echo Compiling VFX particle fragment shader...
%GLSLANG% -V -I. shaders\vfx.frag -o shaders\vfx.frag.spv || goto :shader_error

echo Compiling water vertex shader...
%GLSLANG% -V -I. shaders\water.vert -o shaders\water.vert.spv || goto :shader_error

echo Compiling water fragment shader...
%GLSLANG% -V -I. shaders\water.frag -o shaders\water.frag.spv || goto :shader_error

echo Compiling water cell vertex shader...
%GLSLANG% -V -I. shaders\water_cell.vert -o shaders\water_cell.vert.spv || goto :shader_error

echo Compiling water cell fragment shader...
%GLSLANG% -V -I. shaders\water_cell.frag -o shaders\water_cell.frag.spv || goto :shader_error

echo Compiling underwater overlay fragment shader...
%GLSLANG% -V -I. shaders\water_underwater.frag -o shaders\water_underwater.frag.spv || goto :shader_error

echo Compiling water flow compute shader...
%GLSLANG% -V -Ishaders shaders\water_flow.comp -o shaders\water_flow.comp.spv || goto :shader_error

echo Compiling grass vertex shader...
%GLSLANG% -V -I. shaders\grass.vert -o shaders\grass.vert.spv || goto :shader_error

echo Compiling grass shadow vertex shader...
%GLSLANG% -V -I. shaders\grass_shadow.vert -o shaders\grass_shadow.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile grass_shadow.vert
    exit /b 1
)

echo Compiling grass shadow fragment shader...
%GLSLANG% -V -I. shaders\grass_shadow.frag -o shaders\grass_shadow.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile grass_shadow.frag
    exit /b 1
)

echo Compiling grass fragment shader...
%GLSLANG% -V -I. shaders\grass.frag -o shaders\grass.frag.spv || goto :shader_error

echo Compiling foliage vertex shader...
%GLSLANG% -V -I. shaders\foliage.vert -o shaders\foliage.vert.spv || goto :shader_error

echo Compiling kinematic foliage vertex shader...
%GLSLANG% -V -I. shaders\foliage_kinematic.vert -o shaders\foliage_kinematic.vert.spv || goto :shader_error

echo Compiling foliage fragment shader...
%GLSLANG% -V -I. shaders\foliage.frag -o shaders\foliage.frag.spv || goto :shader_error
echo Compiling foliage shadow vertex shader...
%GLSLANG% -V -I. shaders\foliage_shadow.vert -o shaders\foliage_shadow.vert.spv || goto :shader_error

echo Compiling foliage shadow fragment shader...
%GLSLANG% -V -I. shaders\foliage_shadow.frag -o shaders\foliage_shadow.frag.spv || goto :shader_error

echo Compiling far terrain vertex shader...
%GLSLANG% -V -I. shaders\far_terrain.vert -o shaders\far_terrain.vert.spv || goto :shader_error

echo Compiling far terrain fragment shader...
%GLSLANG% -V -I. shaders\far_terrain.frag -o shaders\far_terrain.frag.spv || goto :shader_error

echo Compiling far tree vertex shader...
%GLSLANG% -V -I. shaders\far_tree.vert -o shaders\far_tree.vert.spv || goto :shader_error

echo Compiling far tree fragment shader...
%GLSLANG% -V -I. shaders\far_tree.frag -o shaders\far_tree.frag.spv || goto :shader_error

echo Compiling far tree mesh vertex shader...
%GLSLANG% -V -I. shaders\far_tree_mesh.vert -o shaders\far_tree_mesh.vert.spv || goto :shader_error

echo Compiling far tree mesh fragment shader...
%GLSLANG% -V -I. shaders\far_tree_mesh.frag -o shaders\far_tree_mesh.frag.spv || goto :shader_error

echo Compiling far terrain shadow vertex shader...
%GLSLANG% -V -I. shaders\far_terrain_shadow.vert -o shaders\far_terrain_shadow.vert.spv || goto :shader_error

echo Compiling far tree mesh shadow vertex shader...
%GLSLANG% -V -I. shaders\far_tree_mesh_shadow.vert -o shaders\far_tree_mesh_shadow.vert.spv || goto :shader_error

REM Record the source hashes these .spv were built from, so a later edit to a shared include
REM (lighting.glsl is #included by eleven shaders) cannot silently leave them stale. See
REM tools/shader_manifest.py -- this is the guard for the bug that shipped a pink world.
python "%~dp0tools\shader_manifest.py" --update
if %errorlevel% neq 0 echo WARNING: could not update shaders\shader_manifest.json ^(is python on PATH?^)

REM Reached only if EVERY compile above returned 0 -- each failure exits before here, so the
REM manifest above is never recorded over a failed build. That is what keeps
REM `shader_manifest.py --check` honest: a failed build leaves the OLD manifest against NEW
REM sources, so --check reddens by itself. No separate guard needed.
echo All shaders compiled successfully!
exit /b 0

:shader_error
REM TOP-LEVEL on purpose. `exit /b 1` inside a `||` block nested inside the `if defined
REM USE_GLSLC ( ... )` block halts the script but returns exit code 0 -- verified with a
REM minimal repro. Jumping out to a top-level label is what makes the failure visible to
REM callers. glslc has already printed the real error above; this is the exit status.
echo.
echo BUILD FAILED: a shader did not compile. The .spv on disk is STALE -- do not commit.
if not defined SHADERS_NONINTERACTIVE pause
exit /b 1
REM pause
