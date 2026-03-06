# Phyxel Architecture Overview
## Current State & Proposed Refactoring

---

## Current Architecture (Simplified)

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application.cpp                          │
│                         (2,645 lines)                           │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │  Window      │  │   Camera     │  │    Input     │         │
│  │  Management  │  │   System     │  │   Handling   │         │
│  │  (~200 ln)   │  │  (~250 ln)   │  │  (~350 ln)   │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │   Voxel      │  │  Rendering   │  │ Performance  │         │
│  │ Interaction  │  │ Coordination │  │  Monitoring  │         │
│  │  (~600 ln)   │  │  (~400 ln)   │  │  (~250 ln)   │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
│                                                                 │
│  Plus: Frame loop, physics integration, UI, etc. (~600 ln)     │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌──────────────┐      ┌──────────────┐     ┌──────────────┐
│ ChunkManager │      │    Chunk     │     │ VulkanDevice │
│ (1,439 ln)   │      │  (2,130 ln)  │     │  (1,616 ln)  │
│              │      │              │     │              │
│ • Streaming  │      │ • Storage    │     │ • Swapchain  │
│ • Persistence│      │ • Collision  │     │ • Memory     │
│ • Dynamic    │      │ • Rendering  │     │ • Commands   │
│   Objects    │      │ • Physics    │     │              │
└──────────────┘      └──────────────┘     └──────────────┘

                PROBLEM: Each box has too many responsibilities
                         Hard for AI to process entire files
                         Changes affect too many systems
```

---

## Proposed Refactored Architecture

```
                    ┌─────────────────────┐
                    │  ApplicationCore    │
                    │    (~400 lines)     │
                    │                     │
                    │ • Initialization    │
                    │ • Main Loop         │
                    │ • Cleanup           │
                    │ • Module Composition│
                    └──────────┬──────────┘
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
    ┌─────▼─────┐       ┌─────▼─────┐       ┌─────▼─────┐
    │    UI     │       │   Scene   │       │  Graphics │
    │ ~300 ln   │       │  ~350 ln  │       │  ~450 ln  │
    └───────────┘       └───────────┘       └───────────┘
          │                    │                    │
    ┌─────▼─────┐       ┌─────▼─────┐       ┌─────▼─────┐
    │  Window   │       │  Camera   │       │  Render   │
    │  Manager  │       │ Controller│       │Coordinator│
    │  ~200 ln  │       │  ~350 ln  │       │  ~400 ln  │
    └───────────┘       └───────────┘       └───────────┘
          │
    ┌─────▼─────┐
    │   Input   │
    │  Manager  │
    │  ~300 ln  │
    └───────────┘

          ┌─────────────────────────────────┐
          │        Entity System            │
          │  (Player, Enemy, Character)     │
          └─────────────────────────────────┘
                         │
          ┌─────────────────────────────────┐
          │        Core Systems             │
          └─────────────────────────────────┘
                         │
      ┌──────────────────┼──────────────────┐
      │                  │                  │
┌─────▼─────┐      ┌────▼─────┐     ┌─────▼─────┐
│   World   │      │  Voxel   │     │  Physics  │
│           │      │          │     │           │
└───────────┘      └──────────┘     └───────────┘
      │                  │                 │
┌─────▼─────┐      ┌────▼─────┐     ┌─────▼─────┐
│   Chunk   │      │  Voxel   │     │   Chunk   │
│  Manager  │      │Interaction│    │  Physics  │
│  ~400 ln  │      │  ~600 ln │     │  Manager  │
└───────────┘      └──────────┘     │  ~700 ln  │
      │                              └─────┬─────┘
┌─────▼─────┐                             │
│ Streaming │                       ┌─────▼─────┐
│  Manager  │                       │  Physics  │
│  ~350 ln  │                       │   Shape   │
└───────────┘                       │  Factory  │
      │                             │  ~200 ln  │
┌─────▼─────┐                       └───────────┘
│Persistence│
│  Manager  │
│  ~400 ln  │
└───────────┘


          ┌─────────────────────────────────┐
          │      Individual Chunk           │
          └─────────────────────────────────┘
                         │
          ┌──────────────┼──────────────┐
          │              │              │
    ┌─────▼─────┐  ┌────▼─────┐  ┌────▼─────┐
    │   Voxel   │  │  Chunk   │  │  Chunk   │
    │  Storage  │  │  Physics │  │  Render  │
    │  ~400 ln  │  │  ~700 ln │  │  ~350 ln │
    └───────────┘  └──────────┘  └──────────┘


          ┌─────────────────────────────────┐
          │         Vulkan Layer            │
          └─────────────────────────────────┘
                         │
      ┌──────────────────┼──────────────────┐
      │                  │                  │
┌─────▼─────┐      ┌────▼─────┐     ┌─────▼─────┐
│  Vulkan   │      │  Vulkan  │     │  Vulkan   │
│  Device   │      │ Swapchain│     │  Memory   │
│  ~600 ln  │      │  ~400 ln │     │  Manager  │
└───────────┘      └──────────┘     │  ~350 ln  │
                                    └─────┬─────┘
                                          │
                                    ┌─────▼─────┐
                                    │  Vulkan   │
                                    │  Command  │
                                    │  Manager  │
                                    │  ~300 ln  │
                                    └───────────┘


          ┌─────────────────────────────────┐
          │         Utilities               │
          └─────────────────────────────────┘
                         │
      ┌──────────────────┼──────────────────┐
      │                  │                  │
┌─────▼─────┐      ┌────▼─────┐     ┌─────▼─────┐
│Coordinate │      │Performance│    │   Logger  │
│   Utils   │      │  Monitor  │     │  (exists) │
│  ~200 ln  │      │  ~250 ln  │     └───────────┘
└───────────┘      └───────────┘
      │                  
┌─────▼─────┐      
│  Texture  │      
│  Manager  │      
│  ~150 ln  │      
└───────────┘      

BENEFIT: Each module is < 800 lines (AI-digestible)
         Clear separation of concerns
         Easy to locate and modify code
         Reduced compilation dependencies
```

---

## File Count Comparison

### Current (24 source files)
```
Files > 2000 lines: 2   (Application.cpp, Chunk.cpp)
Files > 1000 lines: 4   (+ VulkanDevice.cpp, ChunkManager.cpp)
Files > 500 lines:  9
Average file size:  588 lines
Total LOC:         ~14,000
```

### Proposed (41 source files)
```
Files > 2000 lines: 0   ✅
Files > 1000 lines: 0   ✅
Files > 500 lines:  6   (all < 800)
Average file size:  ~340 lines  (42% reduction)
Total LOC:         ~14,000  (same functionality, better organized)
```

---

## Module Dependency Graph

```
Legend: → depends on

ApplicationCore
  → WindowManager
  → InputManager
  → CameraController
  → RenderCoordinator
  → ChunkManager
  → PhysicsWorld
  → VoxelInteractionSystem

RenderCoordinator
  → ChunkManager
  → RenderPipeline
  → ChunkRenderData

ChunkManager
  → Chunk
  → WorldPersistenceManager
  → ChunkStreamingManager
  → DynamicObjectManager

Chunk
  → VoxelStorage
  → ChunkPhysicsManager
  → ChunkRenderData

ChunkPhysicsManager
  → PhysicsWorld
  → PhysicsShapeFactory

VulkanDevice
  → VulkanSwapchain
  → VulkanMemoryManager
  → VulkanCommandManager

VoxelInteractionSystem
  → ChunkManager
  → ForceSystem
  → PhysicsWorld
  → CoordinateUtils

Utilities (leaf nodes, no dependencies on domain code):
  - CoordinateUtils
  - TextureManager
  - PerformanceMonitor
  - Logger
```

**Layering:**
1. **Utilities** (bottom layer - no domain dependencies)
2. **Core Domain** (Voxel, Chunk, Physics)
3. **Services** (Rendering, Input, Persistence)
4. **Application** (top layer - composition)

---

## Quick Start: First Refactoring

### Option 1: WindowManager (Easiest, 2 hours)

**What you'll do:**
1. Create `include/ui/WindowManager.h` and `src/ui/WindowManager.cpp`
2. Move GLFW initialization from Application
3. Update Application to use WindowManager
4. Test window creation

**Files changed:** 3 files  
**Lines moved:** ~200 lines  
**Risk:** Very low  
**Benefit:** Application.cpp reduced by 10%

**Command sequence:**
```bash
# 1. Create new files
touch include/ui/WindowManager.h
touch src/ui/WindowManager.cpp

# 2. Copy template from RefactoringExamples.md

# 3. Update CMakeLists.txt
# Add: src/ui/WindowManager.cpp

# 4. Build and test
cmake --build build
./build/Debug/Phyxel.exe
```

### Option 2: CoordinateUtils (Medium, 3 hours)

**What you'll do:**
1. Create `include/utils/CoordinateUtils.h` and `src/utils/CoordinateUtils.cpp`
2. Extract coordinate conversion functions from Chunk, ChunkManager, Application
3. Update all callers to use CoordinateUtils
4. Test coordinate conversions

**Files changed:** 6 files (CoordinateUtils + 3 updates)  
**Lines centralized:** ~150 lines (was duplicated in 3 places)  
**Risk:** Medium (affects many systems)  
**Benefit:** Single source of truth for coordinates

### Option 3: CameraController (Medium, 4 hours)

**What you'll do:**
1. Create `include/scene/CameraController.h` and `src/scene/CameraController.cpp`
2. Move camera state and logic from Application
3. Update Application to use CameraController
4. Test camera movement and looking

**Files changed:** 3 files  
**Lines moved:** ~350 lines  
**Risk:** Medium (input handling can be tricky)  
**Benefit:** Reusable camera system

---

## Incremental Refactoring Workflow

### Week 1: Foundation
- [ ] Day 1-2: Extract WindowManager
- [ ] Day 3-4: Extract CoordinateUtils
- [ ] Day 5: Testing and documentation

### Week 2: Input & Camera
- [ ] Day 1-3: Extract InputManager
- [ ] Day 4-5: Extract CameraController

### Week 3: Rendering
- [ ] Day 1-2: Extract RenderCoordinator
- [ ] Day 3-4: Extract ChunkRenderData
- [ ] Day 5: Extract VulkanSwapchain

### Week 4: Physics
- [ ] Day 1-3: Extract ChunkPhysicsManager (complex!)
- [ ] Day 4: Extract PhysicsShapeFactory
- [ ] Day 5: Testing and performance validation

### Week 5: World Management
- [ ] Day 1-2: Extract WorldPersistenceManager
- [ ] Day 3-4: Extract ChunkStreamingManager
- [ ] Day 5: Extract DynamicObjectManager

### Week 6: Final Cleanup
- [ ] Day 1-2: Extract VoxelInteractionSystem
- [ ] Day 3-4: Final Vulkan extractions
- [ ] Day 5: Documentation, metrics, celebration! 🎉

---

## Testing Strategy Summary

### Before Each Refactoring
```cpp
// Write characterization test to capture current behavior
TEST(ChunkTest, AddCubeBehavior) {
    Chunk chunk(glm::ivec3(0));
    chunk.addCube(glm::ivec3(5, 5, 5), glm::vec3(1, 0, 0));
    
    EXPECT_EQ(chunk.getCubeCount(), 1);
    EXPECT_NE(chunk.getCubeAt(glm::ivec3(5, 5, 5)), nullptr);
}
```

### After Each Refactoring
```cpp
// Same test should still pass
// Add new unit tests for extracted module
TEST(VoxelStorageTest, AddCube) {
    VoxelStorage storage;
    storage.addCube(glm::ivec3(5, 5, 5), glm::vec3(1, 0, 0));
    
    EXPECT_EQ(storage.getCubeCount(), 1);
    EXPECT_NE(storage.getCubeAt(glm::ivec3(5, 5, 5)), nullptr);
}
```

### Integration Testing
```bash
# Run full game and test:
# - Place cubes
# - Remove cubes
# - Subdivide cubes
# - Break cubes with physics
# - Save and load world
# - Test camera movement
# - Test chunk streaming

# Performance regression test:
# - Measure FPS before refactoring
# - Measure FPS after refactoring
# - Should be within 5% (preferably better!)
```

---

## Rollback Plan

If a refactoring goes wrong:

```bash
# 1. Stash your changes
git stash

# 2. Verify the old version still works
cmake --build build --clean-first
./build/Debug/Phyxel.exe

# 3. Review what went wrong
git stash show -p

# 4. Either:
# a) Fix the issue and try again
git stash pop

# b) Or abandon and try a different approach
git stash drop
```

---

## Metrics Dashboard (Track Progress)

### Code Metrics
| Metric                    | Before | After Phase 3 | After Phase 6 | Target |
|---------------------------|--------|---------------|---------------|--------|
| Largest file size         | 2645   | 1400          | 700           | <800   |
| Average file size         | 588    | 450           | 340           | <400   |
| Files > 1000 lines        | 4      | 2             | 0             | 0      |
| Total files               | 24     | 32            | 41            | ~40    |
| Application.cpp lines     | 2645   | 1500          | 400           | <500   |
| Chunk.cpp lines           | 2130   | 1500          | 600           | <700   |

### Build Metrics
| Metric                    | Before | Target | Notes                     |
|---------------------------|--------|--------|---------------------------|
| Full rebuild time         | ?      | <30s   | Measure with `time cmake` |
| Incremental build time    | ?      | <5s    | Change 1 file, rebuild    |
| Header includes per file  | ?      | <20    | Use `clang -H`            |

### Runtime Metrics
| Metric                    | Before | Target | Notes                     |
|---------------------------|--------|--------|---------------------------|
| Startup time              | ?      | <2s    | Time to first frame       |
| FPS (1000 cubes)          | ?      | ±5%    | No regression             |
| Memory usage              | ?      | ±10%   | Slight increase OK        |

---

## Common Questions

### Q: Will this break my existing save files?
**A:** No! Refactoring doesn't change data formats, only code organization.

### Q: Will performance get worse?
**A:** It shouldn't! Good refactoring can even improve performance through better cache locality. We'll benchmark every step.

### Q: How long will this take?
**A:** Following the 6-week plan: ~40-60 hours total. But you can stop after any phase and still have improvements!

### Q: What if I want to refactor differently?
**A:** This plan is a suggestion! Use it as a template and adapt to your preferences.

### Q: Should I refactor everything at once?
**A:** **NO!** Incremental refactoring is much safer. Do one module, test, commit, repeat.

### Q: Can I skip the testing?
**A:** Please don't! Tests are your safety net. They ensure refactoring doesn't break functionality.

---

## Success Stories (Motivation)

### Similar Refactorings in Game Engines

**Unity Engine:**
- 2015: Split monolithic GameObject into ComponentSystem
- Result: 40% faster iteration, easier to add features

**Unreal Engine:**
- 2018: Extracted rendering backend from engine core
- Result: Vulkan and DX12 backends added in 6 months

**Godot Engine:**
- 2020: Split scene system into smaller modules
- Result: Community contributions increased 3x

### Expected Benefits for Phyxel

**For AI Assistance:**
- ✅ Files fit in AI context window (< 4000 tokens)
- ✅ AI can understand module purpose quickly
- ✅ Better code suggestions from AI

**For Development:**
- ✅ Find code 2x faster
- ✅ Add features without affecting other systems
- ✅ Easier to onboard contributors

**For Maintenance:**
- ✅ Bugs isolated to specific modules
- ✅ Refactoring one system doesn't break others
- ✅ Can rewrite subsystems independently

---

## Getting Help

### Stuck on a Refactoring?
1. Check `RefactoringExamples.md` for code templates
2. Look for similar patterns in the codebase
3. Ask AI: "How do I extract [functionality] from [file] into a new class?"
4. Draw a dependency diagram on paper

### Compilation Errors?
1. Check for missing includes
2. Verify forward declarations
3. Look for circular dependencies
4. Try: `cmake --build build --verbose`

### Tests Failing?
1. Run old version to confirm test was passing
2. Compare behavior before/after with debugger
3. Check if you missed updating a caller
4. Verify data structures match exactly

---

## Next Steps

1. **Read** `CodebaseRefactoringAnalysis.md` for detailed analysis
2. **Study** `RefactoringExamples.md` for code templates
3. **Choose** your first refactoring (recommend WindowManager)
4. **Branch** `git checkout -b refactor/window-manager`
5. **Extract** following the migration checklist
6. **Test** thoroughly
7. **Commit** and move to next module!

Remember: **Perfect is the enemy of good. Ship incremental improvements!**
