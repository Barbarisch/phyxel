#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "core/ChunkVoxelStore.h"
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
 * - Sparse hash map management for O(1) subdivided-voxel lookups (subcubeMap, microcubeMap)
 * - Voxel location resolution system for hover detection
 * 
 * DESIGN PATTERN:
 * - Uses setCallbacks() to receive Chunk data accessors once during initialization
 * - Manages voxel hierarchy data structures (vectors + hash maps)
 * - Coordinates with ChunkRenderManager and ChunkPhysicsManager via stored callbacks
 * - All voxel state changes trigger appropriate render/physics updates
 * 
 * PERFORMANCE:
 * - O(1) subdivided-voxel lookups via the sparse hash maps (subcubeMap, microcubeMap)
 * - O(1) indexed cube access (z + y*32 + x*32*32)
 * - Voxel presence/type DERIVED on read from the dense cubes array (no per-voxel cache)
 * - Efficient subdivision with automatic parent cleanup
 * 
 * EXTRACTED METHODS:
 * - Cube operations: addCube, removeCube, setCubeColor, getCubeAtFast
 * - Subcube operations: subdivideAt, addSubcube, removeSubcube, clearSubdivisionAt
 * - Microcube operations: subdivideSubcubeAt, addMicrocube, removeMicrocube, clearMicrocubesAt
 * - Hash map management: initializeVoxelMaps, addSubcubeToMaps, addMicrocubeToMaps, etc.
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

    // Bulk operations
    void clearAllVoxels();  // Clear the sparse hierarchy maps (subcubeMap, microcubeMap)

    // Cube operations
    bool addCube(const glm::ivec3& localPos);
    // overwrite=false (default): reject if the cell is already occupied (safe placement). overwrite=true:
    // remove an existing SOLID cube and place the new one in its place (useful for structure placement).
    // (Overwriting SUBDIVIDED cells is not yet supported — it still rejects even with overwrite.)
    bool addCube(const glm::ivec3& localPos, const std::string& material, bool overwrite = false);
    // deferRebuild=true: skip the (expensive) per-call chunk re-mesh + GPU upload
    // and instead flag the chunk via setNeedsUpdate, so the per-frame
    // updateDirtyChunks() pass re-meshes each touched chunk exactly ONCE. Used by
    // removeCubeFast / bulk destruction to avoid O(voxels-removed) re-meshes.
    bool removeCube(const glm::ivec3& localPos, bool deferRebuild = false);
    int removeCubesBatch(const std::vector<glm::ivec3>& positions);  // Remove multiple cubes, rebuild once
    int addCubesBatch(const std::vector<glm::ivec3>& positions, const std::string& material = "");  // Add multiple cubes, rebuild once

    // Subcube operations
    bool subdivideAt(const glm::ivec3& localPos);
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const std::string& material = "Default");
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);

    // Microcube operations
    bool subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);
    bool addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const std::string& material = "Default");
    bool removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    bool clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);

    // Hash map management (subdivided voxels only since Phase 4.1 — the cube-keyed
    // addToVoxelMaps/removeFromVoxelMaps/updateVoxelMaps trio is gone with the dense maps
    // they maintained; cube presence/type derive from the `cubes` array on read).
    void initializeVoxelMaps();
    void addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube);
    void removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    void addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube);
    void removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);

    // Voxel location resolution
    VoxelLocation resolveLocalPosition(const glm::ivec3& localPos) const;
    bool hasVoxelAt(const glm::ivec3& localPos) const;
    bool hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;

    // Check if callbacks have been configured
    bool hasCallbacks() const { return static_cast<bool>(m_getCubes); }

    // Fast lookups
    Cube* getCubeAtFast(const glm::ivec3& localPos);
    const Cube* getCubeAtFast(const glm::ivec3& localPos) const;
    
    // Utility
    static size_t subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);

    // Direct hash map access (for ChunkManager and other systems).
    // NOTE: cubeMap/voxelTypeMap getters are gone — both were per-voxel caches of information the
    // dense `cubes` array already answers in O(1) (see kIdx below). They had no external consumers.
    const std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, IVec3Hash>& getSubcubeMap() const { return subcubeMap; }

    // Palette-compressed static voxel state (Phase 4.2a). Currently a read-only MIRROR of the
    // authoritative `cubes` vector, maintained by add/removeCube + initializeVoxelMaps. Exposed
    // so the scan-heavy readers (mesher, occupancy build) can switch to it, and so tests can
    // assert it matches the Cubes. Authority flips in 4.2b, at which point static voxels stop
    // allocating a Cube at all.
    const ChunkVoxelStore& getVoxelStore() const { return voxelStore; }

    // Re-read one voxel from the authoritative Cube into the palette store. Needed by any path
    // that mutates a Cube's material/visible OUTSIDE add/removeCube — currently just the legacy
    // row-per-voxel load in WorldStorage, which addCube()s and then flips visible directly.
    // Without this the mirror silently disagrees with the Cube until the next
    // initializeVoxelMaps() rebuild, which would become a real bug once authority flips (4.2b).
    void syncStoreAt(const glm::ivec3& localPos);

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

    // ── Positional index into the dense cubes array (docs/LargeWorldScalePlan.md Phase 4.1) ──
    // Chunk sizes `cubes` to exactly 32*32*32 and stores each cube at z + y*32 + x*1024, so the
    // array IS the position->Cube index. The former `cubeMap` (a 32k-node hash per solid chunk)
    // and `voxelTypeMap` (another 32k nodes, caching a value updateVoxelMaps already DERIVED from
    // the array + the subcube/microcube maps) were pure duplication — together ~41% of per-chunk
    // RAM at the measured 18.1 MB/chunk. Both are now computed on read: an array index is cheaper
    // than the hash lookup it replaced. subcubeMap/microcubeMap stay: they are genuinely SPARSE
    // (empty for ordinary terrain), so they cost nothing on the chunks that dominate RAM.
    static constexpr size_t kIdx(const glm::ivec3& p) {
        return static_cast<size_t>(p.z) + static_cast<size_t>(p.y) * 32 + static_cast<size_t>(p.x) * 1024;
    }
    static constexpr bool inBounds(const glm::ivec3& p) {
        return p.x >= 0 && p.x < 32 && p.y >= 0 && p.y < 32 && p.z >= 0 && p.z < 32;
    }
    // O(1) position -> Cube*, straight off the dense array (nullptr if out of bounds/empty).
    Cube* cubeAt(const glm::ivec3& localPos) const;

    // Palette-compressed static state for the 32³ cube grid (Phase 4.2a mirror). ~96 KB/chunk
    // against the ~10.5 MB the Cubes cost, so mirroring is <1% while it is being proven out.
    ChunkVoxelStore voxelStore;

    // Sparse hierarchy maps — only populated where a cube is actually subdivided.
    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, IVec3Hash> subcubeMap;
    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Microcube*, IVec3Hash>, IVec3Hash>, IVec3Hash> microcubeMap;
};

} // namespace Phyxel
