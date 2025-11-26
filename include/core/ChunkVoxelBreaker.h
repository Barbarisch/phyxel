#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <memory>

namespace VulkanCube {

// Forward declarations
class Cube;
class ChunkManager;

namespace Physics {
    class PhysicsWorld;
}

/**
 * ChunkVoxelBreaker - Handles breaking voxels from static to dynamic physics objects
 * 
 * EXTRACTED FROM CHUNK.CPP (November 2025 - Phase 21)
 * Original Chunk: 996 lines
 * ChunkVoxelBreaker: ~150 lines (35 header + ~115 implementation)
 * 
 * PURPOSE:
 * Handles the complex logic of converting static voxels (cubes, subcubes, microcubes)
 * into dynamic physics-enabled objects that can fall, tumble, and interact with the world.
 * 
 * RESPONSIBILITIES:
 * 1. Static→Dynamic Conversion: Remove from chunk, create physics body
 * 2. Data Structure Updates: Clean up hash maps, voxel type maps, collision shapes
 * 3. Physics Configuration: Set initial position, apply forces, configure gravity
 * 4. Global Transfer: Move dynamic objects to ChunkManager's global system
 * 
 * BREAKING OPERATIONS:
 * - breakSubcube: Convert subcube (1/3 scale) to dynamic physics object
 * 
 * DESIGN PATTERN:
 * Uses callbacks to access Chunk's internal state and coordinate with other systems:
 * - GetSubcubesFunc: Access static subcubes vector
 * - RemoveSubcubeFunc: Remove subcube from data structures
 * - RebuildFacesFunc: Trigger face rebuilding after removal
 * - BatchUpdateCollisionsFunc: Update physics collision shapes
 * - GetMicrocubesAtFunc: Check if subcube has been subdivided
 * - GetSubcubesAtFunc: Debug - list all subcubes at position
 * - SetNeedsUpdateFunc: Mark chunk for GPU buffer update
 * - WorldOriginAccessFunc: Get chunk's world origin
 * 
 * USAGE:
 * ChunkVoxelBreaker breaker;
 * breaker.setCallbacks(
 *     [this]() -> std::vector<Subcube*>& { return staticSubcubes; },
 *     [this](const glm::ivec3& parent, const glm::ivec3& sub) { return removeSubcube(parent, sub); },
 *     [this]() { rebuildFaces(); },
 *     [this]() { batchUpdateCollisions(); },
 *     [this](const glm::ivec3& p, const glm::ivec3& s) { return getMicrocubesAt(p, s); },
 *     [this](const glm::ivec3& p) { return getSubcubesAt(p); },
 *     [this](bool v) { renderManager.setNeedsUpdate(v); },
 *     [this]() -> const glm::ivec3& { return worldOrigin; }
 * );
 * bool success = breaker.breakSubcube(parentPos, subcubePos, physicsWorld, chunkManager, impulse);
 */
class ChunkVoxelBreaker {
public:
    // Callback types for accessing Chunk state
    using GetSubcubesFunc = std::function<std::vector<Subcube*>&()>;
    using RemoveSubcubeFunc = std::function<bool(const glm::ivec3&, const glm::ivec3&)>;
    using RebuildFacesFunc = std::function<void()>;
    using BatchUpdateCollisionsFunc = std::function<void()>;
    using GetMicrocubesAtFunc = std::function<std::vector<Microcube*>(const glm::ivec3&, const glm::ivec3&)>;
    using GetSubcubesAtFunc = std::function<std::vector<Subcube*>(const glm::ivec3&)>;
    using SetNeedsUpdateFunc = std::function<void(bool)>;
    using WorldOriginAccessFunc = std::function<const glm::ivec3&()>;

    ChunkVoxelBreaker() = default;
    ~ChunkVoxelBreaker() = default;

    // Callback setup
    void setCallbacks(
        GetSubcubesFunc getSubcubesFunc,
        RemoveSubcubeFunc removeSubcubeFunc,
        RebuildFacesFunc rebuildFacesFunc,
        BatchUpdateCollisionsFunc batchUpdateCollisionsFunc,
        GetMicrocubesAtFunc getMicrocubesAtFunc,
        GetSubcubesAtFunc getSubcubesAtFunc,
        SetNeedsUpdateFunc setNeedsUpdateFunc,
        WorldOriginAccessFunc worldOriginFunc
    );

    // Break subcube into dynamic physics object
    bool breakSubcube(
        const glm::ivec3& parentPos,
        const glm::ivec3& subcubePos,
        Physics::PhysicsWorld* physicsWorld,
        ChunkManager* chunkManager,
        const glm::vec3& impulseForce
    );

private:
    // Callbacks for accessing Chunk state
    GetSubcubesFunc m_getSubcubes;
    RemoveSubcubeFunc m_removeSubcube;
    RebuildFacesFunc m_rebuildFaces;
    BatchUpdateCollisionsFunc m_batchUpdateCollisions;
    GetMicrocubesAtFunc m_getMicrocubesAt;
    GetSubcubesAtFunc m_getSubcubesAt;
    SetNeedsUpdateFunc m_setNeedsUpdate;
    WorldOriginAccessFunc m_getWorldOrigin;
};

} // namespace VulkanCube
