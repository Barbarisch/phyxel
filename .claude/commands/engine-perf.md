Measure the Phyxel engine's rendering performance with a clean, repeatable loop. Use this whenever evaluating an FPS/perf change.

**Drive the engine over its HTTP API on `localhost:8090`, NOT the MCP tools** (the `phyxel` MCP server is unreliable and hangs). Use `curl` / PowerShell directly.

## Steps

1. **Build if needed** (only if source changed since last build):
   ```powershell
   $env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
   cmake --build build --config Debug
   ```
   (Add `cmake -B build -S .` first only if new source files were added — CMake globs sources.)

2. **Ensure a clean single instance.** Kill any stale engine first (an orphaned instance holds port 8090 and you'll measure the wrong process):
   ```powershell
   Get-Process phyxel -ErrorAction SilentlyContinue | Stop-Process -Force
   ```

3. **Launch with the CharacterTestbed project** (clean flat world; the default world DB is stale and renders magenta):
   ```powershell
   Start-Process -FilePath "G:\Github\phyxel\build\editor\Debug\phyxel.exe" -WorkingDirectory "G:\Github\phyxel" -ArgumentList '--project','C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed'
   ```
   To test WITH validation layers: set `$env:PHYXEL_VALIDATION=1` before launching (default is off in Debug).

4. **Wait for ready, then wait for the scene to FULLY LOAD.** Poll `GET /api/status` until it returns JSON. Then poll `GET /api/debug/engine_timing` until `visibleInstances` stops climbing and stabilizes (~819200 for CharacterTestbed). **Do not measure before this** — sampling mid-stream-load reports misleadingly high FPS.

5. **Sample** `GET /api/debug/engine_timing` ~5 times, ~1s apart. Report the **median** of:
   - `fps`
   - `detailed.commandRecordTime` (CPU command recording, ms)
   - `visibleInstances`, `drawCalls`
   Note: in this JSON `gpuFrameTime` just mirrors `cpuFrameTime` — it is NOT a real GPU timer. Infer GPU-bound vs CPU-bound from `totalFrameTime` minus `commandRecordTime` (large remainder = GPU-bound / CPU waiting on GPU).

6. **For a before/after comparison**, measure baseline first, then apply the change, rebuild, relaunch, and re-measure with the scene fully loaded both times. Report both medians side by side.

## Reference
Architecture and known bottlenecks: see memory `reference-render-pipeline-internals` and `project-perf-optimization`. Visual changes to voxel rendering risk winding bugs — verify from multiple angles with `GET /api/screenshot`.
