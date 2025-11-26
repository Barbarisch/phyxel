# Subsystem Architecture

## Overview

This document describes the subsystem architecture pattern used throughout the Phyxel codebase. This pattern emerged from refactoring monolithic classes into focused, single-responsibility components.

## Core Pattern: Callback-Based Subsystems

### Philosophy

Rather than tight coupling through direct member access or inheritance, subsystems use **callback-based dependency injection**. This provides:

- **Clean separation of concerns**: Each subsystem focuses on one domain
- **Testability**: Subsystems can be tested in isolation with mock callbacks
- **Flexibility**: Parent classes remain unaware of subsystem implementation details
- **No circular dependencies**: Callbacks break dependency cycles

### Pattern Structure

```cpp
// 1. Subsystem defines callback types
class Subsystem {
    using DataAccessFunc = std::function<Data&()>;
    using NotifyFunc = std::function<void(Event)>;
    
    // 2. Callbacks are configured during initialization
    void setCallbacks(DataAccessFunc getData, NotifyFunc notify);
    
    // 3. Subsystem uses callbacks to access parent state
    void doWork() {
        auto& data = m_getData();
        // ... work with data ...
        m_notify(Event::Complete);
    }
    
private:
    DataAccessFunc m_getData;
    NotifyFunc m_notify;
};

// 4. Parent configures callbacks with lambdas
class Parent {
    Subsystem m_subsystem;
    Data m_data;
    
    void initialize() {
        m_subsystem.setCallbacks(
            [this]() -> Data& { return m_data; },
            [this](Event e) { handleEvent(e); }
        );
    }
};
```

## Major Subsystem Hierarchies

### 1. ChunkManager Subsystems

**ChunkManager** (604 lines) - Multi-chunk world coordinator
- **ChunkStreamingManager** - Chunk loading, saving, streaming (handles WorldStorage)
- **DynamicObjectManager** - Global dynamic voxel lifecycle (subcubes, cubes, microcubes)
- **FaceUpdateCoordinator** - Face rebuilding coordination for dynamic objects
- **ChunkInitializer** - Chunk creation and initialization
- **DirtyChunkTracker** - Selective chunk update optimization
- **ChunkVoxelQuerySystem** - O(1) voxel lookups with spatial hashing
- **ChunkVoxelModificationSystem** - Voxel add/remove/color operations

**Original size**: 1,414 lines  
**Current size**: 604 lines  
**Reduction**: -810 lines (-57%)

### 2. Chunk Subsystems

**Chunk** (801 lines) - 32x32x32 voxel section manager
- **ChunkRenderManager** (Graphics) - Face generation, Vulkan buffer management (~328 lines)
- **ChunkPhysicsManager** (Physics) - Collision shapes, rigid body management (~833 lines)
- **ChunkVoxelManager** - Voxel hierarchy operations (cubes→subcubes→microcubes) (~616 lines)
- **ChunkVoxelBreaker** - Static→dynamic voxel conversion (~120 lines)

**Original size**: 2,444 lines  
**Current size**: 801 lines  
**Reduction**: -1,643 lines (-67%)

### 3. VoxelInteractionSystem Subsystems

**VoxelInteractionSystem** (519 lines) - Mouse interaction coordinator
- **VoxelRaycaster** - Ray-voxel intersection, O(1) picking (~200 lines)
- **VoxelForceApplicator** - Mouse-based force application (~235 lines)
- **VoxelManipulationSystem** - Voxel manipulation (remove, subdivide, break) (~481 lines)

**Original size**: 1,275 lines  
**Current size**: 519 lines  
**Reduction**: -756 lines (-59%)

## Callback Types and Patterns

### Common Callback Patterns

#### 1. Data Access Callbacks
```cpp
using GetDataFunc = std::function<Data&()>;
using GetConstDataFunc = std::function<const Data&()>;
```
**Purpose**: Provide read/write or read-only access to parent's data structures.

#### 2. Modification Callbacks
```cpp
using SetDirtyFunc = std::function<void(bool)>;
using MarkChunkDirtyFunc = std::function<void(Chunk*)>;
```
**Purpose**: Notify parent of state changes requiring updates.

#### 3. Operation Callbacks
```cpp
using RebuildFacesFunc = std::function<void()>;
using UpdateChunkFunc = std::function<void(size_t)>;
```
**Purpose**: Trigger specific operations in parent or sibling subsystems.

#### 4. Query Callbacks
```cpp
using GetChunkAtFunc = std::function<Chunk*(const glm::ivec3&)>;
using FindCubeFunc = std::function<Cube*(const glm::ivec3&)>;
```
**Purpose**: Perform lookups in parent's data structures.

### Callback Initialization Pattern

All subsystems follow this initialization pattern:

```cpp
// In parent's initialize() or constructor
void Parent::initialize() {
    subsystem.setCallbacks(
        // Data access
        [this]() -> Data& { return m_data; },
        
        // Notifications
        [this](Event e) { handleEvent(e); },
        
        // Operations
        [this]() { rebuild(); },
        
        // Queries
        [this](Key k) -> Value* { return lookup(k); }
    );
}
```

## Design Principles

### 1. Single Responsibility
Each subsystem handles ONE domain:
- **ChunkVoxelManager**: Voxel hierarchy (cubes, subcubes, microcubes)
- **ChunkPhysicsManager**: Physics bodies and collision shapes
- **ChunkRenderManager**: Face generation and GPU buffers

### 2. Callback Over Inheritance
Instead of:
```cpp
class Chunk : public Renderer, public Physics { }  // ❌ Tight coupling
```

We use:
```cpp
class Chunk {
    ChunkRenderManager renderManager;  // ✅ Composition
    ChunkPhysicsManager physicsManager;
};
```

### 3. Minimal Public Interface
Subsystems expose only essential methods:
```cpp
class VoxelRaycaster {
public:
    void setCallbacks(...);           // Setup
    VoxelLocation pickVoxel(...);     // Core functionality
    glm::vec3 screenToWorldRay(...);  // Helper
    
    // No getters, no setters, no implementation details leaked
};
```

### 4. State Ownership
- **Parent owns data**: Subsystems never own primary data structures
- **Subsystems own logic**: All domain logic lives in the subsystem
- **Callbacks bridge the gap**: Parent provides access via callbacks

## Benefits Achieved

### Code Organization
- ✅ **Reduced file sizes**: Largest files now ~800 lines (was 2,444)
- ✅ **Clear boundaries**: Each subsystem has obvious responsibilities
- ✅ **Easy navigation**: Find voxel code in VoxelManager, physics in PhysicsManager

### Maintainability
- ✅ **Isolated changes**: Modify rendering without touching physics
- ✅ **Clear dependencies**: Callbacks make dependencies explicit
- ✅ **Reduced complexity**: Each subsystem is simpler than original monolith

### Testability
- ✅ **Mock callbacks**: Test subsystems with fake data
- ✅ **Unit testable**: Each subsystem can be tested independently
- ✅ **Integration testable**: Parent can test subsystem coordination

### Performance
- ✅ **Zero runtime cost**: Lambdas inline, callbacks are function pointers
- ✅ **No virtual dispatch**: No inheritance = no vtable lookups
- ✅ **Same memory layout**: Subsystems are just members, no indirection

## Common Subsystem Responsibilities

### Manager Subsystems
Handle lifecycle and coordination:
- ChunkStreamingManager (load/save/unload)
- DynamicObjectManager (create/update/destroy)
- ChunkInitializer (create/populate chunks)

### Coordinator Subsystems
Orchestrate cross-system operations:
- FaceUpdateCoordinator (rebuild faces across multiple objects)
- DirtyChunkTracker (batch update optimization)

### Query Subsystems
Provide optimized lookups:
- ChunkVoxelQuerySystem (O(1) spatial queries)
- VoxelRaycaster (ray-voxel intersection)

### Manipulation Subsystems
Handle data transformations:
- VoxelManipulationSystem (remove, subdivide, break)
- ChunkVoxelManager (voxel hierarchy operations)

### System Subsystems
Interface with external systems:
- ChunkPhysicsManager (Bullet Physics integration)
- ChunkRenderManager (Vulkan rendering)

## When to Create a Subsystem

### ✅ Good Reasons
1. **Clear domain boundary**: "All physics code" or "All rendering code"
2. **Reusable logic**: Code used in multiple places
3. **Complex operations**: 100+ lines of intricate logic
4. **External system interface**: Wrapping Vulkan, Bullet, etc.
5. **Performance optimization**: Spatial queries, caching, batching

### ❌ Bad Reasons
1. **Arbitrary line count**: "File is 500 lines, must split"
2. **Single-use code**: Only called once, not complex
3. **Premature abstraction**: "Might need it later"
4. **Over-engineering**: Creating subsystem for 20-line method

## Anti-Patterns to Avoid

### ❌ Callback Hell
```cpp
// Too many layers of indirection
subsystem.setCallbacks(
    [this]() { return getA(); },
    [this]() { return getB(); },
    [this]() { return getC(); },
    [this]() { return getD(); },
    [this]() { return getE(); },
    [this]() { return getF(); },
    [this]() { return getG(); },
    [this]() { return getH(); }  // ❌ Too many!
);
```

**Solution**: Group related callbacks or reconsider subsystem boundary.

### ❌ Tight Coupling via Callbacks
```cpp
// Subsystem modifying parent state directly
[this](Chunk* chunk) { 
    chunk->someInternalDetail = value;  // ❌ Breaking encapsulation
}
```

**Solution**: Parent provides high-level operation callbacks only.

### ❌ Subsystem State Leakage
```cpp
class Subsystem {
public:
    std::vector<Data>& getData() { return m_data; }  // ❌ Exposing internals
};
```

**Solution**: Subsystems provide operations, not data access.

## Testing Strategy

### Unit Testing Subsystems

```cpp
TEST(VoxelRaycaster, BasicIntersection) {
    VoxelRaycaster raycaster;
    
    // Mock ChunkManager callback
    auto mockGetChunk = [](const glm::ivec3& pos) -> Chunk* {
        static MockChunk chunk;
        return &chunk;
    };
    
    raycaster.setCallbacks(mockGetChunk);
    
    VoxelLocation hit = raycaster.pickVoxel(
        glm::vec3(0,0,-5),  // camera
        glm::vec3(0,0,1)    // ray direction
    );
    
    EXPECT_TRUE(hit.isValid());
    EXPECT_EQ(hit.worldPos, glm::ivec3(0,0,0));
}
```

### Integration Testing Parents

```cpp
TEST(ChunkManager, VoxelQueryIntegration) {
    ChunkManager manager;
    manager.initialize(device, physicalDevice);
    manager.createChunk(glm::ivec3(0,0,0));
    
    // Test that subsystem integration works
    Cube* cube = manager.getCubeAtFast(glm::ivec3(5,5,5));
    EXPECT_NE(cube, nullptr);
}
```

## Migration Guide

When encountering monolithic code:

1. **Identify domain boundaries**: Group related methods
2. **Extract to subsystem**: Create new class with focused responsibility
3. **Define callbacks**: Determine what parent access is needed
4. **Implement setCallbacks()**: Configure dependency injection
5. **Update parent**: Add subsystem member, configure in initialize()
6. **Delegate methods**: Replace implementations with subsystem calls
7. **Test thoroughly**: Verify behavior unchanged

## Summary

The subsystem architecture provides:
- **Modularity**: Clear separation of concerns
- **Testability**: Independent testing of components
- **Maintainability**: Easier to understand and modify
- **Performance**: Zero runtime overhead

All while avoiding the pitfalls of deep inheritance hierarchies or tight coupling.

**Key takeaway**: Use callbacks to invert dependencies, keeping subsystems focused and parents simple.
