# P3d survey - the column water, every creator and reader (2026-10-17, WaterCore.md 26.24)

Line numbers as of commit 76e1a17f.

## 1. Created / destroyed
- `WaterCoreManager.cpp:997-1009` createColumns (limit 200x64x200 voxels; bed defaults to Generator); `:1011-1014` destroyColumns.
- `WaterCoreManager.cpp:1104-1170` openWindow (CPU 1/9 m av, `av.window`, autoSleep off, pond friction 0 + setBedManning,
  floorY from the column beds, seed, setHeld); `:1172-1181` closeWindow; `:1025` hold dropped if the window av is destroyed.
- `Application.cpp:14224-14324` the `water_columns` handler (create, destroy, plane, bed, clear_flows, manning_n, spring_rate,
  probe, profile, spring, outfall, window, window_close, list / wet_list) - the ONLY callers of createColumns / openWindow
  outside tests. `EngineAPIServer.cpp:2208` the route.
- `Application.cpp:460-466` setGeneratorBedQuery - wired at boot; its only user is refreshColumnSolids.
- Checked, creates nothing: WorldGenerator (creek swale / carve shape terrain only), `gencreek` (a clip name in
  water_creek_live.py), game.json (description text only), the MCP server (no water_columns / water_av_* tool).
- A normal session: zero regions.

## 2. Per-frame readers
- `WaterCoreManager.cpp:284` stepColumnSplashes (741-781): the SC cosmetic crown, droplets with volumeId = -region id; knob
  m_creekSplash (h:470), kSplashMinSpeed (h:276).
- `:287` stepColumns (1021-1030); `:944-995` refreshColumnSolids; `:257` markSolidsDirty -> region occRevision.
- `:677-731` (in setMovingSolids) SC load per column; kLoadReleaseSeconds h:271.
- Render: `:933-939` surfaceFields appends region fields AFTER the volumes'; `Application.cpp:3757` the render gate's
  `|| !columnRegions().empty()`.
- Ripples: `:1504-1601` RC forcing on region layers; `:508` liveRippleLayers; `:1376` rippleLayers appends region layers.
- Droplets: `:1300-1317` landing for negative volumeId; `:1347-1356` cosmetic landing = ripple impulse only; h:328 cosmetic
  stats; h:336 dropletVolume excludes cosmetic; `Application.cpp:14367,14373` creek_splash + stats.
- Debris: `Application.cpp:3720-3721` columnBoxes() into setWaterVolumeBoxes; `:561` columnWater() reads m_fields (region
  fields too) but the debris tile gate passes volumeBoxes() only (DebrisRuntime.cpp:195-203).
- Other m_fields readers: surfaceAtWorld (1631), columnSurfaceStats (1671), water_av_column (Application.cpp:14145-14164).
- Save / load, nav, NPCs: none.

## 3. Volumes <-> regions
- Windows are av volumes (`Av::window` h:436, written at 1129, never read; `Av::floorY` h:437, read in sampleOccupancy
  154-156, set only by openWindow).
- windowGhosts 1039-1058, windowTransfers 1060-1102; ColumnWater setHeld / setGhost / transfers (ColumnWater.cpp:169-219);
  ShoreSolver::setHeld (ShoreSolver.h:77-80); windowInfo 1183-1196; windowSolverForTest h:264-268.
- SHARED, kept: rippleWindow (1380-1396) + kColumnRippleWindow 384 (h:291); setBedManning; flow forcing on volumes
  (1464-1497, m_flowForcing); setViewPoint.
- totalMass / totalCells count volumes only; regions are not in WaterBodyTable.

## 4. Tests and tools
- tests/core/WaterColumnTest.cpp (11 tests, 3 disabled) - all regions/windows EXCEPT EveryVolumeGetsTheRippleWindow (volumes).
- tests/core/ShoreSolverTest.cpp (7) - solver tests; APondSpillsWhenItTopsItsCrest uses ColumnWater.
- tests/core/WaterCreekH0Test.cpp (6) - ShoreSolver on a creek; WaterSteppedBedTest DISABLED_DiagColumn - ShoreSolver.
- tests/core/ShoreBandTest.cpp (10) - the sea surf band on ShoreSolver.
- Tools calling water_columns: water_creek_bench (9), water_dam_pond (9), water_creek_rig (9), water_creek_live (7),
  water_creek_current (5), water_creek_splash (5), water_creek_profile (2), water_creek_ripples (1). Column-only (no pond
  mode): current, splash, ripples.

## 5. The old CA water
- WaterManager on in every session (Application.cpp:432-435, follows the camera 3703-3710; config 6830-6960; the hydrology
  table 6854); debris falls back to it (DebrisRuntime.cpp:207). Regions never read it. WaterBench_Creek turns it off.

## 6. Size
- ColumnWater.h 139, ColumnWater.cpp 311; ShoreSolver.h 134 / .cpp 387 (stays: ShoreBand); ShoreBand.h 134 / .cpp 386.
- Column + window code in WaterCoreManager.cpp ~430 lines (sites above); header 16, 247-298, 436-437, 466-470, 498-504.
- ColumnWater.h included by WaterCoreManager.h:16 (and so transitively everywhere); WaterCoreManager.h includes
  WaterSurfaceMesh.h itself.
