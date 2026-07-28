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
    /// Bulk cell clear (cube + subdivision content) — one pass over the chunk's
    /// storage for the whole set; caller owns occupancy + remesh. Returns cells cleared.
    int clearCellsBulk(const std::vector<glm::ivec3>& localCells);

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

    // ── SUB-VOXEL FLOOR (WaterSystemV3 Phase 4B) ──────────────────────────────────────────────
    // How far up this voxel is filled by a PURE HORIZONTAL sub-voxel floor, as a fraction of the
    // voxel (0, 1/3, 2/3 for subcubes; ninths for microcubes). Lets water sit ON a low platform
    // instead of hovering a whole voxel above it.
    //
    // Returns a NEGATIVE value when the voxel must be treated as fully solid: a real cube, or
    // subdivided content that is NOT just flat layers stacked from the bottom. That conservative
    // rule is load-bearing — a 1-subcube-thick wall or a thin microcube eave sheet has no full
    // horizontal layer, so calling it "floor 0, therefore passable" would let water pour through
    // walls and flood interiors that are watertight today. Only a genuine platform opens up.
    //
    // O(1) in the voxel count: one hash lookup for the voxel, then at most a few dozen lookups
    // inside its own sparse sub-map. (Do NOT implement this over Chunk::getSubcubesAt — that
    // linear-scans every subcube in the chunk.)
    static constexpr float kSolidFloor = -1.0f;
    float subVoxelFloor(const glm::ivec3& localPos) const;
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;

    // Check if callbacks have been configured
    bool hasCallbacks() const { return static_cast<bool>(m_getCubes); }

    // Fast lookups. Since Phase 4.2b these MATERIALIZE: a solid voxel that lives only in the
    // palette store gets a heap Cube allocated on first access (carrying the store's
    // material/visible), so the ~158 existing Cube* callers keep working unchanged. Presence
    // questions must go through hasVoxelAt/getVoxelType instead — those never allocate.
    Cube* getCubeAtFast(const glm::ivec3& localPos);
    const Cube* getCubeAtFast(const glm::ivec3& localPos) const;

    // Set a solid cube voxel's visible flag without materializing (store write + write-through
    // to an already-materialized overlay Cube). The legacy row-per-voxel DB load uses this.
    void setCubeVisible(const glm::ivec3& localPos, bool visible);

    // Fill every cell with a solid cube of `material` — store-only, no Cube allocations
    // (Chunk::populateWithCubes).
    void fillAllVoxels(const std::string& material);
    
    // Utility
    static size_t subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);

    // Direct hash map access (for ChunkManager and other systems).
    // NOTE: cubeMap/voxelTypeMap getters are gone — both were per-voxel caches of information the
    // dense `cubes` array already answers in O(1) (see kIdx below). They had no external consumers.
    const std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, IVec3Hash>& getSubcubeMap() const { return subcubeMap; }

    // Palette-compressed static voxel state — THE AUTHORITY for cube presence/material/visible
    // since Phase 4.2b. addCube writes here and allocates nothing; the dense `cubes` vector is a
    // sparse MATERIALIZED OVERLAY holding heap Cubes only where a caller asked for one (physics/
    // damage state). Invariants: (1) every solid cube voxel is in the store, materialized or
    // not; (2) where an overlay Cube exists, ITS material/visible win at every scan site (the
    // hybrid read) — so direct Cube* mutation stays correct even without an explicit sync.
    const ChunkVoxelStore& getVoxelStore() const { return voxelStore; }

    // Re-read one voxel from its materialized overlay Cube into the palette store. Optional
    // hardening after direct Cube* mutation: scan sites already prefer the overlay Cube (hybrid
    // read), but syncing keeps the store truthful for store-only readers.
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
    // O(1) position -> materialized overlay Cube*, straight off the dense array. Returns nullptr
    // for air AND for store-only voxels — this is the RAW read; it never allocates. Presence
    // must be answered by the store.
    Cube* cubeAt(const glm::ivec3& localPos) const;

    // Materialize the overlay Cube for a solid store voxel (allocates on first access; returns
    // the existing Cube on repeat access; nullptr for air). Backs getCubeAtFast/getCubeHelper.
    // Lazily sizes the `cubes` vector to 32768 slots, so untouched chunks don't even pay the
    // 256 KB pointer array.
    Cube* materializeAt(const glm::ivec3& localPos) const;

    // Palette-compressed static state for the 32³ cube grid — authoritative (Phase 4.2b).
    // ~96 KB/chunk against the ~7.6 MB the per-voxel Cubes cost.
    ChunkVoxelStore voxelStore;

    // Sparse hierarchy maps — only populated where a cube is actually subdivided.
    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, IVec3Hash> subcubeMap;
    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, Microcube*, IVec3Hash>, IVec3Hash>, IVec3Hash> microcubeMap;
};

} // namespace Phyxel
