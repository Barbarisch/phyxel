# Pathing Features Session Handoff
Date: May 9, 2026

## What Was Done

A full 6-phase NPC pathing improvement was implemented over this session. All phases are complete and the engine builds cleanly. A visual live-demo script was created to test the features in-engine.

---

## Implementation Status

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | Forward terrain probe (`validateAheadOfPath`) | ✅ Done |
| 2 | Immediate path invalidation on voxel change | ✅ Done |
| 3 | NavGrid jump links + A* expansion | ✅ Done (+ bug fix applied, see below) |
| 4 | Debris blocking placeholder | ✅ Done (TODO stub in NavCell) |
| 5 | Steering: smooth yaw lerp, decel, NPC separation | ✅ Done |
| 6 | World Event Bus seam (TODO comment in NPCManager) | ✅ Done |

---

## Modified Files

### `engine/include/core/NavGrid.h`
- Added `NavLinkType { Jump, Climb }` enum
- Added `NavLink { ivec2 start, end; NavLinkType type; float cost; }` struct
- Added `m_links: unordered_map<int64_t, vector<NavLink>>` private member
- New methods: `getLinksAt`, `addLink`, `removeLinksAt`, `buildJumpLinks`, `buildJumpLinksNear`, `linkCount`

### `engine/src/core/NavGrid.cpp`
- `buildFromRegion()` calls `buildJumpLinks()` at end
- `rebuildCell()` calls `removeLinksAt(x,z)` then `buildJumpLinksNear(x,z,3)` at end
- `buildJumpLinksNear()`: scans 4 cardinal dirs for pattern walkable→gap→walkable within 2-block height diff, creates NavLink
- `removeLinksAt()`: removes start-keyed links and cleans up reverse endpoint references
- `checkPathIntersectsCell()` implemented

### `engine/include/core/AStarPathfinder.h`
- Added `enum class WaypointType { Normal, LinkJump }`
- Added `PathResult.waypointTypes: vector<WaypointType>` (parallel to waypoints)
- `smoothPath()` updated signature to take both waypoints and types

### `engine/src/core/AStarPathfinder.cpp`
- Added `reachedViaLink` map alongside gScore/cameFrom
- **BUG FIX (last change this session):** The original link expansion only checked `getLinksAt(current.cell)`. But `getNeighbors()` skips `nearWall` cells, meaning the gap-edge cells (e.g. X=72 right before a gap at X=73) are never visited by A*, so their jump links are never discovered.
  - **Fix:** After regular neighbor expansion, also probe 4 cardinal `nearWall` neighbors of the current cell as potential link takeoff points. Each is visited with `costToSrc = COST_ORTHOGONAL` added, and the edge cell is inserted into `cameFrom` as an intermediate waypoint so the NPC physically walks to the gap edge before jumping.
  - File: `engine/src/core/AStarPathfinder.cpp`, the block labeled `// Phase 3 — expand navigation links`
- Path reconstruction tags each step `LinkJump` or `Normal` via `reachedViaLink`
- `smoothPath()` preserves `LinkJump` waypoints (never pruned during collinearity pass)

### `engine/include/scene/Entity.h`
- Added `virtual void jump() {}` (no-op default)

### `engine/include/scene/AnimatedVoxelCharacter.h`
- `jump()` marked `override`

### `engine/include/scene/behaviors/PatrolBehavior.h`
- `#include "core/AStarPathfinder.h"` added
- `m_pathNodeTypes: vector<WaypointType>` — parallel to m_pathNodes
- `m_linkJumpTriggered: bool` — prevents calling jump() multiple times per link waypoint
- `m_probeFrameCounter`, `PROBE_INTERVAL=5`, `PROBE_LOOKAHEAD=3`
- `m_currentYaw`, `TURN_SPEED=8.0f`, `DECEL_RADIUS=1.5f`
- New public: `getPathNodes() const` — used by NPCManager Phase 2 and tests
- New public: `invalidatePath()` — clears path + types + linkJumpTriggered
- New private: `validateAheadOfPath()` declaration

### `engine/src/scene/behaviors/PatrolBehavior.cpp`
- Terrain probe: `if (++m_probeFrameCounter >= PROBE_INTERVAL) validateAheadOfPath()` in `update()`
- `validateAheadOfPath()`: checks next PROBE_LOOKAHEAD nodes, invalidates if `!walkable` or surfaceY drift > 1
- Jump link execution: when current node is `LinkJump` and `!m_linkJumpTriggered`, calls `ctx.self->jump()`
- Arrival decel: `effectiveSpeed = walkSpeed * clamp(distXZ/DECEL_RADIUS, 0.3, 1.0)`
- NPC separation: queries `entityRegistry->getEntitiesNear(pos, 1.5f)`, repulsion up to 30% of direction
- Smooth yaw: lerps `m_currentYaw` toward `atan2(dir.x, dir.z)` at `TURN_SPEED * dt`, normalizes to [-π,π]
- `computePath()` stores `m_pathNodeTypes = result.waypointTypes`

### `engine/src/core/NPCManager.cpp`
- `onVoxelChanged()`: after `rebuildCell(wx,wz)`, iterates all NPCs, casts to PatrolBehavior, checks if any `getPathNodes()` waypoint is within ±1 of changed cell → `patrol->invalidatePath()`
- `onRegionChanged()`: same but checks against full region bounds ±1
- Phase 6 TODO comment: `// Phase 6 TODO: post WorldEvent::TerrainChanged(worldPos) to WorldEventBus here`

---

## Demo Script: `scripts/pathing_features_demo.py`

Live visual test — run while the engine is open, no screenshots, self-cleaning.

```
python scripts/pathing_features_demo.py
```

### Test 1 — Jump Gap (Phase 3)
- Builds a solid stone floor at Y=60 from X=60..86, Z=60..70
- Carves the gap column at X=73 (triggering `rebuildCell` → jump link generation)
- Spawns `JumpTester` at X=65, patrol waypoints X=65 ↔ X=81
- Tracks for 50 s, PASS if NPC crosses X=73.5

### Test 2 — Terrain Probe + Invalidation (Phases 1+2)
- Builds a solid stone corridor X=60..95, Z=80..90, Y=60
- Spawns `ProbeTester` at X=63, patrol to X=92
- After 8 s, removes the floor at X=78..82 (5-cell pit, not jumpable)
- Tracks for 15 s, PASS if NPC stops before X=79.5 without crossing the pit
- **Already PASSED in the last run**

### Demo Script Design Notes
- All API calls wait for `async_id` completion before continuing (fill/clear are async)
- `EngineOfflineError` is raised on `ConnectionRefusedError` — caught at top level with a "restart engine" message
- `cleanup_t1()` / `cleanup_t2()` are called at test start (clears leftover geometry from crashed runs) AND at test end. `try/finally` ensures cleanup even on Ctrl+C or unhandled exception.
- Cleanup functions silently ignore `EngineOfflineError` so Ctrl+C always exits cleanly

---

## Current Blocker

**Test 1 (Jump Gap) has not yet been verified with the new binary.**

The A* bug fix was compiled successfully on May 9 but the engine crashed mid-test before the fix could be observed. The engine needs to be restarted to load the new `phyxel.exe`, then the demo re-run.

### To verify next session:
1. Launch the engine: `.\build\editor\Debug\phyxel.exe`
2. Run the demo: `python scripts/pathing_features_demo.py`
3. Expected: Test 1 PASS (NPC crosses the gap), Test 2 PASS (NPC stops at pit)

---

## Build Command

```powershell
$env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
cmake --build build --config Debug --target phyxel_core phyxel
```

Last build: clean (no errors, two harmless warnings: APIENTRY macro redefinition, LNK4098 MSVCRT conflict — both pre-existing).

---

## Pre-existing Test Failures (NOT regressions)

When running `phyxel_tests.exe`, two failures are expected and unrelated to this work:
- `AStarPathfinderTest.PathUpOneBlockStep`
- `NavGridTest.StepUpOneBlock`

Both test `MAX_STEP_UP=1` behavior but the engine intentionally uses `MAX_STEP_UP=0`.

---

## Pending: Unit Test File

`tests/core/PathingFeaturesTest.cpp` was designed but **not yet created** (tool limitations prevented file creation in the previous session). Content is in the conversation summary if needed. The GTest CMakeLists uses `file(GLOB_RECURSE)` on `tests/core/*.cpp` so any file placed there is auto-included — no CMakeLists edit needed.

The test covers:
- `JumpLinkTest.*` — NavGrid link generation, solid floor no-link, height limit, dynamic add/remove
- `JumpLinkPathfindingTest.*` — A* finds path across gap, LinkJump tagging, normal path has no tags, wide gap blocks
- `TerrainProbeTest.*` — probe fires within PROBE_INTERVAL frames, stable terrain doesn't invalidate
- `PathInvalidationTest.*` — Phase 2 NPCManager logic (waypoint proximity check)
- `JumpBehaviorTest.*` — PatrolBehavior calls `jump()` at LinkJump waypoints
