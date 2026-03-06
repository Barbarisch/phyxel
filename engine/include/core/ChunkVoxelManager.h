#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <glm/glm.hpp>

namespace Phyxel {

// Forward declarations
class Cube;
class ChunkManager;
namespace Physics {
    class PhysicsWorld;
}
namespace Graphics {
    class ChunkRenderManager;
}
namespace Physics {
    class ChunkPhysicsManager;
}

/**
 * ChunkVoxelManager - Manages the voxel hierarchy (cubes, subcubes, microcubes)
 * 
 * EXTRACTED FROM CHUNK.CPP (Phase 3 Complete - November 2025):
 * Successfully extracted ~616 lines of voxel management code from Chunk, including:
 * - Cube/Subcube/Microcube creation, deletion, and access
 * - Subdivision logic (cube → subcubes → microcubes)
 * - Hash map management for O(1) voxel lookups (cubeMap, subcubeMap, microcubeMap, voxelTypeMap)
 * - Voxel location resolution system for hover detection
 * 
 * DESIGN PATTERN:
 * - Uses setCallbacks() to receive Chunk data accessors once during initialization
 * - Manages voxel hierarchy data structures (vectors + hash maps)
 * - Coordinates with ChunkRenderManager and ChunkPhysicsManager via stored callbacks
 * - All voxel state changes trigger appropriate render/physics updates
 * 
 * PERFORMANCE:
 * - O(1) voxel lookups via hash maps (cubeMap, subcubeMap, microcubeMap)
 * - O(1) indexed cube access (z + y*32 + x*32*32)
 * - Lazy evaluation of voxel types (cached in voxelTypeMap)
 * - Efficient subdivision with automatic parent cleanup
 * 
 * EXTRACTED METHODS:
 * - Cube operations: addCube, removeCube, setCubeColor, getCubeAtFast
 * - Subcube operations: subdivideAt, addSubcube, removeSubcube, clearSubdivisionAt
 * - Microcube operations: subdivideSubcubeAt, addMicrocube, removeMicrocube, clearMicrocubesAt
 * - Hash map management: initializeVoxelMaps, addToVoxelMaps, removeFromVoxelMaps, etc.
 * - Voxel resolution: resolveLocalPosition, hasVoxelAt, hasSubcubeAt, getVoxelType
 */
class ChunkVoxelManager {
public:
    // Callback function types for accessing Chunk data
    using CubesVectorAccessFunc = std::function<std::vector<std::unique_ptr<Cube>>&()>;
    using SubcubesVectorAccessFunc = std::function<std::vector<std::unique_ptr<Subcube>>&()>;
    using MicrocubesVectorAccessFunc = std::function<std::vector<std::unique_ptr<Microcube>>&()>;
    using WorldOriginAccessFunc = std::function<const glm::ivec3&()>;
    using SetDirtyFunc = std::function<void(bool)>;
    using SetNeedsUpdateFunc = std::function<void(bool)>;
    using RebuildFacesFunc = std::function<void()>;
    using AddCollisionFunc = std::function<void(const glm::ivec3&)>;
    using RemoveCollisionFunc = std::function<void(const glm::ivec3&)>;
    using BatchUpdateCollisionsFunc = std::function<void()>;
    using UpdateNeighborCollisionsFunc = std::function<void(const glm::ivec3&)>;
    using IsInBulkOperationFunc = std::function<bool()>;

    ChunkVoxelManager() = default;
    ~ChunkVoxelManager() = default;

    // One-time callback setup (called from Chunk::initialize)
    void setCallbacks(
        CubesVectorAccessFunc getCubes,
        SubcubesVectorAccessFunc getStaticSubcubes,
        MicrocubesVectorAccessFunc getStaticMicrocubes,
        WorldOriginAccessFunc getWorldOrigin,
        SetDirtyFunc setDirty,
        SetNeedsUpdateFunc setNeedsUpdate,
        RebuildFacesFunc rebuildFaces,
        AddCollisionFunc addCollision,
        RemoveCollisionFunc removeCollision,
        UpdateNeighborCollisionsFunc updateNeighborCollisions,
        IsInBulkOperationFunc isInBulkOperation,
        std::function<void()> updateVulkanBuffer
    );

    // Cube operations
    bool addCube(const glm::ivec3& localPos);
    bool addCube(const glm::ivec3& localPos, const std::string& material);
    bool removeCube(const glm::ivec3& localPos);

    // Subcube operations
    bool subdivideAt(const glm::ivec3& localPos);
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);

    // Microcube operations
    bool subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);
    bool addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    bool removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    bool clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);

    // Hash map management
    void initializeVoxelMaps();
    void addToVoxelMaps(const glm::ivec3& localPos, Cube* cube);
    void removeFromVoxelMaps(const glm::ivec3& localPos);
    void addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube);
    void removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    void addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube);
    void removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    void updateVoxelMaps(const glm::ivec3& localPos);

    // Voxel location resolution
    VoxelLocation resolveLocalPosition(const glm::ivec3& localPos) const;
    bool hasVoxelAt(const glm::ivec3& localPos) const;
    bool hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;

    // Fast lookups
    Cube* getCubeAtFast(const glm::ivec3& localPos);
    const Cube* getCubeAtFast(const glm::ivec3& localPos) const;
    
    // Utility
    static size_t subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);

    // Direct hash map access (for ChunkManager and other systems)
    const std::unordered_map<glm::ivec3, Cube*, IVec3Hash>& getCubeMap() const { return cubeMap; }
    const std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, IVec3Hash>& getSubcubeMap() const { return subcubeMap; }
    const std::unordered_map<glm::ivec3, VoxelLocation::Type, IVec3Hash>& getVoxelTypeMap() const { return voxelTypeMap; }

    // Helper methods for accessing voxels (public for Chunk delegation)
    Cube* getCubeHelper(const glm::ivec3& localPos) const;
    Subcube* getSubcubeHelper(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    std::vector<Subcube*> getSubcubesHelper(const glm::ivec3& localPos) const;
    Microcube* getMicrocubeHelper(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) const;
    std::vector<Microcube*> getMicrocubesHelper(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) const;

private:
    // Stored callbacks for accessing Chunk state (set once via setCallbacks)
    CubesVectorAccessFunc m_getCubes;
    SubcubesVectorAccessFunc m_getStaticSubcubes;
    MicrocubesVectorAccessFunc m_getStaticMicrocubes;
    WorldOriginAccessFunc m_getWorldOrigin;
    SetDirtyFunc m_setDirty;
    SetNeedsUpdateFunc m_setNeedsUpdate;
    RebuildFacesFunc m_rebuildFaces;
    AddCollisionFunc m_addCollision;
    RemoveCollisionFunc m_removeCollision;
    UpdateNeighborCollisionsFunc m_updateNeighborCollisions;
    IsInBulkOperationFunc m_isInBulkOperation;
    std::function<void()> m_updateVulkanBuffer;

    // O(1) lookup data structures for optimized hover system
    std::unordered_map<glm::ivec3, Cube*, IVec3Hash> cubeMap;
    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, IVec3Hash> subcubeMap;
    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Microcube*, IVec3Hash>, IVec3Hash>, IVec3Hash> microcubeMap;
    std::unordered_map<glm::ivec3, VoxelLocation::Type, IVec3Hash> voxelTypeMap;
};

} // namespace Phyxel
