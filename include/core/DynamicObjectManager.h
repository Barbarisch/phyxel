#pragma once

#include "Types.h"
#include "Cube.h"
#include <vector>
#include <memory>
#include <functional>

namespace VulkanCube {

namespace Physics {
    class PhysicsWorld;
}

// Forward declarations
class Microcube;

/**
 * DynamicObjectManager - Manages global dynamic voxel objects (subcubes, cubes, microcubes)
 * 
 * EXTRACTED FROM CHUNKMANAGER.CPP (November 2025)
 * Original ChunkManager: 1,201 lines → 1,086 lines after extraction
 * DynamicObjectManager: 420 lines (100 header + 320 implementation)
 * Reduction: 115 lines from ChunkManager (-9.6%)
 * 
 * PURPOSE:
 * Manages the lifecycle of "global" dynamic objects - voxels that have broken free from chunks
 * and are now physics-enabled entities moving independently through the world.
 * 
 * RESPONSIBILITIES:
 * 1. Dynamic Object Lifecycle: Add, update, remove physics-enabled voxels
 * 2. Physics Synchronization: Update positions/rotations from physics simulation
 * 3. Lifetime Management: Track and remove expired objects (8-second lifetime)
 * 4. Face Rebuilding Coordination: Trigger rendering updates when objects change
 * 
 * OBJECT TYPES:
 * - Subcubes: 1/3 scale (broken from subdivided cubes)
 * - Cubes: Full scale (broken from normal cubes)
 * - Microcubes: 1/9 scale (broken from subdivided subcubes)
 * 
 * DESIGN PATTERN:
 * Uses callbacks to access ChunkManager's state and physics world:
 * - PhysicsWorldAccessFunc: Access physics world for body removal
 * - DynamicObjectAccessFunc: Access specific object vector (subcubes/cubes/microcubes)
 * - RebuildFacesFunc: Trigger face rebuilding when objects change
 * 
 * USAGE:
 * DynamicObjectManager dynamicMgr;
 * dynamicMgr.setCallbacks(
 *     [this]() { return physicsWorld; },
 *     [this]() -> auto& { return globalDynamicSubcubes; },
 *     [this]() -> auto& { return globalDynamicCubes; },
 *     [this]() -> auto& { return globalDynamicMicrocubes; },
 *     [this]() { rebuildGlobalDynamicFaces(); }
 * );
 * dynamicMgr.updateAllDynamicObjects(deltaTime);
 */
class DynamicObjectManager {
public:
    // Callback types for accessing ChunkManager state
    using PhysicsWorldAccessFunc = std::function<Physics::PhysicsWorld*()>;
    using DynamicSubcubeVectorAccessFunc = std::function<std::vector<std::unique_ptr<Subcube>>&()>;
    using DynamicCubeVectorAccessFunc = std::function<std::vector<std::unique_ptr<Cube>>&()>;
    using DynamicMicrocubeVectorAccessFunc = std::function<std::vector<std::unique_ptr<Microcube>>&()>;
    using RebuildFacesFunc = std::function<void()>;

    DynamicObjectManager() = default;
    ~DynamicObjectManager() = default;

    // Callback setup
    void setCallbacks(
        PhysicsWorldAccessFunc getPhysicsWorldFunc,
        DynamicSubcubeVectorAccessFunc getSubcubesFunc,
        DynamicCubeVectorAccessFunc getCubesFunc,
        DynamicMicrocubeVectorAccessFunc getMicrocubesFunc,
        RebuildFacesFunc rebuildFacesFunc
    );

    // ===== SUBCUBE MANAGEMENT =====
    void addGlobalDynamicSubcube(std::unique_ptr<Subcube> subcube);
    void updateGlobalDynamicSubcubes(float deltaTime);
    void updateGlobalDynamicSubcubePositions();
    void clearAllGlobalDynamicSubcubes();

    // ===== CUBE MANAGEMENT =====
    void addGlobalDynamicCube(std::unique_ptr<Cube> cube);
    void updateGlobalDynamicCubes(float deltaTime);
    void updateGlobalDynamicCubePositions();
    void clearAllGlobalDynamicCubes();

    // ===== MICROCUBE MANAGEMENT =====
    void addGlobalDynamicMicrocube(std::unique_ptr<Microcube> microcube);
    void updateGlobalDynamicMicrocubes(float deltaTime);
    void updateGlobalDynamicMicrocubePositions();
    void clearAllGlobalDynamicMicrocubes();

    // ===== COMBINED OPERATIONS =====
    void updateAllDynamicObjects(float deltaTime);
    void updateAllDynamicObjectPositions();
    
    // ===== CHARACTER DESTRUCTION =====
    // Derez a character into dynamic physics objects
    // Requires the character to be passed as a void* to avoid circular dependencies
    // (will be cast to Scene::AnimatedVoxelCharacter* internally)
    void derezCharacter(void* characterPtr);

private:
    // Callback functions
    PhysicsWorldAccessFunc m_getPhysicsWorld;
    DynamicSubcubeVectorAccessFunc m_getSubcubes;
    DynamicCubeVectorAccessFunc m_getCubes;
    DynamicMicrocubeVectorAccessFunc m_getMicrocubes;
    RebuildFacesFunc m_rebuildFaces;

    // Debug tracking
    int m_debugCounter = 0;
    bool m_firstUpdate = true;
    
    // Maximum number of dynamic objects allowed before cleanup
    static constexpr size_t MAX_DYNAMIC_OBJECTS = 500;
    
    // Enforce object limits to prevent performance degradation
    void enforceObjectLimits();
};

} // namespace VulkanCube
