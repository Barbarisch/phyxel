# Refactoring Progress Tracker

**Last Updated:** November 25, 2025  
**Overall Progress:** 18 of 24 modules (75% complete)  
**Phase 1 Status:** ✅ COMPLETE (WindowManager, CoordinateUtils)
**Phase 2 Status:** ✅ COMPLETE (InputManager)
**Phase 3 Status:** ✅ COMPLETE (RenderCoordinator)
**Phase 4 Status:** ✅ COMPLETE (VoxelInteractionSystem - Raycaster + Force extracted)
**Phase 5 Status:** ✅ COMPLETE (PerformanceMonitor)
**Phase 6 Status:** ✅ COMPLETE (WorldInitializer)
**Phase 7 Status:** ✅ COMPLETE (ChunkRenderManager - rendering extraction)
**Phase 8 Status:** ✅ COMPLETE (ChunkPhysicsManager - physics extraction)
**Phase 9 Status:** ✅ COMPLETE (ChunkVoxelManager - voxel management extraction)
**Phase 10 Status:** ✅ COMPLETE (VoxelRaycaster - raycasting extraction)
**Phase 11 Status:** ✅ COMPLETE (VoxelForceApplicator - force application extraction)
**Phase 12 Status:** ✅ COMPLETE (ChunkStreamingManager - chunk streaming extraction)
**Phase 13 Status:** ✅ COMPLETE (DynamicObjectManager - dynamic object lifecycle)
**Phase 14 Status:** ✅ COMPLETE (FaceUpdateCoordinator - face update coordination)
**Phase 15 Status:** ✅ COMPLETE (ChunkInitializer - chunk initialization and world generation)
**Phase 16 Status:** ✅ COMPLETE (DirtyChunkTracker - dirty chunk tracking and selective updates)
**Phase 17 Status:** ✅ COMPLETE (ChunkVoxelQuerySystem - voxel query and lookup operations)
**Phase 18 Status:** ✅ COMPLETE (ChunkVoxelModificationSystem - voxel modification operations)

**Application.cpp Status:**
- **Original:** 2,645 lines
- **Current:** 539 lines
- **Reduction:** 2,106 lines (80% smaller)

**Chunk.cpp Refactoring Status:**
- **Original:** 2,444 lines
- **After Phase 1:** 2,116 lines (-328 lines)
- **After Phase 2:** 1,611 lines (-833 cumulative)
- **After Phase 3:** 995 lines (-1,449 cumulative, -59%)
- **Remaining Phases:** Final cleanup

**VoxelInteractionSystem.cpp Refactoring Status:**
- **Original:** 1,275 lines
- **After VoxelRaycaster:** 985 lines (-290 lines, -23%)
- **After VoxelForceApplicator:** 891 lines (-384 cumulative, -30%)
- **Remaining:** Interaction logic (~800 lines)

**ChunkManager.cpp Refactoring Status:**
- **Original:** 1,414 lines
- **After ChunkStreamingManager:** 1,201 lines (-213 lines, -15%)
- **After DynamicObjectManager:** 1,086 lines (-328 cumulative, -23.2%)
- **After FaceUpdateCoordinator:** 892 lines (-522 cumulative, -36.9%)
- **After ChunkInitializer:** 802 lines (-612 cumulative, -43.3%)
- **After DirtyChunkTracker:** 783 lines (-631 cumulative, -44.6%)
- **After ChunkVoxelQuerySystem:** 680 lines (-734 cumulative, -51.9%)
- **After ChunkVoxelModificationSystem:** 659 lines (-755 cumulative, -53.4%)
- **Status:** ChunkManager refactoring in progress - 53% reduction achieved

---

## Completed Refactorings

### 1. ✅ WindowManager (November 2025)
**Branch:** `refactor/window-manager`  
**Status:** Merged/Committed  
**Time Spent:** ~2 hours  
**Lines Reduced:** ~150 lines from Application.cpp

**Files Created:**
- `include/ui/WindowManager.h` (56 lines)
- `src/ui/WindowManager.cpp` (104 lines)

**Files Modified:**
- `include/Application.h` - Added WindowManager member, removed window members
- `src/Application.cpp` - Delegated all window operations

**Key Changes:**
- Extracted GLFW window lifecycle management
- Moved resize callbacks and event polling
- Centralized window state (size, title, handle)
- Added comprehensive logging

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** `docs/QUICKSTART_WindowManager.md`

**Lessons Learned:**
- Systematic approach works well (create class → move code → update callers)
- CMake auto-detects new .cpp files with GLOB_RECURSE
- Important to verify no duplicate function declarations

---

### 2. ✅ CoordinateUtils (November 2025)
**Branch:** `refactor/coordinate-utils`  
**Status:** Committed  
**Time Spent:** ~3 hours  
**Code Centralized:** ~80 lines of coordinate math from multiple files

**Files Created:**
- `include/utils/CoordinateUtils.h` (77 lines)
- `src/utils/CoordinateUtils.cpp` (45 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Forwarding methods to CoordinateUtils
- `src/core/ChunkManager.cpp` - 15+ call sites updated
- `src/Application.cpp` - 3 call sites updated
- `src/graphics/Renderer.cpp` - 1 call site updated
- `src/core/ForceSystem.cpp` - 1 call site updated

**Functions Extracted:**
- `worldToChunkCoord()` - World → Chunk coordinate conversion
- `worldToLocalCoord()` - World → Local (0-31) conversion
- `chunkCoordToOrigin()` - Chunk → World origin
- `localToWorld()` - Chunk + Local → World (new helper)
- `isValidLocalCoord()` - Validation helper (new)

**Key Changes:**
- Centralized all coordinate conversion logic
- Proper negative coordinate handling with floor division
- Comprehensive documentation with examples
- Consistent CHUNK_SIZE constant (32)

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Inline documentation in header

**Lessons Learned:**
- Static utility classes work well for pure math functions
- Batch PowerShell replacement effective for many call sites
- Forwarding methods in ChunkManager maintain backward compatibility
- Clear examples in documentation reduce confusion

---

### 3. ✅ InputManager (November 2025)
**Branch:** `refactor/input-manager`  
**Status:** Committed  
**Time Spent:** ~6 hours  
**Lines Reduced:** ~270 lines from Application.cpp

**Files Created:**
- `include/input/InputManager.h` (142 lines)
- `src/input/InputManager.cpp` (289 lines)

**Files Modified:**
- `include/Application.h` - Removed 11 camera/mouse members, added InputManager
- `src/Application.cpp` - Removed ~270 lines of input handling code

**Functions Extracted:**
- `processInput()` - ~150 lines of keyboard handling (WASD, Space, Shift, ESC, F1-F3, T, G, C, P, O)
- `mouseCallback()` - ~58 lines of mouse movement and camera rotation
- `mouseButtonCallback()` - ~42 lines of mouse button handling
- `initializeCamera()` - ~20 lines of camera setup

**Key Features:**
- Camera state management (position, rotation, yaw/pitch)
- WASD + Space/Shift camera movement
- Right-click mouse look with sensitivity control
- Callback-based action system for keyboard shortcuts
- Mouse button actions with modifier support (Ctrl+click, etc.)
- Mouse position tracking for external systems (hover detection, velocity)

**Key Changes:**
- Centralized all input handling in dedicated manager
- Proper modifier key handling for mouse actions (fixed button+modifier collision)
- Static GLFW callbacks redirect to instance methods (same pattern as WindowManager)
- Action registration system decouples input detection from game logic
- Camera state now private with const getters for rendering

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Inline documentation in header, PLAN_InputManager.md

**Lessons Learned:**
- Map keys must account for modifier combinations (button+mods as composite key)
- Callback patterns keep Application clean while InputManager handles details
- Extra closing braces in namespaces cause mysterious compilation errors
- Testing modifier keys requires exact matching (no mods vs ctrl vs shift)

---

### 4. ✅ VoxelInteractionSystem (November 2025)
**Branch:** `refactor/voxel-interaction`  
**Status:** Committed  
**Time Spent:** ~8 hours  
**Lines Reduced:** ~600 lines from Application.cpp

**Files Created:**
- `include/scene/VoxelInteractionSystem.h` (115 lines)
- `src/scene/VoxelInteractionSystem.cpp` (658 lines)

**Functions Extracted:**
- `placeNewCube()` (~75 lines)
- `removeHoveredCube()` (~75 lines)
- `subdivideHoveredCube()` (~50 lines)
- `breakHoveredCube()` (~200 lines)
- `breakHoveredCubeWithForce()` (~75 lines)
- `breakHoveredSubcube()` (~50 lines)
- `breakCubeAtPosition()` (~75 lines)
- `updateMouseHover()` (~65 lines) - raycasting and hover detection
- `pickVoxelOptimized()` (~100 lines) - DDA raycasting
- `resolveSubcubeInVoxel()` (~50 lines)
- `voxelLocationToCubeLocation()` (~25 lines)

**Key Features:**
- Complete voxel placement, removal, subdivision, and breaking
- Mouse hover raycasting with DDA algorithm
- Physics integration for breaking cubes with force
- Subcube-level manipulation support
- Coordinate conversion helpers

**Key Changes:**
- Centralized all voxel interaction logic in Scene namespace
- Maintains state for hover detection (m_hoveredCube, m_isHoveringCube)
- Dependency injection pattern (ChunkManager, PhysicsWorld, InputManager)
- Clean separation between interaction logic and rendering

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Inline documentation in header

**Lessons Learned:**
- Large extractions benefit from maintaining clear state ownership
- Raycasting logic is complex enough to warrant its own module
- Physics integration requires careful coordinate system management
- Breaking this into smaller methods improved readability

---

### 5. ✅ PerformanceMonitor (November 2025)
**Branch:** main (direct commit)  
**Status:** Committed  
**Time Spent:** ~2 hours  
**Lines Reduced:** ~152 lines from Application.cpp

**Files Created:**
- `include/utils/PerformanceMonitor.h` (61 lines)
- `src/utils/PerformanceMonitor.cpp` (175 lines)

**Functions Extracted:**
- `updateFrameTiming()` - Update frame CPU/GPU timing
- `profileFrame()` - Capture and store frame metrics
- `printPerformanceStats()` - Basic stats output (FPS, vertices, chunks, etc.)
- `printProfilingInfo()` - Averaged stats over 60 frames with culling efficiency
- `printDetailedTimings()` - Detailed timing breakdown with percentages
- `addFrameTiming()` / `addDetailedTiming()` - Maintain rolling 60-frame sample window

**Key Features:**
- Tracks frame timing (CPU/GPU), rendering stats, and performance metrics
- Maintains rolling 60-frame window for averaged statistics
- Provides formatted output for console and ImGui overlay
- Exposes getters for external systems (ImGui renderer)

**Key Changes:**
- Moved FrameTiming and DetailedFrameTiming state to PerformanceMonitor
- Application.cpp reduced from 1,393 to 1,241 lines
- Total reduction: 53% from original 2,645 lines
- Clean separation between tracking and rendering logic

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Inline documentation in header

**Lessons Learned:**
- Quick win refactorings maintain momentum
- Getter methods enable integration with existing UI systems
- Rolling window pattern works well for performance averaging
- Batch PowerShell replacements effective but require verification

---

### 6. ✅ RenderCoordinator (November 2025)
**Branch:** main (direct commit)  
**Status:** Committed  
**Time Spent:** ~3 hours  
**Lines Reduced:** ~334 lines from Application.cpp

**Files Created:**
- `include/graphics/RenderCoordinator.h` (103 lines)
- `src/graphics/RenderCoordinator.cpp` (415 lines)

**Functions Extracted:**
- `render()` - Main render delegation
- `drawFrame()` - Frame synchronization, swapchain management, command buffer recording (~250 lines)
- `renderStaticGeometry()` - Static chunk rendering with frustum culling (~80 lines)
- `renderDynamicSubcubes()` - Dynamic subcube rendering pipeline (~40 lines)

**Key Features:**
- Complete frame rendering coordination
- Swapchain recreation and frame synchronization
- Uniform buffer management and matrix caching
- Static geometry rendering with multi-level culling (distance, frustum, occlusion)
- Dynamic subcube rendering with separate pipeline
- Performance statistics tracking integration
- Render distance management

**Key Changes:**
- Centralized all rendering operations in Graphics namespace
- Maintains state for frame index, cached matrices, render distances
- Dependency injection pattern (9 dependencies: VulkanDevice, pipelines, managers, etc.)
- Application.cpp reduced from 1,241 to 907 lines
- Total reduction: 66% from original 2,645 lines

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Inline documentation in header

**Lessons Learned:**
- Namespace organization critical (Vulkan::RenderPipeline, UI::ImGuiRenderer, root ChunkManager)
- Include paths must match actual file locations
- Forward declarations must match class namespaces exactly
- Multiple build fix iterations expected for large extractions
- Pattern: create class → extract → update Application → fix namespaces → test

---

### 7. ✅ WorldInitializer (November 2025)
**Branch:** main (direct commit)  
**Status:** Committed  
**Time Spent:** ~2.5 hours  
**Lines Reduced:** ~368 lines from Application.cpp

**Files Created:**
- `include/core/WorldInitializer.h` (105 lines)
- `src/core/WorldInitializer.cpp` (427 lines)

**Functions Extracted:**
- `initialize()` - Main orchestration of all initialization (~172 lines)
- `initializeWindow()` - Window creation and setup (~28 lines)
- `initializeVulkan()` - Complete Vulkan pipeline initialization (~176 lines)
- `initializeTextureAtlas()` - Texture loading and sampler setup (~22 lines)
- `initializeScene()` - Scene manager setup (~6 lines)
- `initializePhysics()` - Physics world initialization (~8 lines)
- `loadAssets()` - Asset loading (~6 lines)

**Key Features:**
- Centralized initialization orchestration
- Proper initialization order enforcement
- Configuration management (render distances)
- Comprehensive error handling with logging
- Chunk world generation and storage
- Input system setup with camera positioning
- Physics body creation for all chunks

**Key Changes:**
- Created WorldInitializer in Core namespace with 14 dependencies
- Removed circular dependencies (excluded VoxelInteractionSystem and RenderCoordinator)
- Fixed include paths (examples/MultiChunkDemo.h, removed non-existent headers)
- Application.cpp reduced from 907 to 539 lines
- **Total reduction: 80% from original 2,645 lines**
- RenderCoordinator configuration moved to Application after initialization

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Inline documentation in header

**Lessons Learned:**
- Circular dependencies require careful dependency ordering
- Initialization vs. runtime dependencies must be separated
- Include path validation critical (MouseVelocityTracker in ForceSystem.h, MultiChunkDemo in examples/)
- WorldInitializer should focus on initialization only, not runtime coordination
- Largest single refactoring yet - brought Application very close to target size

---

### 7. ✅ ChunkRenderManager (November 2025)
**Branch:** `refactor_gemini3`  
**Status:** Committed (Phase 1 of Chunk refactoring)  
**Time Spent:** ~4 hours  
**Lines Reduced:** ~328 lines from Chunk.cpp

**Files Created:**
- `include/graphics/ChunkRenderManager.h` (89 lines)
- `src/graphics/ChunkRenderManager.cpp` (268 lines)

**Functions Extracted:**
- Mesh generation (generateMesh, rebuildFaces, generateCubeFaces)
- Render buffer management (updateRenderBuffer, clearRenderBuffer)
- Visibility state management (setDirty, isDirty)

**Key Features:**
- Completely isolated rendering logic from Chunk
- Callback pattern for accessing cube data without circular dependencies
- O(1) cube lookups using indexed access (z + y*32 + x*32*32)
- Clean separation of concerns

**Key Changes:**
- Chunk.cpp reduced from 2,444 to 2,116 lines
- All Vulkan rendering dependencies isolated
- Maintains face culling optimization
- Zero performance regression

**Testing:** ✅ Build successful, runtime verified  
**Documentation:** Updated Chunk.h header with refactoring status

**Lessons Learned:**
- Callback pattern excellent for avoiding circular dependencies
- Must avoid O(n²) performance - always use indexed cube access
- Phase-by-phase approach maintains stability
- Clear documentation prevents future mistakes

---

### 8. ✅ ChunkPhysicsManager (November 2025)
**Branch:** `refactor_gemini3`  
**Status:** Committed (Phase 2 of Chunk refactoring)  
**Time Spent:** ~6 hours  
**Lines Reduced:** ~833 lines from Chunk.cpp (cumulative with Phase 1)

**Files Created:**
- `include/physics/ChunkPhysicsManager.h` (212 lines)
- `src/physics/ChunkPhysicsManager.cpp` (662 lines)

**Functions Extracted:**
- Physics body lifecycle (createChunkPhysicsBody, updateChunkPhysicsBody, forcePhysicsRebuild)
- Collision entity management (addCollisionEntity, removeCollisionEntities)
- Collision shape creation (createCubeCollisionShape, createSubcubeCollisionShape, createMicrocubeCollisionShape)
- Batch operations (batchUpdateCollisions, buildInitialCollisionShapes)
- Neighbor updates (updateNeighborCollisionShapes, endBulkOperation)
- Helper methods (hasExposedFaces)

**Key Features:**
- Complete isolation of Bullet Physics dependencies
- O(1) collision spatial grid for entity tracking
- Callback pattern for accessing voxel hierarchy data
- Supports cubes, subcubes, and microcubes
- Batch update optimizations

**Key Changes:**
- Chunk.cpp reduced from 2,444 to 1,611 lines (**-833 lines, -34%**)
- All Bullet Physics logic moved to dedicated manager
- 10 major physics methods extracted
- 7 callback function types for clean separation
- Compatibility macros minimal (only in one method)

**Testing:** ✅ Build successful, runtime verified, physics fully functional  
**Documentation:** Updated Chunk.h and ChunkPhysicsManager.h headers

**Lessons Learned:**
- Typedef order matters - must define before use in method signatures
- Callback pattern scales well for complex dependencies
- Incremental extraction with testing prevents regressions
- Compatibility macros can ease migration but should be minimal
- Physics synchronization critical (AABB updates, broadphase sync)

---

### 9. ✅ ChunkVoxelManager (November 2025)
**Branch:** `refactor_gemini3`  
**Status:** Committed (Phase 3 of Chunk refactoring)  
**Time Spent:** ~5 hours  
**Lines Reduced:** ~1,449 lines from Chunk.cpp (cumulative with Phases 1-2)

**Files Created:**
- `include/scene/ChunkVoxelManager.h` (239 lines)
- `src/scene/ChunkVoxelManager.cpp` (1,008 lines)

**Functions Extracted:**
- Cube operations (addCube, removeCube, setCubeColor, getCubeAtFast)
- Subcube operations (subdivideAt, addSubcube, removeSubcube, clearSubdivisionAt)
- Microcube operations (subdivideSubcubeAt, addMicrocube, removeMicrocube, clearMicrocubesAt)
- Hash map management (initializeVoxelMaps, add/remove operations for all voxel types)
- Voxel resolution (resolveLocalPosition, hasVoxelAt, getVoxelType)
- Helper methods (getCubeHelper, getSubcubesHelper, getMicrocubesHelper)

**Key Features:**
- Complete voxel hierarchy management (cubes → subcubes → microcubes)
- Four hash maps for O(1) lookups (cubeMap, subcubeMap, microcubeMap, voxelTypeMap)
- 12 callback function types for accessing Chunk data
- Subdivision logic with color inheritance
- Coordinate validation and bounds checking

**Key Changes:**
- Chunk.cpp reduced from 1,611 to 995 lines (**-616 lines in Phase 3, -1,449 cumulative, -59%**)
- All voxel lifecycle management moved to dedicated manager
- 20+ voxel methods extracted
- Hash maps migrated from Chunk to ChunkVoxelManager
- All methods delegate via lambda callbacks

**Testing:** ✅ Build successful, runtime verified, voxel operations fully functional  
**Documentation:** Updated Chunk.h, ChunkVoxelManager.h, and RefactoringProgress.md

**Lessons Learned:**
- Accessor methods (get*) must also delegate to manager, not just mutators
- Helper methods need public access when used by delegating class
- VoxelLocation structure doesn't contain cube pointer (only type/positions)
- Callback pattern maintains clean separation even with complex data access
- Iterative build/fix cycles prevent regression

---

### 10. ✅ VoxelRaycaster (November 2025)
**Branch:** main (direct commit)  
**Status:** Committed (Phase 1 of VoxelInteractionSystem refactoring)  
**Time Spent:** ~3 hours  
**Lines Reduced:** ~290 lines from VoxelInteractionSystem.cpp

**Files Created:**
- `include/scene/VoxelRaycaster.h` (118 lines)
- `src/scene/VoxelRaycaster.cpp` (309 lines)

**Functions Extracted:**
- `pickVoxel()` - DDA raycasting algorithm for voxel traversal (~125 lines)
- `resolveSubcubeInVoxel()` - Subcube and microcube intersection testing (~100 lines)
- `rayAABBIntersect()` - Ray-AABB bounding box intersection (~20 lines)
- `screenToWorldRay()` - Mouse screen coordinates to world ray conversion (~30 lines)

**Key Features:**
- DDA (Digital Differential Analyzer) algorithm for efficient voxel traversal
- O(distance) complexity instead of O(n) brute force
- Handles full voxel hierarchy (cubes → subcubes → microcubes)
- Callback pattern for ChunkManager and WindowManager access
- Optimized with early exit conditions
- Max ray distance: 200 units, max steps: 500

**Key Changes:**
- VoxelInteractionSystem.cpp reduced from 1,275 to 985 lines (**-290 lines, -23%**)
- All raycasting logic extracted to dedicated class
- 4 raycasting methods now delegate via lambda callbacks
- Pure raycasting logic with no state management
- Screen-to-world ray conversion handles Vulkan Y-axis flip

**Testing:** ✅ Build successful, runtime verified, hover detection fully functional  
**Documentation:** Updated VoxelInteractionSystem.h and VoxelRaycaster.h headers

**Lessons Learned:**
- Callback pattern continues to scale well from Chunk subsystems to scene systems
- DDA algorithm complexity requires careful extraction with context
- Screen space conversion needs proper NDC → eye space → world space transformation
- Subcube/microcube resolution logic benefits from dedicated helper method
- Clean separation enables future optimization of raycasting independently

---

### 11. ✅ VoxelForceApplicator (November 2025)
**Branch:** main (direct commit)  
**Status:** Committed (Phase 2 of VoxelInteractionSystem refactoring)  
**Time Spent:** ~2 hours  
**Lines Reduced:** ~94 lines from VoxelInteractionSystem.cpp

**Files Created:**
- `include/scene/VoxelForceApplicator.h` (100 lines)
- `src/scene/VoxelForceApplicator.cpp` (165 lines)

**Functions Extracted:**
- `breakHoveredCubeWithForce()` - Force-based breaking with mouse velocity detection (~70 lines)
- `breakCubeAtPosition()` - Direct position-based breaking with physics (~95 lines)

**Key Features:**
- Mouse velocity-based force propagation (>500 units/sec threshold)
- Integration with ForceSystem for multi-cube breaking
- Dynamic physics body creation with impulse forces
- Random force application for natural breaking effects
- Material-based physics properties (stone, wood, metal, ice)

**Key Changes:**
- VoxelInteractionSystem.cpp reduced from 985 to 891 lines (**-94 lines**)
- Cumulative reduction from original: **-384 lines (-30%)**
- Both force methods now delegate via lambda callbacks
- Callback pattern for ChunkManager, PhysicsWorld, ForceSystem, MouseVelocityTracker access
- Handles both normal breaking and force propagation modes

**Testing:** ✅ Build successful, runtime verified, force-based breaking fully functional  
**Documentation:** Updated VoxelInteractionSystem.h and VoxelForceApplicator.h headers

**Lessons Learned:**
- Force application logic cleanly separates from interaction state management
- Callback pattern scales to multiple dependencies (4 different callback types)
- MouseVelocityTracker defined in core/ForceSystem.h (not a separate header)
- Material selection algorithm simple but effective (position-based hashing)
- Small extractions (94 lines) still valuable for separation of concerns

---

### 12. ✅ ChunkStreamingManager (November 2025)
**Branch:** main (direct commit)  
**Status:** Committed (Phase 1 of ChunkManager refactoring)  
**Time Spent:** ~3 hours  
**Lines Reduced:** ~213 lines from ChunkManager.cpp

**Files Created:**
- `include/core/ChunkStreamingManager.h` (103 lines)
- `src/core/ChunkStreamingManager.cpp` (210 lines)

**Functions Extracted:**
- `updateChunkStreaming()` - Main streaming coordination with player position
- `loadChunksAroundPosition()` - Load chunks within radius (~30 lines)
- `unloadDistantChunks()` - Unload and save distant chunks (~25 lines)
- `generateOrLoadChunk()` - Generate new or load from storage (~40 lines)
- `loadChunk()` - Load single chunk from WorldStorage (~35 lines)
- `saveChunk()` / `saveAllChunks()` / `saveDirtyChunks()` - Persistence methods (~30 lines)
- `initializeWorldStorage()` - Database initialization (~15 lines)

**Key Features:**
- Dynamic chunk loading based on player proximity (160 unit load distance)
- Automatic unloading and saving of distant chunks (224 unit unload distance)
- WorldStorage integration for persistent world data
- Fallback chunk generation when storage doesn't exist
- Callback pattern for ChunkManager access (creation, chunk map, chunks vector, device handles)

**Key Changes:**
- ChunkManager.cpp reduced from 1,414 to 1,201 lines (**-213 lines, -15%**)
- WorldStorage ownership moved from ChunkManager to ChunkStreamingManager
- ChunkCoordHash moved to ChunkStreamingManager.h (shared dependency)
- All streaming methods now delegate via lambda callbacks
- CoordinateUtils::worldToChunkCoord and chunkCoordToOrigin properly namespaced

**Testing:** ✅ Build successful, runtime verified, chunk streaming fully functional  
**Documentation:** Updated ChunkManager.h and ChunkStreamingManager.h headers

**Lessons Learned:**
- Shared utility structs (ChunkCoordHash) belong in the class that needs them first
- Static utility methods require full namespace qualification (Utils::CoordinateUtils::)
- WorldStorage ownership cleanly transfers to specialized manager
- Chunk streaming is complex enough to warrant dedicated subsystem
- Callback pattern continues to work excellently for ChunkManager extractions
- Small struct definitions can cause duplicate symbol errors if in multiple headers

---

## In Progress

None currently.

---

## Planned Next Steps

### ⏳ Chunk Refactoring (Continued)

**Phase 4: Final Chunk Simplification** (Future)
- Extract voxel hierarchy operations (getCubeAt, createCube, deleteCube)
- Extract subdivision logic (subdivide, breakSubcube)
- Extract coordinate conversion helpers
- Estimated: 300-400 more lines

**Phase 4: Final Cleanup** (Future)
- Extract remaining utility methods
- Remove compatibility macros
- Final simplification
- Target: Chunk.cpp ~1,000-1,200 lines total

---

### ⏳ Phase 2: Input & Camera (Continued)

**Note:** Camera control was integrated into InputManager, so CameraController is no longer needed as a separate refactoring.

---

### Phase 3: Rendering (Future)

#### 6. RenderCoordinator
**Estimated Time:** 6 hours  
**Estimated Reduction:** ~500-600 lines

**Functions to Extract:**
- Main render loop management
- `drawFrame()` and render pass coordination
- Dynamic subcube rendering pipeline
- Command buffer management

---

### Phase 6: Initialization (Future)

#### 7. WorldInitializer
**Estimated Time:** 4 hours  
**Estimated Reduction:** ~350-400 lines

**Functions to Extract:**
- World setup and initialization
- Initial chunk generation
- Test cube placement
- Resource loading

---

## Statistics

### Time Investment
- **Phase 1:** 5 hours (WindowManager, CoordinateUtils)
- **Phase 2:** 6 hours (InputManager)
- **Phase 3:** 3 hours (RenderCoordinator)
- **Phase 4:** 8 hours (VoxelInteractionSystem)
- **Phase 5:** 2 hours (PerformanceMonitor)
- **Total Time Spent:** 24 hours
- **Remaining Estimated:** ~35+ hours
- **Efficiency:** Excellent progress with systematic approach

### Code Reduction
- **Application.cpp:** Started at ~2,645 lines
- **Current:** ~907 lines
- **Reduction:** ~1,738 lines (66%)
- **Target:** ~400 lines (56% more reduction needed)

### Module Creation
- **Completed:** 6 modules (WindowManager, CoordinateUtils, InputManager, VoxelInteractionSystem, PerformanceMonitor, RenderCoordinator)
- **Remaining:** ~18 modules
- **Progress:** 25% complete

---

### 13. ✅ DynamicObjectManager (November 2025)
**Branch:** `refactor_gemini3` (continuation)  
**Status:** COMPLETE  
**Time Spent:** ~2 hours  
**Lines Reduced:** 115 lines from ChunkManager.cpp (-9.6%)

**Files Created:**
- `include/core/DynamicObjectManager.h` (100 lines)
- `src/core/DynamicObjectManager.cpp` (320 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Added DynamicObjectManager member
- `src/core/ChunkManager.cpp` - Delegated 12 dynamic object methods

**Functions Extracted:**
- Subcube Management: add, update, updatePositions, clear (4 methods)
- Cube Management: add, update, updatePositions, clear (4 methods)  
- Microcube Management: add, update, updatePositions, clear (4 methods)
- Combined: updateAllDynamicObjects, updateAllDynamicObjectPositions (2 methods)

**Callback Pattern (5 callbacks):**
- PhysicsWorldAccessFunc - Physics world access for body removal
- DynamicSubcubeVectorAccessFunc - Subcube vector access
- DynamicCubeVectorAccessFunc - Cube vector access
- DynamicMicrocubeVectorAccessFunc - Microcube vector access
- RebuildFacesFunc - Face rebuilding coordination

**Key Changes:**
- Extracted all global dynamic object lifecycle management
- Consolidated repetitive logic across three object types
- 8-second lifetime tracking with automatic expiration
- Proper physics body cleanup on expiration/clear
- Position/rotation synchronization from Bullet physics

**Metrics:**
- ChunkManager: 1,201 → 1,086 lines (-115, -9.6%)
- DynamicObjectManager: 420 lines total (100 header + 320 impl)
- Combined Phases 12+13: -328 lines (-23.2% from original 1,414)

**Testing:** ✅ Build successful, runtime verified

**Lessons Learned:**
- Forward declarations must be in correct namespace (VulkanCube::Physics not global)
- Implementation needs full definitions (Subcube.h, Cube.h, Microcube.h, PhysicsWorld.h)
- Callback pattern scales excellently (5 dependencies handled cleanly)
- Include order critical - forward declarations can mask definition issues

---

### 14. ✅ FaceUpdateCoordinator (November 2025)
**Branch:** `refactor_gemini3` (continuation)  
**Status:** COMPLETE  
**Time Spent:** ~2 hours  
**Lines Reduced:** 194 lines from ChunkManager.cpp (-17.9%)

**Files Created:**
- `include/core/FaceUpdateCoordinator.h` (93 lines)
- `src/core/FaceUpdateCoordinator.cpp` (269 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Added FaceUpdateCoordinator member
- `src/core/ChunkManager.cpp` - Delegated 10 face update methods

**Functions Extracted:**
1. `rebuildGlobalDynamicFaces()` - Generate faces for all dynamic objects
2. `updateAfterCubeBreak()` - Handle cube removal face updates
3. `updateAfterCubePlace()` - Handle cube addition face updates
4. `updateAfterCubeSubdivision()` - Handle subdivision face updates
5. `updateAfterSubcubeBreak()` - Handle subcube breaking updates
6. `updateFacesForPositionChange()` - Dispatcher for position-based updates
7. `updateNeighborFaces()` - Update 6 neighbor faces
8. `updateSingleCubeFaces()` - Update single cube faces
9. `getAffectedNeighborPositions()` - Get 6 neighbor coordinates
10. `updateFacesAtPosition()` - Update faces at specific position

**Callback Pattern (6 callbacks):**
- DynamicSubcubeVectorAccessFunc - Access global dynamic subcubes
- DynamicCubeVectorAccessFunc - Access global dynamic cubes
- DynamicMicrocubeVectorAccessFunc - Access global dynamic microcubes
- FaceDataAccessFunc - Access global dynamic face data
- ChunkLookupFunc - Look up chunks by position
- MarkChunkDirtyFunc - Mark chunks for update

**Key Features:**
- **Global Dynamic Face Rebuilding**: Generates 6 faces for each dynamic object (subcubes, cubes, microcubes)
- **Selective Update System**: Minimal chunk updates when cubes are added/removed/subdivided/broken
- **Cross-Chunk Coordination**: Marks affected chunks dirty when changes span boundaries
- **Neighbor Management**: Updates faces of up to 6 neighbors when cube state changes
- **Scale-Aware Face Generation**: Subcube (1/3), Cube (1.0), Microcube (1/9)
- **Bit Packing**: Microcube position packed into 12 bits for shader texture calculation
- **Physics Integration**: Dynamic objects use physics position/rotation, static use grid position

**Metrics:**
- ChunkManager: 1,086 → 892 lines (-194, -17.9%)
- FaceUpdateCoordinator: 362 lines total (93 header + 269 impl)
- Combined Phases 12+13+14: -522 lines (-36.9% from original 1,414)

**Testing:** ✅ Build successful, runtime verified

**Lessons Learned:**
- Include path correction: `core/Types.h` not `graphics/TextureConstants.h`
- TextureConstants defined in Types.h with getTextureIndexForFace() helper
- Callback pattern continues to scale well (6 dependencies managed cleanly)
- Face generation logic consolidation reduces code duplication significantly

---

### 15. ✅ ChunkInitializer (November 2025)
**Branch:** `refactor_gemini3` (continuation)  
**Status:** COMPLETE  
**Time Spent:** ~2 hours  
**Lines Reduced:** 90 lines from ChunkManager.cpp (-10.1%)

**Files Created:**
- `include/core/ChunkInitializer.h` (102 lines)
- `src/core/ChunkInitializer.cpp` (153 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Added ChunkInitializer member
- `src/core/ChunkManager.cpp` - Delegated 5 initialization methods

**Functions Extracted:**
1. `createChunks()` - Multi-chunk batch creation with cross-chunk culling
2. `createChunk()` - Single chunk creation with 26-neighbor updates
3. `initializeAllChunkVoxelMaps()` - O(1) hover detection optimization
4. `rebuildAllChunkFaces()` - Global face rebuild with 3-pass process
5. `performOcclusionCulling()` - Cross-chunk occlusion culling

**Callback Pattern (5 callbacks):**
- ChunkVectorAccessFunc - Access chunk vector
- ChunkMapAccessFunc - Access chunk spatial hash map
- DeviceAccessFunc - Get Vulkan device handles
- GetChunkAtCoordFunc - Get chunk at coordinate
- RebuildChunkWithCullingFunc - Rebuild chunk with cross-chunk culling

**Key Features:**
- **Multi-Chunk Creation**: Batch creation with two-pass cross-chunk culling
- **Single Chunk Creation**: Updates all 26 neighbors when adding chunks
- **Voxel Map Initialization**: Builds spatial hash maps for O(1) hover detection
- **Face Rebuilding**: Three-pass process (bulk ops → rebuild → culling → buffers)
- **Occlusion Culling**: Cross-chunk face visibility optimization

**Metrics:**
- ChunkManager: 892 → 802 lines (-90, -10.1%)
- ChunkInitializer: 255 lines total (102 header + 153 impl)
- Combined Phases 12+13+14+15: -612 lines (-43.3% from original 1,414)

**Testing:** ✅ Build successful, runtime verified

**Lessons Learned:**
- Forward declare ChunkCoordHash from ChunkStreamingManager.h to avoid redefinition
- Include ChunkStreamingManager.h in implementation for full definition
- Duplicate namespace closings cause cascading syntax errors
- ChunkMap type alias requires ChunkCoordHash definition at use site

---

### 16. ✅ DirtyChunkTracker (November 2025)
**Branch:** `refactor_gemini3` (continuation)  
**Status:** COMPLETE  
**Time Spent:** ~1.5 hours  
**Lines Reduced:** 19 lines from ChunkManager.cpp (-2.4%)

**Files Created:**
- `include/core/DirtyChunkTracker.h` (88 lines)
- `src/core/DirtyChunkTracker.cpp` (57 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Added DirtyChunkTracker member, removed old member variables
- `src/core/ChunkManager.cpp` - Delegated 4 dirty tracking methods + updated updateAllChunks()

**Functions Extracted:**
1. `markChunkDirty(size_t)` - Mark chunk by index for GPU buffer update
2. `markChunkDirty(Chunk*)` - Mark chunk by pointer (converts to index)
3. `updateDirtyChunks()` - Batch update all dirty chunks, clear list
4. `clearDirtyChunkList()` - Reset tracking state

**Callback Pattern (3 callbacks):**
- ChunkVectorAccessFunc - Access chunk vector
- UpdateChunkFunc - Update single chunk's GPU buffers
- GetChunkIndexFunc - Convert chunk pointer to index

**Key Features:**
- **Selective Updates**: Only update chunks marked as dirty
- **Duplicate Prevention**: std::find check before adding to dirty list
- **Batch Processing**: Update all dirty chunks at once
- **Early Exit Optimization**: Skip processing if no dirty chunks
- **Efficient Tracking**: Quick hasDirty() check avoids vector operations

**Metrics:**
- ChunkManager: 802 → 783 lines (-19, -2.4%)
- ChunkManager.h: 265 → 261 lines (-4, removed old member variables)
- DirtyChunkTracker: 145 lines total (88 header + 57 impl)
- Combined Phases 12-16: -631 lines (-44.6% from original 1,414)

**Testing:** ✅ Build successful, runtime verified

**Lessons Learned:**
- Small focused extractions can still provide value (cleanup + maintainability)
- Callback pattern scales well even for simple state access
- Deprecated methods (updateAllChunks) should be updated to use new abstractions
- Member variable removal reduces ChunkManager interface complexity

---

### 17. ✅ ChunkVoxelQuerySystem (November 2025)
**Branch:** `refactor_gemini3` (continuation)  
**Status:** COMPLETE  
**Time Spent:** ~1.5 hours  
**Lines Reduced:** 103 lines from ChunkManager.cpp (-13.2%)

**Files Created:**
- `include/core/ChunkVoxelQuerySystem.h` (108 lines)
- `src/core/ChunkVoxelQuerySystem.cpp` (147 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Added ChunkVoxelQuerySystem member, forward declared ChunkCoordHash
- `src/core/ChunkManager.cpp` - Delegated 12 query methods

**Functions Extracted:**
1. `getChunkAtCoord()` - O(1) chunk lookup by coordinate (const and non-const)
2. `getChunkAtFast()` - Chunk lookup by world position
3. `getCubeAtFast()` - O(1) cube lookup by world position
4. `getChunkAt()` / `getCubeAt()` - Legacy compatibility methods
5. `resolveGlobalPosition()` - O(1) voxel location resolution
6. `resolveGlobalPositionWithSubcube()` - Subcube validation
7. `hasVoxelAt()` - Check if voxel exists at position
8. `getVoxelTypeAt()` - Get voxel type (EMPTY/CUBE/SUBDIVIDED)
9. `getSubcubeAt()` - Get subcube at position
10. `getSubcubeColor()` - Get subcube color

**Callback Pattern (2 callbacks):**
- ChunkMapAccessFunc - Access chunk spatial hash map
- ChunkVectorAccessFunc - Access chunk vector

**Key Features:**
- **Read-Only Operations**: Pure query system, no modifications
- **O(1) Lookups**: Spatial hash map for chunk access
- **Coordinate Conversion**: Automatic world → chunk → local conversion
- **Hover Detection**: VoxelLocation resolution for mouse picking
- **Legacy Support**: Backward compatibility with old method names

**Metrics:**
- ChunkManager: 783 → 680 lines (-103, -13.2%)
- ChunkVoxelQuerySystem: 255 lines total (108 header + 147 impl)
- Combined Phases 12-17: -734 lines (-51.9% from original 1,414)

**Testing:** ✅ Build successful, runtime verified

**Lessons Learned:**
- Forward declaration of ChunkCoordHash needed to avoid circular dependency
- Separating read (query) from write (modification) operations improves design
- Query system pairs naturally with modification system (next phase)
- Const-correctness important for query methods

---

### 18. ✅ ChunkVoxelModificationSystem (November 2025)
**Branch:** `refactor_gemini3` (continuation)  
**Status:** COMPLETE  
**Time Spent:** ~1.5 hours  
**Lines Reduced:** 21 lines from ChunkManager.cpp (-3.1%)

**Files Created:**
- `include/core/ChunkVoxelModificationSystem.h` (85 lines)
- `src/core/ChunkVoxelModificationSystem.cpp` (142 lines)

**Files Modified:**
- `include/core/ChunkManager.h` - Added ChunkVoxelModificationSystem member
- `src/core/ChunkManager.cpp` - Delegated 8 modification methods

**Functions Extracted:**
1. `setCubeColorFast()` - Fast color update with dirty marking
2. `removeCubeFast()` - Fast cube removal
3. `addCubeFast()` - Fast cube addition
4. `setCubeColor()` - Legacy color setter
5. `removeCube()` - Legacy removal with face updates
6. `addCube()` - Legacy addition with face updates
7. `setCubeColorEfficient()` - Color change (no texture modification)
8. `setSubcubeColorEfficient()` - Subcube color change

**Callback Pattern (4 callbacks):**
- GetChunkFunc - Get chunk at world position
- MarkChunkDirtyFunc - Mark chunk for GPU update
- UpdateAfterCubeBreakFunc - Update faces after removal
- UpdateAfterCubePlaceFunc - Update faces after placement

**Key Features:**
- **Write Operations**: Complements ChunkVoxelQuerySystem (read/write separation)
- **Dirty Tracking**: Automatic chunk marking for GPU updates
- **Face Updates**: Selective face rebuilding after modifications
- **Legacy Support**: Backward compatibility with updateAfterCubeBreak/Place
- **Coordinate Conversion**: Automatic world → local translation

**Metrics:**
- ChunkManager: 680 → 659 lines (-21, -3.1%)
- ChunkVoxelModificationSystem: 227 lines total (85 header + 142 impl)
- Combined Phases 12-18: -755 lines (-53.4% from original 1,414)

**Testing:** ✅ Build successful, runtime verified

**Lessons Learned:**
- Read/write separation (QuerySystem + ModificationSystem) is clean design pattern
- Modification system naturally uses query system via callbacks
- Fast methods (mark dirty) vs legacy methods (trigger face updates) serve different needs
- Color efficient methods preserve current texture-based rendering approach

---

## Best Practices Identified

### What Worked Well
1. **Small, incremental changes** - Easier to test and debug
2. **Git branches per refactoring** - Clean history, easy rollback
3. **Build and test after each change** - Catch errors immediately
4. **Systematic approach** - Create → Move → Update → Test → Commit
5. **Clear documentation** - Quick-start guides help future work
6. **Use existing patterns** - Follow established code style
7. **Composite map keys** - When handling button+modifier combinations, use struct keys
8. **Exact modifier matching** - Don't trigger Ctrl+click when user just clicks
9. **Read/write separation** - Query and modification systems cleanly separated

### Challenges Encountered
1. **Duplicate function declarations** - Syntax errors from copy-paste
2. **CMake reconfiguration needed** - For new .cpp files
3. **Missing include files** - Need to add utility includes to call sites
4. **Batch replacements** - PowerShell helps but verify results
5. **Mouse action collision** - Multiple actions on same button need composite keys
6. **Namespace closing braces** - Extra braces cause mysterious compilation errors
7. **LOG macro formatting** - Some LOG_DEBUG/LOG_TRACE calls incompatible with format strings
8. **Member variable references** - Batch replacements can miss ImGui/external API calls
9. **Include path errors** - Timer.h in utils/ not core/, verify correct paths
10. **Namespace confusion** - ChunkManager in root, ImGuiRenderer in UI, RenderPipeline in Vulkan
11. **Forward declarations** - Must match actual class namespaces exactly (not assumed locations)

### Tools & Techniques
- **grep_search** - Find all occurrences before changing
- **read_file** - Understand context before editing
- **replace_string_in_file** - Precise, context-aware edits
- **PowerShell batch replace** - Efficient for many similar changes
- **Git branches** - Safe experimentation
- **Build verification** - Catch issues early

---

## Future Considerations

### Potential Improvements
- ~~Consider creating `camera::Camera` class separate from InputManager~~ (Integrated into InputManager)
- May want `rendering::RenderCoordinator` to manage render loops
- `world::WorldManager` might coordinate chunk/voxel systems
- ~~Input/Camera separation~~ Camera controls now in InputManager with good separation

### Architecture Decisions
- Keep Application as thin coordinator
- Each module should be independently testable
- Minimize cross-module dependencies
- Use dependency injection where possible
- Maintain backward compatibility during transition

---

## Statistics Summary

**Total Lines Reduced from Application.cpp:** ~1,738 lines (66% reduction)  
**Total New Files Created:** 12 files (6 .h + 6 .cpp)  
**Total Refactorings Complete:** 6 (WindowManager, CoordinateUtils, InputManager, VoxelInteractionSystem, PerformanceMonitor, RenderCoordinator)  
**Current Application.cpp Size:** ~907 lines (from 2,645 original)  
**Remaining to Target:** ~507 lines to reach ~400 line goal  
**Next Major Targets:** WorldInitializer (~350-400 lines) or other initialization/setup code

---

## Next Session Goals

**Primary Goal:** Continue refactoring toward ~400 line Application.cpp  
**Best Next Target:**
1. **WorldInitializer** - Extract world setup and initialization (~350-400 lines) - HIGH IMPACT

**Success Criteria:**
- RenderCoordinator fully functional ✅
- Build successful ✅
- Runtime verified ✅
- Documentation updated ✅

**Next Steps:**
- WorldInitializer extraction to handle initialization code
- Continue systematic extraction process
- Maintain testing at each step
- Target reaching ~400 lines or less in Application.cpp
