# DynamicCube → Cube Migration Guide

**Date:** November 15, 2025  
**Branch:** `feature/unify-cube-class`  
**Goal:** Eliminate `DynamicCube` class and merge its functionality into `Cube` class for consistency with `Subcube` and `Microcube` patterns.

---

## 🎯 Migration Objective

Currently, the codebase has an architectural inconsistency:
- `Subcube` and `Microcube` handle both static and dynamic states in a single class
- `Cube` + `DynamicCube` split this responsibility into two separate classes

This migration unifies `DynamicCube` into `Cube` to achieve consistency across all three hierarchy levels.

---

## 📊 Impact Analysis

**Files to Modify:** 7 core files  
**Files to Delete:** 2 files (DynamicCube.h/cpp)  
**Documentation to Update:** 4 files  
**Total References:** 84 occurrences of "DynamicCube"  
**Estimated Time:** 2-3 hours  

### Files Affected

**Core Implementation:**
1. `include/core/Cube.h` - Add DynamicCube fields/methods
2. `src/core/Cube.cpp` - Implement new methods
3. `include/core/ChunkManager.h` - Change vector types & signatures
4. `src/core/ChunkManager.cpp` - Update usage (10 locations)
5. `src/scene/VoxelInteractionSystem.cpp` - Update creation (13 locations)
6. `src/graphics/RenderCoordinator.cpp` - Update rendering (1 location)
7. `src/Application.cpp` - Update includes (3 locations)

**To Delete:**
- `include/core/DynamicCube.h`
- `src/core/DynamicCube.cpp`

**Documentation:**
- `docs/CodebaseRefactoringAnalysis.md`
- `docs/LoggingReference.md`
- `docs/LOGGING_SUMMARY.md`
- `docs/RenderingArchitectureAnalysis.md`

---

## 📋 Migration Phases

### **PHASE 1: Extend Cube Class** ⚡ LOW RISK

Add DynamicCube functionality to Cube without breaking existing code.

#### Step 1.1: Add Private Fields to `Cube.h`

Add after existing `rigidBody` field:
```cpp
// Physics position/rotation (for dynamic cubes)
glm::vec3 physicsPosition = glm::vec3(0.0f);
glm::vec4 physicsRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

// Material system (for dynamic cubes)
std::string materialName = "Default";
float lifetime = 30.0f;
```

#### Step 1.2: Add Public Methods to `Cube.h`

Add to public section:
```cpp
// Physics accessors
const glm::vec3& getPhysicsPosition() const { return physicsPosition; }
const glm::vec4& getPhysicsRotation() const { return physicsRotation; }
bool isDynamic() const { return rigidBody != nullptr; }

// Lifetime management
float getLifetime() const { return lifetime; }
bool hasExpired() const { return isDynamic() && lifetime <= 0.0f; }
void setLifetime(float time) { lifetime = time; }
void updateLifetime(float deltaTime) { lifetime -= deltaTime; }

// Material system
const std::string& getMaterialName() const { return materialName; }
void setMaterial(const std::string& material);
void applyMaterialProperties();
void applyMaterialProperties(const std::string& newMaterialName);
glm::vec3 getEffectiveColor() const;

// Physics mutators
void setPhysicsPosition(const glm::vec3& pos) { physicsPosition = pos; }
void setPhysicsRotation(const glm::vec4& rot) { physicsRotation = rot; }
```

#### Step 1.3: Add New Constructor to `Cube.h`

Add with existing constructors:
```cpp
Cube(const glm::ivec3& pos, const glm::vec3& col, const std::string& material);
```

#### Step 1.4: Update `getWorldPosition()` in `Cube.cpp`

Replace existing implementation:
```cpp
glm::vec3 Cube::getWorldPosition() const {
    // If dynamic, use smooth physics position
    // If static, convert grid position to world
    return isDynamic() ? physicsPosition : glm::vec3(position);
}
```

#### Step 1.5: Add Constructor Implementation to `Cube.cpp`

Add after existing constructors:
```cpp
Cube::Cube(const glm::ivec3& pos, const glm::vec3& col, const std::string& material) 
    : position(pos), color(col), originalColor(col), materialName(material),
      broken(false), visible(true), rigidBody(nullptr) {
    initializeBonds();
    physicsPosition = glm::vec3(pos); // Initialize physics position to grid position
}
```

#### Step 1.6: Copy Material Methods from `DynamicCube.cpp` to `Cube.cpp`

Add these implementations (copy from DynamicCube.cpp):
```cpp
void Cube::setMaterial(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

void Cube::applyMaterialProperties() {
    if (!rigidBody) return;
    
    // Get material properties from physics world
    const auto& props = Physics::MaterialRegistry::getMaterialProperties(materialName);
    
    // Apply to rigid body
    rigidBody->setFriction(props.friction);
    rigidBody->setRestitution(props.restitution);
    
    // Update mass based on material density
    btVector3 inertia(0, 0, 0);
    btCollisionShape* shape = rigidBody->getCollisionShape();
    if (shape) {
        float volume = 1.0f; // 1m³ cube
        float mass = props.density * volume;
        shape->calculateLocalInertia(mass, inertia);
        rigidBody->setMassProps(mass, inertia);
    }
}

void Cube::applyMaterialProperties(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

glm::vec3 Cube::getEffectiveColor() const {
    // Get material tint
    const auto& props = Physics::MaterialRegistry::getMaterialProperties(materialName);
    return color * props.colorTint;
}
```

#### Step 1.7: Rename CUBE_SIZE → CUBE_SCALE

Change in `Cube.h`:
```cpp
// Old:
static constexpr float CUBE_SIZE = 1.0f;
// New:
static constexpr float CUBE_SCALE = 1.0f;
```

Update `getSize()` method:
```cpp
// Old:
static float getSize() { return CUBE_SIZE; }
// New:
static float getScale() { return CUBE_SCALE; }
```

**✅ Checkpoint 1:** Build and verify Cube compiles
```bash
cd build
cmake --build . --config Debug --target VulkanCube
```

---

### **PHASE 2: Update ChunkManager** ⚠️ MEDIUM RISK

Change internal storage from `DynamicCube` to `Cube`.

#### Step 2.1: Update Include in `ChunkManager.h`

```cpp
// Remove this line:
#include "DynamicCube.h"

// (Cube.h is already included)
```

#### Step 2.2: Update Vector Type in `ChunkManager.h`

Find and replace:
```cpp
// Old (line ~41):
std::vector<std::unique_ptr<DynamicCube>> globalDynamicCubes;

// New:
std::vector<std::unique_ptr<Cube>> globalDynamicCubes;
```

#### Step 2.3: Update Method Signatures in `ChunkManager.h`

```cpp
// Old (line ~150):
void addGlobalDynamicCube(std::unique_ptr<DynamicCube> cube);

// New:
void addGlobalDynamicCube(std::unique_ptr<Cube> cube);
```

```cpp
// Old (line ~154):
const std::vector<std::unique_ptr<DynamicCube>>& getGlobalDynamicCubes() const { return globalDynamicCubes; }

// New:
const std::vector<std::unique_ptr<Cube>>& getGlobalDynamicCubes() const { return globalDynamicCubes; }
```

#### Step 2.4: Update Implementation in `ChunkManager.cpp`

```cpp
// Old (line ~942):
void ChunkManager::addGlobalDynamicCube(std::unique_ptr<DynamicCube> cube) {

// New:
void ChunkManager::addGlobalDynamicCube(std::unique_ptr<Cube> cube) {
```

No other changes needed in ChunkManager.cpp - all method calls remain identical.

**✅ Checkpoint 2:** Build ChunkManager
```bash
cmake --build . --config Debug
```

---

### **PHASE 3: Update VoxelInteractionSystem** ⚠️ MEDIUM RISK

Update cube creation points.

#### Step 3.1: Update Cube Creation (Line ~360)

```cpp
// Old:
auto dynamicCube = std::make_unique<DynamicCube>(cubeCornerPos, originalColor, selectedMaterial);

// New:
auto dynamicCube = std::make_unique<Cube>(cubeCornerPos, originalColor, selectedMaterial);
```

#### Step 3.2: Update Cube Creation (Line ~661)

Same change as above:
```cpp
// Old:
auto dynamicCube = std::make_unique<DynamicCube>(cubeCornerPos, originalColor, selectedMaterial);

// New:
auto dynamicCube = std::make_unique<Cube>(cubeCornerPos, originalColor, selectedMaterial);
```

**Note:** Variable name `dynamicCube` can stay as-is for clarity, even though it's now a `Cube*`.

**✅ Checkpoint 3:** Build VoxelInteractionSystem
```bash
cmake --build . --config Debug
```

---

### **PHASE 4: Update Application.cpp** ⚡ LOW RISK

Remove unnecessary include.

#### Step 4.1: Remove Include (Line 11)

```cpp
// Remove:
#include "core/DynamicCube.h"
```

**✅ Checkpoint 4:** Build Application
```bash
cmake --build . --config Debug
```

---

### **PHASE 5: Verify RenderCoordinator** ⚡ LOW RISK

No changes needed - verify it compiles.

#### Step 5.1: Check Line 132

This line should compile without changes:
```cpp
size_t cubeCount = chunkManager->getGlobalDynamicCubeCount();
```

**✅ Checkpoint 5:** Build RenderCoordinator
```bash
cmake --build . --config Debug
```

---

### **PHASE 6: Delete DynamicCube Files** 🔴 HIGH RISK

Only do this after full build succeeds!

#### Step 6.1: Full Build Test

```bash
cd build
cmake --build . --config Debug --target VulkanCube
```

#### Step 6.2: If Build Succeeds, Delete Files

```bash
cd ..
git rm include/core/DynamicCube.h
git rm src/core/DynamicCube.cpp
```

#### Step 6.3: Rebuild to Confirm

```bash
cd build
cmake --build . --config Debug --clean-first
```

**✅ Checkpoint 6:** Full clean build succeeds

---

### **PHASE 7: Update Documentation** ⚡ LOW RISK

Update all documentation references.

#### Step 7.1: Update `docs/RenderingArchitectureAnalysis.md`

Find and update:
- Remove "DynamicCube" section (line ~74)
- Update "Cube" section to mention dynamic mode
- Update namespace list (line ~343)
- Update "Keep existing Cube/Subcube/DynamicCube" text (line ~402)

#### Step 7.2: Update `docs/LoggingReference.md`

Remove DynamicCube.cpp entry (line ~137)

#### Step 7.3: Update `docs/LOGGING_SUMMARY.md`

Remove DynamicCube.cpp from list (lines ~49, ~153)

#### Step 7.4: Update `docs/CodebaseRefactoringAnalysis.md`

Remove DynamicCube reference (line ~305)

**✅ Checkpoint 7:** Documentation reflects unified Cube class

---

### **PHASE 8: Testing & Verification** 🧪 CRITICAL

Comprehensive testing of all affected functionality.

#### Test Case 1: Static Cube Creation
- [ ] Place cubes in world
- [ ] Cubes appear at correct grid positions
- [ ] Cubes are solid (no physics)

#### Test Case 2: Dynamic Cube Creation
- [ ] Break a static cube
- [ ] Cube converts to dynamic (falls with physics)
- [ ] Cube has correct material properties

#### Test Case 3: Material System
- [ ] Create cube with "Wood" material
- [ ] Create cube with "Stone" material
- [ ] Create cube with "Glass" material
- [ ] Each behaves differently (bounce, friction)

#### Test Case 4: Lifetime Management
- [ ] Dynamic cubes expire after 30 seconds
- [ ] Expired cubes are cleaned up automatically
- [ ] No memory leaks

#### Test Case 5: Rendering
- [ ] Static cubes render correctly
- [ ] Dynamic cubes render correctly
- [ ] Both use correct pipelines

#### Test Case 6: Subcube/Microcube
- [ ] Subcube breaking still works
- [ ] Microcube breaking still works
- [ ] No regression in subdivision system

**✅ Checkpoint 8:** All tests pass

---

## 🔄 Rollback Plan

If migration fails at any phase:

```bash
# Discard all changes
git checkout .
git clean -fd

# Or rollback to pre-migration state
git reset --hard HEAD

# Or switch back to main
git checkout main
```

---

## 📝 Pre-Migration Checklist

- [ ] All current changes committed or stashed
- [ ] Created feature branch: `feature/unify-cube-class`
- [ ] Backup branch created: `backup-before-dynamiccube-merge`
- [ ] Build currently passes on main branch
- [ ] Migration document reviewed

---

## 🎉 Post-Migration Checklist

- [ ] All phases completed successfully
- [ ] Full build passes
- [ ] All tests pass
- [ ] Documentation updated
- [ ] No compiler warnings
- [ ] Git commit with clear message
- [ ] Ready for merge to main

---

## 📊 Progress Tracking

| Phase | Status | Time | Notes |
|-------|--------|------|-------|
| Phase 1 | ⬜ Not Started | - | - |
| Phase 2 | ⬜ Not Started | - | - |
| Phase 3 | ⬜ Not Started | - | - |
| Phase 4 | ⬜ Not Started | - | - |
| Phase 5 | ⬜ Not Started | - | - |
| Phase 6 | ⬜ Not Started | - | - |
| Phase 7 | ⬜ Not Started | - | - |
| Phase 8 | ⬜ Not Started | - | - |

---

## 🔍 Known Issues & Resolutions

*Document any issues encountered during migration here*

---

## 📚 References

- Original Analysis: See comprehensive analysis in chat history (Nov 15, 2025)
- Issue Discussion: DynamicCube architectural inconsistency
- Related Pattern: Subcube/Microcube unified class design
