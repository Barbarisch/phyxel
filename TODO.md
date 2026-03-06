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

- [ ] **19. WorldStorage has only 1 test (initialize returns true)**
  - 926-line class with no save/load round-trip tests.
  - File: `tests/core/WorldStorageTest.cpp`
  - Fix: Add round-trip persistence tests, corruption handling, subcube/microcube storage.

- [ ] **20. ChunkManager has zero unit tests**
  - 1,200+ line orchestrator with no direct unit tests. Integration tests don't assert on face culling correctness.
  - Fix: Add unit tests with MockChunkManager or test chunks in isolation.

- [ ] **21. E2E tests are ~80% stubs**
  - Tests call `app.initialize()`, sleep, then `app.cleanup()` with no assertions.
  - Files: `tests/e2e/ApplicationE2ETests.cpp`, `tests/e2e/WorldInteractionE2ETests.cpp`
  - Fix: Implement actual assertions or remove placeholder tests.

- [x] **22. ~60% of benchmarks commented out**
  - Due to `std::function` callback issues (related to issue #6).
  - File: `tests/benchmark/ChunkBenchmarks.cpp`
  - **Fixed**: Uncommented all 6 previously disabled benchmarks (ChunkManagerCreation, AddCube, RemoveCube, GetCubeAt, MultiChunkFaceCulling, VoxelMapInitialization). Fixed `addCube` call to match current single-arg API.

- [ ] **23. Sanitizers (ASan/TSan) OFF by default**
  - File: `cmake/Sanitizers.cmake`
  - Fix: Enable at least ASan for Debug builds by default, or add a CI preset that does.

- [ ] **24. Zero tests for ChunkStreamingManager, ForceSystem, DebrisSystem, ScriptingSystem, CollisionSpatialGrid**
  - Core gameplay and infrastructure systems with no test coverage.
  - Fix: Prioritize ForceSystem and ChunkStreamingManager tests.
