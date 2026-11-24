#include "core/ChunkVoxelManager.h"
#include "core/Cube.h"
#include "utils/Logger.h"
#include <algorithm>
#include <random>
#include <unordered_set>

namespace Phyxel {

void ChunkVoxelManager::setCallbacks(
    CubesVectorAccessFunc getCubes,
    WorldOriginAccessFunc getWorldOrigin,
    SetDirtyFunc setDirty,
    SetNeedsUpdateFunc setNeedsUpdate,
    RebuildFacesFunc rebuildFaces,
    AddCollisionFunc addCollision,
    RemoveCollisionFunc removeCollision,
    UpdateNeighborCollisionsFunc updateNeighborCollisions,
    IsInBulkOperationFunc isInBulkOperation,
    std::function<void()> updateVulkanBuffer
) {
    m_getCubes = std::move(getCubes);
    m_getWorldOrigin = std::move(getWorldOrigin);
    m_setDirty = std::move(setDirty);
    m_setNeedsUpdate = std::move(setNeedsUpdate);
    m_rebuildFaces = std::move(rebuildFaces);
    m_addCollision = std::move(addCollision);
    m_removeCollision = std::move(removeCollision);
    m_updateNeighborCollisions = std::move(updateNeighborCollisions);
    m_isInBulkOperation = std::move(isInBulkOperation);
    m_updateVulkanBuffer = std::move(updateVulkanBuffer);
}

// =============================================================================
// BULK OPERATIONS
// =============================================================================

void ChunkVoxelManager::clearAllVoxels() {
    voxelStore.clear();   // Phase 4.2a
}

// 4.2b: the store IS the authority, so there is nothing to rebuild it from — do NOT clear it
// here (this runs after decode/gen, which populated it via addCube). What we do reconcile is
// the other direction: any materialized overlay Cube that a legacy path filled directly into
// the vector gets registered in the store, so overlay ⊆ store holds.
void ChunkVoxelManager::initializeVoxelMaps() {
    auto& cubes = m_getCubes();
    for (size_t i = 0; i < cubes.size() && i < ChunkVoxelStore::kVoxels; ++i) {
        if (const Cube* c = cubes[i].get())
            voxelStore.set(i, c->getMaterialName(), c->isVisible());
    }
}

// =============================================================================
// Voxel location resolution
// =============================================================================

VoxelLocation ChunkVoxelManager::resolveLocalPosition(
    const glm::ivec3& localPos
) const {
    VoxelLocation location;
    // Note: VoxelLocation doesn't store cube pointer - caller must use getCubeAt if needed
    location.type = getVoxelType(localPos);
    return location;
}

bool ChunkVoxelManager::hasVoxelAt(const glm::ivec3& localPos) const {
    return getVoxelType(localPos) != VoxelLocation::EMPTY;
}

// DERIVED (Phase 4.1): a solid cube, else EMPTY (the 1/9 m subcube/microcube store that could
// make a cell SUBDIVIDED was deleted in D3c-5). 4.2b: cube presence answers from the STORE (never the overlay — a store-only voxel is just as
// solid, and this path must never allocate).
VoxelLocation::Type ChunkVoxelManager::getVoxelType(const glm::ivec3& localPos) const {
    if (inBounds(localPos) && voxelStore.solid(kIdx(localPos))) return VoxelLocation::CUBE;
    return VoxelLocation::EMPTY;
}

// Re-read one voxel from its materialized overlay Cube into the palette store. 4.2b: a missing
// overlay Cube means "no local mutation to pick up" — it must NOT erase the store entry (the
// store is the authority; store-only voxels are normal).
void ChunkVoxelManager::syncStoreAt(const glm::ivec3& localPos) {
    if (!inBounds(localPos)) return;
    if (const Cube* c = cubeAt(localPos)) {
        voxelStore.set(kIdx(localPos), c->getMaterialName(), c->isVisible());
    }
}

void ChunkVoxelManager::setCubeVisible(const glm::ivec3& localPos, bool visible) {
    if (!inBounds(localPos)) return;
    const size_t index = kIdx(localPos);
    if (!voxelStore.solid(index)) return;
    voxelStore.setVisible(index, visible);
    if (Cube* c = cubeAt(localPos)) c->setVisible(visible);   // write-through to the overlay
    m_setDirty(true);
}

void ChunkVoxelManager::fillAllVoxels(const std::string& material) {
    voxelStore.fillUniform(material, /*visible=*/true);   // O(1) once 4.4 stage 1 lands
    m_setDirty(true);
}

// The dense cubes array IS the position index (see ChunkVoxelManager.h) — an array read replaces
// the former cubeMap hash lookup. RAW read: nullptr for air AND for store-only voxels.
Cube* ChunkVoxelManager::cubeAt(const glm::ivec3& localPos) const {
    if (!inBounds(localPos) || !m_getCubes) return nullptr;
    auto& cubes = m_getCubes();
    const size_t index = kIdx(localPos);
    if (index >= cubes.size()) return nullptr;
    return cubes[index].get();
}

// 4.2b materialize-on-demand: back-fill a heap Cube for a store voxel the first time a caller
// asks for the Cube* API. The Cube carries the store's material/visible; from then on it is the
// overlay that wins at scan sites, so callers may mutate it directly (physics/damage do).
Cube* ChunkVoxelManager::materializeAt(const glm::ivec3& localPos) const {
    if (Cube* existing = cubeAt(localPos)) return existing;
    if (!inBounds(localPos) || !m_getCubes) return nullptr;
    const size_t index = kIdx(localPos);
    if (!voxelStore.solid(index)) return nullptr;   // air never materializes

    auto& cubes = m_getCubes();
    if (cubes.size() < ChunkVoxelStore::kVoxels)
        cubes.resize(ChunkVoxelStore::kVoxels);     // lazy: untouched chunks skip the slot array
    auto cube = std::make_unique<Cube>(localPos, voxelStore.material(index));
    cube->setVisible(voxelStore.visible(index));
    cubes[index] = std::move(cube);
    return cubes[index].get();
}

Cube* ChunkVoxelManager::getCubeAtFast(const glm::ivec3& localPos) {
    return materializeAt(localPos);
}

const Cube* ChunkVoxelManager::getCubeAtFast(const glm::ivec3& localPos) const {
    return materializeAt(localPos);
}

// =============================================================================
// Helper functions for voxel access
// =============================================================================

Cube* ChunkVoxelManager::getCubeHelper(const glm::ivec3& localPos) const {
    return materializeAt(localPos);
}

// =============================================================================
// Cube operations
// =============================================================================

bool ChunkVoxelManager::addCube(
    const glm::ivec3& localPos
) {
    return addCube(localPos, "");
}

bool ChunkVoxelManager::addCube(
    const glm::ivec3& localPos,
    const std::string& material,
    bool overwrite
) {
    // Validate position
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }

    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;

    // Occupied-cell handling. Default: reject (safe "place only if empty"). overwrite=true: remove an
    // existing SOLID cube and fall through to place the new one in its place.
    if (voxelStore.solid(index)) {
        if (!overwrite) return false;
        removeCube(localPos, /*deferRebuild=*/true);   // clears store + any overlay Cube + maps + collision
    }

    // Phase 4.2b: static voxels live in the palette store ONLY — no Cube allocation. A Cube is
    // materialized later only if a caller asks for per-voxel physics/damage state (getCubeAtFast).
    voxelStore.set(index, material.empty() ? std::string("Default") : material, /*visible=*/true);

    // Mark chunk as dirty for smart saving
    m_setDirty(true);
    
    // Add collision shape with reference counting (skipped in bulk loading; endBulkOperation
    // rebuilds all collision once).
    if (!m_isInBulkOperation()) {
        m_addCollision(localPos);
        // Only update neighbors during individual operations, not bulk loading
        m_updateNeighborCollisions(localPos);
    }
    
    m_setNeedsUpdate(true);
    
    return true;
}

bool ChunkVoxelManager::removeCube(
    const glm::ivec3& localPos,
    bool deferRebuild
) {
    // Validate position
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    if (!voxelStore.solid(index)) return false;

    // Drop the store entry AND any materialized overlay Cube (4.2b: the store is authoritative;
    // the overlay slot may not even exist for never-touched terrain).
    voxelStore.erase(index);
    auto& cubes = m_getCubes();
    if (index < cubes.size()) cubes[index].reset();
    
    // Remove collision shape with proper memory management. Deferred in BULK mode like the
    // add side is: applying removes but skipping adds turned every replace-write on a bulk-mode
    // chunk into a collision hole (Ravenmere barrow floor, G-98); endBulkOperation rebuilds.
    if (!m_isInBulkOperation()) m_removeCollision(localPos);
    
    // Update collision shapes of neighboring cubes that might now be exposed
    m_updateNeighborCollisions(localPos);
    
    // Mark chunk as dirty for smart saving
    m_setDirty(true);
    LOG_DEBUG_FMT("ChunkVoxelManager", "Removed cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z
              << ") - Chunk now DIRTY for save");

    if (deferRebuild) {
        // Defer the expensive re-mesh: flag the chunk so the per-frame
        // updateDirtyChunks() pass rebuilds it ONCE, no matter how many voxels
        // were removed this op. Avoids O(voxels) full-chunk re-meshes + uploads.
        m_setNeedsUpdate(true);
    } else {
        // Immediately rebuild faces to remove the cube from GPU buffer
        m_rebuildFaces();
        m_updateVulkanBuffer();
    }

    return true;
}

int ChunkVoxelManager::removeCubesBatch(const std::vector<glm::ivec3>& positions) {
    auto& cubes = m_getCubes();
    int removed = 0;

    for (const auto& localPos : positions) {
        if (localPos.x < 0 || localPos.x >= 32 ||
            localPos.y < 0 || localPos.y >= 32 ||
            localPos.z < 0 || localPos.z >= 32) {
            continue;
        }
        size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
        if (!voxelStore.solid(index)) continue;

        voxelStore.erase(index);
        if (index < cubes.size()) cubes[index].reset();   // drop any materialized overlay
        if (!m_isInBulkOperation()) m_removeCollision(localPos);   // symmetric with the add side (G-98)
        ++removed;
    }

    if (removed > 0) {
        m_setDirty(true);
        m_rebuildFaces();
        m_updateVulkanBuffer();
    }
    return removed;
}

int ChunkVoxelManager::addCubesBatch(const std::vector<glm::ivec3>& positions, const std::string& material) {
    int added = 0;

    for (const auto& localPos : positions) {
        if (localPos.x < 0 || localPos.x >= 32 ||
            localPos.y < 0 || localPos.y >= 32 ||
            localPos.z < 0 || localPos.z >= 32) {
            continue;
        }
        size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
        if (voxelStore.solid(index)) {
            continue; // occupied — skip overlap
        }
        // 4.2b: store-only write, no Cube allocation.
        voxelStore.set(index, material.empty() ? std::string("Default") : material, /*visible=*/true);
        m_addCollision(localPos);
        ++added;
    }

    if (added > 0) {
        m_setDirty(true);
        m_setNeedsUpdate(true);
        m_rebuildFaces();
        m_updateVulkanBuffer();
    }
    return added;
}

int ChunkVoxelManager::clearCellsBulk(const std::vector<glm::ivec3>& localCells) {
    // Bulk cell clear: one pass for the whole cell set. Caller owns occupancy + chunk remesh.
    int cleared = 0;
    auto& cubes = m_getCubes();
    for (const auto& lp : localCells) {
        if (lp.x < 0 || lp.x >= 32 || lp.y < 0 || lp.y >= 32 ||
            lp.z < 0 || lp.z >= 32) continue;
        const size_t idx = lp.z + lp.y * 32 + lp.x * 32 * 32;
        // 4.2b port (merge integration): the palette store is the cube authority — the
        // dense cubeMap/voxelTypeMap this loop originally erased were removed in Phase
        // 4.1/4.2b. Clear the store cell AND the materialized overlay Cube (if any).
        if (voxelStore.solid(idx)) voxelStore.erase(idx);
        if (idx < cubes.size() && cubes[idx]) cubes[idx].reset();
        m_removeCollision(lp);
        ++cleared;
    }
    if (cleared == 0) return 0;

    m_setNeedsUpdate(true);
    m_setDirty(true);
    return cleared;
}

} // namespace Phyxel
