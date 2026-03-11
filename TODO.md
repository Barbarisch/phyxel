# Phyxel - Issue Tracker

## Critical: Crash-Level Bugs

- [x] **1. PhysicsWorld::cleanup() use-after-free / null dereference**
  - `dynamicsWorld.reset()` was called before character cleanup, so `dynamicsWorld->removeAction(character)` dereferenced a null pointer.
  - File: `src/physics/PhysicsWorld.cpp` cleanup() method
  - **Fixed**: Moved character/ghost/shape cleanup before `dynamicsWorld.reset()`.

- [x] **2. DynamicObjectManager::clearAllGlobalDynamicCubes() leaks physics bodies**
  - Called `cubes.clear()` without removing rigid bodies from the Bullet world first.
  - File: `src/core/DynamicObjectManager.cpp`
  - **Fixed**: Added physics body removal loop matching the subcube/microcube versions.

- [x] **3. Application::cleanup() destroys physics before entities**
  - `entities` vector was never cleared before `physicsWorld->cleanup()`.
  - File: `src/Application.cpp` cleanup() method
  - **Fixed**: Added `entities.clear()` + null out cached pointers before physics cleanup. Also reset `audioSystem`.

## High: Code Hazards

- [x] **4. Compatibility macros in Chunk.cpp**
  - `#define physicsWorld`, `#define chunkPhysicsBody`, etc. poisoned the entire translation unit.
  - File: `src/core/Chunk.cpp` lines 23-28
  - **Fixed**: Replaced all 6 macros with direct `physicsManager.getXxxRef()` calls. Removed the `#undef`/`#define` hack in `breakSubcube()`. Also fixed duplicate `#include <iostream>`.

- [x] **5. MSVC_RUNTIME_LIBRARY double-Debug**
  - `"MultiThreadedDebug$<$<CONFIG:Debug>:Debug>"` evaluated to `MultiThreadedDebugDebug` in Debug.
  - Files: `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/integration/CMakeLists.txt`, `tests/e2e/CMakeLists.txt`
  - **Fixed**: Changed to `"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"` to match Bullet/GLFW DLL runtime. Requires CMake regeneration.

- [x] **6. ChunkVoxelManager callback explosion**
  - Every public method took 6-9 `std::function` callback parameters. Refactored to use `setCallbacks()` pattern (matching ChunkVoxelBreaker).
  - Files: `include/core/ChunkVoxelManager.h`, `src/core/ChunkVoxelManager.cpp`, `src/core/Chunk.cpp`
  - **Fixed**: Callbacks stored as members via `setCallbacks()`. All 15 call sites in Chunk.cpp simplified from 6-9 lambda args to just positional params. ~130 lines of boilerplate removed.

## Medium: Performance Hot-Path Issues

- [x] **7. DynamicObjectManager rebuilds GPU faces every frame**
  - `updateGlobalDynamic*Positions()` called `m_rebuildFaces()` every frame when any transform changes (always true for moving objects).
  - Three separate calls per frame (subcubes, cubes, microcubes) each triggered a full face regeneration.
  - File: `src/core/DynamicObjectManager.cpp`
  - **Fixed**: (a) Added movement threshold (`MOVEMENT_THRESHOLD_SQ = 1e-6`) — transforms only update when position changes by >0.001 units. (b) Replaced per-type `m_rebuildFaces()` with a dirty flag (`m_positionsDirty`). (c) Batched all three position updates into a single `updateAllDynamicObjectPositions()` call. (d) Added 30 Hz throttle (`MIN_REBUILD_INTERVAL`) — position-driven rebuilds capped at 30/sec instead of 60+. Add/remove operations still rebuild immediately.

- [x] **8. RenderCoordinator per-frame heap allocation & redundant work**
  - `std::vector<size_t>` allocated on heap every frame in `renderStaticGeometry()`.
  - Frustum re-extracted from VP matrix per chunk instead of once per frame.
  - `getPerformanceStats()` called 3 times per frame.
  - Files: `include/graphics/RenderCoordinator.h`, `src/graphics/RenderCoordinator.cpp`
  - **Fixed**: Preallocated visible chunk vector as class member; hoisted frustum computation out of per-chunk loop; deduplicated `getPerformanceStats()` to single call. Also removed duplicate `#include "PostProcessor.h"`.

- [x] **9. ChunkVoxelManager removeMicrocube() O(27) nested loop**
  - Triple-nested 3x3x3 loop calling `getMicrocubesHelper()` per iteration. `microcubeMap[cubePos].empty()` would suffice.
  - File: `src/core/ChunkVoxelManager.cpp`
  - **Fixed**: Replaced both O(27) loops (in `removeMicrocube()` and `clearMicrocubesAt()`) with O(1) `microcubeMap.count(cubePos) > 0` — safe because `removeMicrocubeFromMaps()` already cleans up empty inner maps.

- [x] **10. DynamicObjectManager enforceObjectLimits() is O(n²)**
  - `cubes.erase(cubes.begin())` in a loop — front-erasure on a vector.
  - File: `src/core/DynamicObjectManager.cpp`
  - **Fixed**: Changed to bulk erase with single `cubes.erase(begin, begin + removeCount)`.

## Low: Code Quality & Cleanup

- [x] **11. Raw `new` for all voxels (Cube/Subcube/Microcube)**
  - No RAII wrappers; manual `delete` loops in destructors. Exception-unsafe.
  - Files: `src/core/ChunkVoxelManager.cpp`, `src/core/Chunk.cpp`
  - **Fixed**: Migrated all three voxel vector types (`cubes`, `staticSubcubes`, `staticMicrocubes`) from raw pointers to `std::unique_ptr` across 12 files. Replaced ~15 `new` sites with `std::make_unique`, removed ~20 manual `delete` calls (auto-cleanup via `unique_ptr::reset()` / vector `erase()`). Hash maps remain non-owning raw pointers (correct: aliases into owning vectors). Also fixed a pre-existing memory leak in `~Chunk()` where `staticMicrocubes` were never deleted.
  - Files changed: `Chunk.h`, `Chunk.cpp`, `ChunkVoxelManager.h`, `ChunkVoxelManager.cpp`, `ChunkVoxelBreaker.h`, `ChunkVoxelBreaker.cpp`, `ChunkRenderManager.h`, `ChunkRenderManager.cpp`, `ChunkPhysicsManager.h`, `ChunkPhysicsManager.cpp`, `ChunkStorage.h`, `ChunkStorage.cpp`, `WorldStorage.cpp`

- [x] **12. `createBreakawaCube` typo**
  - Missing 'y' in "breakaway" — persisted across header and implementation.
  - Files: `include/physics/PhysicsWorld.h`, `src/physics/PhysicsWorld.cpp`, plus 4 callers
  - **Fixed**: Renamed to `createBreakawayCube` across all 6 files (15 occurrences).

- [x] **13. Four near-identical cube creation methods in PhysicsWorld**
  - `createCube(pos, size, mass)`, `createCube(pos, size, materialName)`, `createBreakawayCube(...)` x2 share ~80% code.
  - Files: `include/physics/PhysicsWorld.h`, `src/physics/PhysicsWorld.cpp`
  - **Fixed**: Added private `CubeCreationParams` struct + `createCubeInternal()` helper. All four public methods now thin wrappers (~10 lines each) that fill params and delegate. ~200 lines of duplication removed.

- [x] **14. `void*` in DynamicObjectManager::derezCharacter()**
  - C-style type erasure to avoid circular deps. A forward declaration or interface would preserve type safety.
  - File: `include/core/DynamicObjectManager.h`
  - **Fixed**: Added forward declaration `namespace Scene { class AnimatedVoxelCharacter; }` in header. Changed signature from `void*` to `Scene::AnimatedVoxelCharacter*`. Removed `static_cast` in implementation.

- [x] **15. `resetMouseDelta()` called twice per frame**
  - Called at both top and bottom of the game loop in Application::run().
  - File: `src/Application.cpp`
  - **Fixed**: Removed the duplicate call at the end of the frame.

- [x] **16. `camera->setMode(Free)` called twice during init**
  - Lines 209 and 218 of Application.cpp.
  - File: `src/Application.cpp`
  - **Fixed**: Removed the duplicate call.

- [x] **17. Rename `VulkanCube` namespace and references to `Phyxel`**
  - Renamed `VulkanCube` namespace to `Phyxel` across all 180+ source files (~398 occurrences).
  - Updated README.md, docs, build_and_test.ps1, examples.
  - Fixed exe references to lowercase `phyxel.exe`, build script target to lowercase `phyxel`.
  - Vulkan `pApplicationName` set to `"Phyxel"`.
  - **Fixed**: Global find-replace across engine/, game/, tests/, examples/, docs/.

- [x] **18. Duplicate `#include <iostream>` in Chunk.cpp**
  - File: `src/core/Chunk.cpp` lines 8 and 10
  - **Fixed**: Removed the duplicate (done as part of issue #4).

## Testing Gaps

- [x] **19. WorldStorage has only 1 test (initialize returns true)**
  - 926-line class with no save/load round-trip tests.
  - File: `tests/core/WorldStorageTest.cpp`
  - **Fixed**: Added 21 tests covering: single/multi-cube round-trip, all 9 materials, multi-chunk persistence, negative coords, boundary positions, close/reopen persistence, deleteChunk, resave overwrite, createNewWorld, statistics, compactDatabase. Also fixed `deleteChunk()` bug (was only deleting from `chunks` table, leaving orphaned cube rows).

- [x] **20. ChunkManager has zero unit tests**
  - 1,200+ line orchestrator with no direct unit tests. Integration tests don't assert on face culling correctness.
  - **Fixed**: Added 31 unit tests in `tests/core/ChunkManagerTest.cpp` covering: static coordinate helpers (worldToChunkCoord, worldToLocalCoord, chunkCoordToOrigin, round-trip conversions), Chunk data operations (addCube, removeCube, getCubeAt, materials, dirty tracking, boundary positions, index mapping, full chunk fill).

- [x] **21. E2E tests are ~80% stubs**
  - Tests call `app.initialize()`, sleep, then `app.cleanup()` with no assertions.
  - Files: `tests/e2e/ApplicationE2ETests.cpp`, `tests/e2e/WorldInteractionE2ETests.cpp`
  - **Fixed**: Converted 12 stub tests in WorldInteractionE2ETests to `GTEST_SKIP()` with descriptions of what each needs to become a real test. Kept ApplicationE2ETests (which have actual timing assertions). Stubs now clearly document required APIs (e.g. `Application::breakVoxelAt()`).

- [x] **22. ~60% of benchmarks commented out**
  - Due to `std::function` callback issues (related to issue #6).
  - File: `tests/benchmark/ChunkBenchmarks.cpp`
  - **Fixed**: Uncommented all 6 previously disabled benchmarks (ChunkManagerCreation, AddCube, RemoveCube, GetCubeAt, MultiChunkFaceCulling, VoxelMapInitialization). Fixed `addCube` call to match current single-arg API.

- [x] **23. Sanitizers (ASan/TSan) OFF by default**
  - File: `cmake/Sanitizers.cmake`
  - **Fixed**: ASan opt-in via `-DENABLE_ASAN=ON`. Applied globally in top-level CMakeLists.txt (before external deps) so all libraries share the same ABI. Uses generator expression for MSVC multi-config (Debug only). Requires clean build directory when toggling.

- [x] **24. Zero tests for ChunkStreamingManager, ForceSystem, DebrisSystem, ScriptingSystem, CollisionSpatialGrid**
  - Core gameplay and infrastructure systems with no test coverage.
  - **Fixed**: Added 36 ForceSystem tests, 15 ChunkStreamingManager tests, 18 DebrisSystem tests, 28 CollisionSpatialGrid tests. ScriptingSystem skipped (requires Python/pybind11 runtime). Total: 412 tests passing.

## Feature & Polish (Phase 3)

- [x] **25. Subcube/microcube per-material textures**
  - Subcubes and microcubes used placeholder/default textures regardless of parent material.
  - **Fixed**: Added `materialName` field to Subcube and Microcube. Material inherited on subdivision, passed through from templates. Rendering uses `getTextureIndexForMaterial()`.

- [x] **26. Procedural texture visual improvements**
  - Generated textures were too simple (just noise/smooth).
  - **Fixed**: Added 7 new texture algorithms (stratified, growth rings, brushed metal, glass edges, dimples, cracked ice, porous). All materials updated with characteristic patterns.

## Upcoming Work

- [ ] **27. Subcube/microcube material database persistence**
  - SQLite schema only stores material for full cubes. Subcube/microcube materials default to "Default" on save/load.
  - Need to add `material` column to subcube/microcube tables in WorldStorage.

- [ ] **28. Chunk LOD / distance rendering**
  - Currently all loaded chunks render at full detail. Distant chunks could use lower-resolution representations.

- [ ] **29. Entity AI behavior scripting**
  - Spider and animated characters have basic state machines but no scriptable AI behaviors.

- [ ] **30. Audio system MCP integration**
  - Audio system exists but has no MCP tools for triggering sounds or music from external agents.

- [ ] **31. Lighting control via MCP**
  - Ambient and directional lighting adjustable via keybinds but not exposed through MCP API.
