#include "core/ChunkVoxelManager.h"
#include "core/Cube.h"
#include "utils/Logger.h"
#include <algorithm>
#include <random>
#include <unordered_set>

namespace Phyxel {

// =============================================================================
// COORDINATE UTILITIES
// =============================================================================
// These functions convert 3D coordinates to unique indices for hash map storage

/**
 * Convert subcube position to unique flat index
 * 
 * INDEXING STRATEGY:
 * Each chunk has 32x32x32 possible cube positions (32,768 cubes max)
 * Each cube can be subdivided into 3x3x3 subcubes (27 subcubes per cube)
 * Total possible subcubes: 32,768 * 27 = 884,736
 * 
 * CALCULATION:
 * 1. parentIndex = z + y*32 + x*32*32 (cube's flat index in chunk)
 * 2. subcubeOffset = sx + sy*3 + sz*9 (subcube's offset within parent, 0-26)
 * 3. finalIndex = parentIndex * 27 + subcubeOffset
 * 
 * WHY UNIQUE?
 * Each parent cube occupies a distinct range of 27 indices:
 * - Cube (0,0,0): indices 0-26
 * - Cube (0,0,1): indices 27-53
 * - Cube (1,0,0): indices 884,736 - 884,762
 * No overlap possible!
 * 
 * @param parentPos Local position of parent cube (0-31, 0-31, 0-31)
 * @param subcubePos Local position within parent (0-2, 0-2, 0-2)
 * @return Unique flat index (0 to 884,735)
 */
size_t ChunkVoxelManager::subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Parent cube's flat index in chunk (0 to 32,767)
    size_t parentIndex = parentPos.z + parentPos.y * 32 + parentPos.x * 32 * 32;
    
    // Subcube's offset within parent cube (0 to 26)
    size_t subcubeOffset = subcubePos.x + subcubePos.y * 3 + subcubePos.z * 9;
    
    // Final unique index combining both
    return parentIndex * 27 + subcubeOffset;
}

void ChunkVoxelManager::setCallbacks(
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
) {
    m_getCubes = std::move(getCubes);
    m_getStaticSubcubes = std::move(getStaticSubcubes);
    m_getStaticMicrocubes = std::move(getStaticMicrocubes);
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
    subcubeMap.clear();
    microcubeMap.clear();
    voxelStore.clear();   // Phase 4.2a
}

// =============================================================================
// HASH MAP MANAGEMENT
// =============================================================================
// These functions maintain the SPARSE hierarchy maps for O(1) subdivided-voxel lookups.
//
// DATA STRUCTURES:
// - subcubeMap: { cubePos -> { subcubePos -> Subcube* } }  // Two-level: cube then subcube
// - microcubeMap: { cubePos -> { subcubePos -> { microPos -> Microcube* } } }  // Three-level
//
// Both are SPARSE — only populated where a cube is actually subdivided, so ordinary terrain
// chunks (which dominate RAM) carry none of this.
//
// REMOVED (Phase 4.1, docs/LargeWorldScalePlan.md): cubeMap { localPos -> Cube* } and
// voxelTypeMap { localPos -> VoxelType }. Both were DENSE — up to 32,768 heap nodes per chunk
// each — and both were pure duplication: `cubes` is already indexed by position
// (z + y*32 + x*1024), so cubeMap restated an array read, and voxelTypeMap cached a value that
// updateVoxelMaps already derived from the array + these two maps. They are computed on read now
// (cubeAt / getVoxelType); an array index beats the hash lookup it replaced. The old warning
// below still matters for what remains:
//
// CONSISTENCY:
// ALL add/remove operations MUST update BOTH the vector AND the hash maps.
// Stale hash map entries cause crashes (dangling pointers) or incorrect rendering.

/**
 * Add subcube to two-level hash map
 * Call this whenever adding a subcube to staticSubcubes vector
 * 
 * STRUCTURE: subcubeMap[cubePos][subcubePos] = Subcube*
 * - First level: parent cube position
 * - Second level: subcube position within parent (0-2, 0-2, 0-2)
 * 
 * VOXEL TYPE: Marks position as SUBDIVIDED (contains subcubes, not a solid cube)
 */
void ChunkVoxelManager::addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube) {
    if (subcube) {
        subcubeMap[localPos][subcubePos] = subcube;           // Two-level lookup
    }
}

/**
 * Remove subcube from two-level hash map with automatic cleanup
 * 
 * CLEANUP LOGIC:
 * If removing the last subcube at a cube position, erase the entire parent entry.
 * This prevents empty hash map entries from accumulating (memory leak).
 * 
 * Example:
 * - Before: subcubeMap[10,5,3] = { (0,0,0)->Subcube1, (1,0,0)->Subcube2 }
 * - Remove (0,0,0): subcubeMap[10,5,3] = { (1,0,0)->Subcube2 }
 * - Remove (1,0,0): subcubeMap[10,5,3] is ERASED entirely (empty map)
 */
void ChunkVoxelManager::removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    auto it = subcubeMap.find(localPos);
    if (it != subcubeMap.end()) {
        it->second.erase(subcubePos);  // Remove specific subcube
        if (it->second.empty()) {
            subcubeMap.erase(localPos);  // Cleanup: remove empty parent entry
        }
    }
}

/**
 * Add microcube to three-level hash map
 * 
 * STRUCTURE: microcubeMap[cubePos][subcubePos][microcubePos] = Microcube*
 * - First level: parent cube position in chunk
 * - Second level: subcube position within cube (0-2, 0-2, 0-2)
 * - Third level: microcube position within subcube (0-2, 0-2, 0-2)
 * 
 * WHY THREE LEVELS?
 * Microcubes are 1/9 scale (1/3 * 1/3), requiring triple hierarchy:
 * Chunk -> Cube (32x32x32) -> Subcube (3x3x3) -> Microcube (3x3x3)
 * 
 * TOTAL CAPACITY:
 * 32,768 cubes * 27 subcubes/cube * 27 microcubes/subcube = 23,887,872 microcubes max!
 */
void ChunkVoxelManager::addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube) {
    if (microcube) {
        microcubeMap[cubePos][subcubePos][microcubePos] = microcube;  // Three-level lookup
    }
}

/**
 * Remove microcube from three-level hash map with cascading cleanup
 * 
 * CLEANUP LOGIC (cascading):
 * 1. Erase microcube from innermost map
 * 2. If subcube map becomes empty, erase entire subcube entry
 * 3. If cube map becomes empty, erase entire cube entry
 * 
 * This prevents memory leaks from empty nested maps accumulating.
 * 
 * Example:
 * microcubeMap[5,5,5][1,1,1][0,0,0] exists
 * After removal: entire chain is cleaned if it was the last microcube
 */
void ChunkVoxelManager::removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            subcubeIt->second.erase(microcubePos);  // Level 3: erase microcube
            if (subcubeIt->second.empty()) {
                cubeIt->second.erase(subcubePos);   // Level 2: cleanup empty subcube map
            }
        }
        if (cubeIt->second.empty()) {
            microcubeMap.erase(cubePos);            // Level 1: cleanup empty cube map
        }
    }
}

/**
 * Initialize all hash maps from voxel vectors
 * 
 * WHEN TO CALL:
 * - After loading chunk from disk (WorldStorage)
 * - After major structural changes requiring full rebuild
 * - During chunk initialization
 * 
 * ALGORITHM:
 * 1. Clear the sparse hierarchy maps (fresh start)
 * 2. Iterate through staticSubcubes vector, populate subcubeMap
 * 3. Iterate through staticMicrocubes vector, populate microcubeMap
 *
 * The cubes pass is gone (Phase 4.1): `cubes` is already the position index, so there is no
 * cube-keyed map to rebuild — which also means loading a chunk no longer allocates 32k hash
 * nodes before it can be queried.
 * 
 * PERFORMANCE:
 * O(n) where n = total voxels, but only called occasionally (not every frame)
 * Typical: ~100-1000 voxels per chunk, takes <1ms
 * 
 * COORDINATE CONVERSION:
 * Subcubes/microcubes store world positions, but hash maps use local positions.
 * Conversion: localPos = worldPos - worldOrigin
 */
void ChunkVoxelManager::initializeVoxelMaps() {
    // Clear existing maps (fresh start - prevents stale entries)
    subcubeMap.clear();
    microcubeMap.clear();

    // 4.2b: the store IS the authority, so there is nothing to rebuild it from — do NOT clear it
    // here (this runs after decode/gen, which populated it via addCube). What we do reconcile is
    // the other direction: any materialized overlay Cube that a legacy path filled directly into
    // the vector gets registered in the store, so overlay ⊆ store holds.
    {
        auto& cubes = m_getCubes();
        for (size_t i = 0; i < cubes.size() && i < ChunkVoxelStore::kVoxels; ++i) {
            if (const Cube* c = cubes[i].get())
                voxelStore.set(i, c->getMaterialName(), c->isVisible());
        }
    }

    glm::ivec3 worldOrigin = m_getWorldOrigin();

    // Build subcubeMap from static subcubes
    auto& staticSubcubes = m_getStaticSubcubes();
    for (const auto& subcube : staticSubcubes) {
        if (subcube) {
            glm::ivec3 parentWorldPos = subcube->getPosition();
            glm::ivec3 localPos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = subcube->getLocalPosition();
            subcubeMap[localPos][subcubePos] = subcube.get();
        }
    }

    // Build microcubeMap from static microcubes
    auto& staticMicrocubes = m_getStaticMicrocubes();
    for (const auto& microcube : staticMicrocubes) {
        if (microcube) {
            glm::ivec3 parentWorldPos = microcube->getParentCubePosition();
            glm::ivec3 cubePos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
            glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
            microcubeMap[cubePos][subcubePos][microcubePos] = microcube.get();
        }
    }

    LOG_DEBUG_FMT("ChunkVoxelManager", "Initialized voxel maps: "
              << subcubeMap.size() << " subdivided positions, "
              << microcubeMap.size() << " microcube positions");
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

bool ChunkVoxelManager::hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    auto it = subcubeMap.find(localPos);
    if (it != subcubeMap.end()) {
        return it->second.find(subcubePos) != it->second.end();
    }
    return false;
}

// DERIVED (Phase 4.1): this is exactly what updateVoxelMaps used to compute before caching it in
// voxelTypeMap — a solid cube wins, else any subcube/microcube presence means SUBDIVIDED, else
// EMPTY. Every writer paired its subcubeMap/microcubeMap insert with the type write in the same
// call (addSubcubeToMaps/addMicrocubeToMaps), so deriving on read is equivalent, not racy.
// 4.2b: cube presence answers from the STORE (never the overlay — a store-only voxel is just as
// solid, and this path must never allocate).
VoxelLocation::Type ChunkVoxelManager::getVoxelType(const glm::ivec3& localPos) const {
    if (inBounds(localPos) && voxelStore.solid(kIdx(localPos))) return VoxelLocation::CUBE;
    auto subIt = subcubeMap.find(localPos);
    if (subIt != subcubeMap.end() && !subIt->second.empty()) return VoxelLocation::SUBDIVIDED;
    auto microIt = microcubeMap.find(localPos);
    if (microIt != microcubeMap.end() && !microIt->second.empty()) return VoxelLocation::SUBDIVIDED;
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

Subcube* ChunkVoxelManager::getSubcubeHelper(
    const glm::ivec3& localPos, 
    const glm::ivec3& subcubePos
) const {
    // Try hash map lookup first
    auto it = subcubeMap.find(localPos);
    if (it != subcubeMap.end()) {
        auto subcubeIt = it->second.find(subcubePos);
        if (subcubeIt != it->second.end()) {
            return subcubeIt->second;
        }
    }
    
    // Fallback: linear search (slower, but handles inconsistent state)
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticSubcubes = m_getStaticSubcubes();
    for (const auto& subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube.get();
        }
    }
    return nullptr;
}

std::vector<Subcube*> ChunkVoxelManager::getSubcubesHelper(
    const glm::ivec3& localPos
) const {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + localPos;
    
    auto& staticSubcubes = m_getStaticSubcubes();
    for (const auto& subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube.get());
        }
    }
    return result;
}

Microcube* ChunkVoxelManager::getMicrocubeHelper(
    const glm::ivec3& cubePos, 
    const glm::ivec3& subcubePos, 
    const glm::ivec3& microcubePos
) const {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            auto microcubeIt = subcubeIt->second.find(microcubePos);
            if (microcubeIt != subcubeIt->second.end()) {
                return microcubeIt->second;
            }
        }
    }
    return nullptr;
}

std::vector<Microcube*> ChunkVoxelManager::getMicrocubesHelper(
    const glm::ivec3& cubePos, 
    const glm::ivec3& subcubePos
) const {
    std::vector<Microcube*> result;
    
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            for (const auto& microcubePair : subcubeIt->second) {
                if (microcubePair.second) {
                    result.push_back(microcubePair.second);
                }
            }
        }
    }
    
    return result;
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
    } else if (getVoxelType(localPos) == VoxelLocation::SUBDIVIDED) {
        // Overwriting subdivided cells (clearing all sub/microcubes) is not yet supported — reject
        // even with overwrite. TODO: add subdivided clear-and-replace when structure placement needs it.
        return false;
    }

    // Phase 4.2b: static voxels live in the palette store ONLY — no Cube allocation. A Cube is
    // materialized later only if a caller asks for per-voxel physics/damage state (getCubeAtFast).
    voxelStore.set(index, material.empty() ? std::string("Default") : material, /*visible=*/true);

    // Mark chunk as dirty for smart saving
    m_setDirty(true);
    
    // Add collision shape with reference counting (skipped in bulk loading; endBulkOperation
    // rebuilds all collision once — mirrors addSubcube/addMicrocube, fixes the cube-path perf gap).
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
    
    // Remove collision shape with proper memory management
    m_removeCollision(localPos);
    
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
        m_removeCollision(localPos);
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
        if (getVoxelType(localPos) == VoxelLocation::SUBDIVIDED) {
            continue; // subdivided voxels here — skip overlap
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

// =============================================================================
// Subcube operations
// =============================================================================

bool ChunkVoxelManager::subdivideAt(
    const glm::ivec3& localPos
) {
    // Check if position is valid
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    // The cell must hold a solid cube (store is the presence authority — no materialization
    // needed just to subdivide).
    const size_t cubeIndex = kIdx(localPos);
    if (!voxelStore.solid(cubeIndex)) return false;

    // Check if already subdivided
    auto existingSubcubes = getSubcubesHelper(localPos);
    if (!existingSubcubes.empty()) return false;

    // Inherit material from the parent voxel (overlay Cube wins if one was materialized and
    // mutated; else the store).
    const Cube* overlay = cubeAt(localPos);
    std::string material = overlay ? overlay->getMaterialName() : voxelStore.material(cubeIndex);
    
    // Create 27 subcubes (3x3x3)
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    auto& staticSubcubes = m_getStaticSubcubes();
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 subcubeLocalPos(x, y, z);
                
                auto newSubcube = std::make_unique<Subcube>(parentWorldPos, subcubeLocalPos, material);
                Subcube* rawPtr = newSubcube.get();
                staticSubcubes.push_back(std::move(newSubcube));
                
                // Update hash maps for each subcube
                addSubcubeToMaps(localPos, subcubeLocalPos, rawPtr);
            }
        }
    }
    
    // Delete the parent cube completely — store entry AND any materialized overlay Cube.
    // (Pre-4.2b this reset only the Cube slot and left a stale solid entry in the store —
    // the mirror gap ChunkVoxelAuthority.SubdivideAtErasesStoreEntry pins down.)
    voxelStore.erase(cubeIndex);
    {
        auto& cubes = m_getCubes();
        if (cubeIndex < cubes.size()) cubes[cubeIndex].reset();
    }
    LOG_DEBUG_FMT("ChunkVoxelManager", "Removed parent cube at ("
              << localPos.x << "," << localPos.y << "," << localPos.z
              << ") - replaced by 27 subcubes");


    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::addSubcube(
    const glm::ivec3& parentPos,
    const glm::ivec3& subcubePos,
    const std::string& material
) {
    // Check if position is valid
    if (parentPos.x < 0 || parentPos.x >= 32 ||
        parentPos.y < 0 || parentPos.y >= 32 ||
        parentPos.z < 0 || parentPos.z >= 32) {
        LOG_DEBUG("VoxelManager", "addSubcube FAIL: parentPos({},{},{}) out of bounds",
                  parentPos.x, parentPos.y, parentPos.z);
        return false;
    }
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) {
        LOG_DEBUG("VoxelManager", "addSubcube FAIL: subcubePos({},{},{}) out of bounds",
                  subcubePos.x, subcubePos.y, subcubePos.z);
        return false;
    }
    
    // Reject if a solid cube occupies this cube position (store is the presence authority)
    if (voxelStore.solid(kIdx(parentPos))) {
        return false;
    }

    // Check if subcube already exists
    if (getSubcubeHelper(parentPos, subcubePos)) {
        LOG_DEBUG("VoxelManager", "addSubcube FAIL: subcube already exists at parent({},{},{}) sub({},{},{})",
                  parentPos.x, parentPos.y, parentPos.z, subcubePos.x, subcubePos.y, subcubePos.z);
        return false;
    }

    // Create new subcube
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + parentPos;
    auto newSubcube = std::make_unique<Subcube>(parentWorldPos, subcubePos, material);
    Subcube* rawPtr = newSubcube.get();
    auto& staticSubcubes = m_getStaticSubcubes();
    staticSubcubes.push_back(std::move(newSubcube));
    
    // Update hash maps
    addSubcubeToMaps(parentPos, subcubePos, rawPtr);

    // Update collision shape (skipped during bulk template spawn)
    if (!m_isInBulkOperation()) {
        m_addCollision(parentPos);
    }

    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::removeSubcube(
    const glm::ivec3& parentPos,
    const glm::ivec3& subcubePos
) {
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticSubcubes = m_getStaticSubcubes();
    
    // Try to find and remove from static subcubes
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        Subcube* subcube = it->get();
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            staticSubcubes.erase(it);
            
            // Update hash maps BEFORE checking remaining subcubes
            removeSubcubeFromMaps(parentPos, subcubePos);
            
            // Check if any subcubes remain at this parent position
            std::vector<Subcube*> remainingSubcubes = getSubcubesHelper(parentPos);
            
            if (remainingSubcubes.empty()) {
                // No more subcubes - remove collision shape entirely
                LOG_TRACE_FMT("ChunkVoxelManager", "No subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - removing collision shape");
                m_removeCollision(parentPos);
                
                // Position becomes empty
                LOG_DEBUG_FMT("ChunkVoxelManager", "[VOXEL MAP] Position now empty at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - all subcubes removed");
            } else {
                // Still have subcubes - update collision shape
                LOG_DEBUG_FMT("ChunkVoxelManager", "[COLLISION] " << remainingSubcubes.size() 
                          << " subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - updating collision shape");
                m_removeCollision(parentPos);
                m_addCollision(parentPos);
                
                LOG_DEBUG_FMT("ChunkVoxelManager", "[VOXEL MAP] Maintained SUBDIVIDED type at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - " << remainingSubcubes.size() << " subcubes remain");
            }
            
            m_setNeedsUpdate(true);
            m_setDirty(true);
            return true;
        }
    }
    
    return false;
}

int ChunkVoxelManager::clearCellsBulk(const std::vector<glm::ivec3>& localCells) {
    // Bulk cell clear: ONE remove_if pass over the chunk's subcube/microcube
    // storage for the whole cell set. clearSubdivisionAt scans + erase()s the
    // full vectors PER CELL — clearing a felled tree's ~550 cells that way cost
    // ~758 ms (the topple-start hitch). Caller owns occupancy + chunk remesh.
    auto key = [](const glm::ivec3& p) { return (p.x << 10) | (p.y << 5) | p.z; };
    std::unordered_set<int> want;
    int cleared = 0;
    auto& cubes = m_getCubes();
    for (const auto& lp : localCells) {
        if (lp.x < 0 || lp.x >= 32 || lp.y < 0 || lp.y >= 32 ||
            lp.z < 0 || lp.z >= 32) continue;
        want.insert(key(lp));
        const size_t idx = lp.z + lp.y * 32 + lp.x * 32 * 32;
        if (idx < cubes.size() && cubes[idx]) {
            cubeMap.erase(lp);
            cubes[idx].reset();
        }
        subcubeMap.erase(lp);
        microcubeMap.erase(lp);
        voxelTypeMap.erase(lp);
        m_removeCollision(lp);
        ++cleared;
    }
    if (cleared == 0) return 0;

    const glm::ivec3 origin = m_getWorldOrigin();
    auto inWant = [&](const glm::ivec3& parentWorldPos) {
        const glm::ivec3 lp = parentWorldPos - origin;
        if (lp.x < 0 || lp.x >= 32 || lp.y < 0 || lp.y >= 32 ||
            lp.z < 0 || lp.z >= 32) return false;
        return want.count(key(lp)) > 0;
    };
    auto& subs = m_getStaticSubcubes();
    subs.erase(std::remove_if(subs.begin(), subs.end(),
                              [&](const std::unique_ptr<Subcube>& s) {
                                  return s && inWant(s->getPosition());
                              }),
               subs.end());
    auto& mics = m_getStaticMicrocubes();
    mics.erase(std::remove_if(mics.begin(), mics.end(),
                              [&](const std::unique_ptr<Microcube>& m) {
                                  return m && inWant(m->getParentCubePosition());
                              }),
               mics.end());

    m_setNeedsUpdate(true);
    m_setDirty(true);
    return cleared;
}

bool ChunkVoxelManager::clearSubdivisionAt(
    const glm::ivec3& localPos
) {
    // Check if position is valid
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    // Remove all static subcubes at this position
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + localPos;
    auto& staticSubcubes = m_getStaticSubcubes();
    auto it = staticSubcubes.begin();
    bool removedAny = false;
    
    while (it != staticSubcubes.end()) {
        auto& subcube = *it;
        if (subcube && subcube->getPosition() == parentWorldPos) {
            it = staticSubcubes.erase(it);
            removedAny = true;
        } else {
            ++it;
        }
    }
    
    // Remove all static microcubes at this cube position
    auto& staticMicrocubes = m_getStaticMicrocubes();
    auto mit = staticMicrocubes.begin();
    while (mit != staticMicrocubes.end()) {
        auto& microcube = *mit;
        if (microcube && microcube->getParentCubePosition() == parentWorldPos) {
            mit = staticMicrocubes.erase(mit);
            removedAny = true;
        } else {
            ++mit;
        }
    }
    
    // Clear the subdivision state from data structures
    subcubeMap.erase(localPos);
    microcubeMap.erase(localPos);
    
    if (removedAny) {
        m_removeCollision(localPos);
        LOG_DEBUG_FMT("ChunkVoxelManager", "Cleared subdivision at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - position now empty");
        m_setNeedsUpdate(true);
        m_setDirty(true);
    }
    
    return removedAny;
}

// =============================================================================
// Microcube operations
// =============================================================================

bool ChunkVoxelManager::subdivideSubcubeAt(
    const glm::ivec3& cubePos,
    const glm::ivec3& subcubePos
) {
    // Check if position is valid
    if (cubePos.x < 0 || cubePos.x >= 32 ||
        cubePos.y < 0 || cubePos.y >= 32 ||
        cubePos.z < 0 || cubePos.z >= 32) {
        return false;
    }
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) {
        return false;
    }
    
    // Get the subcube at this position
    Subcube* subcube = getSubcubeHelper(cubePos, subcubePos);
    if (!subcube) return false;
    
    // Check if already subdivided into microcubes
    auto existingMicrocubes = getMicrocubesHelper(cubePos, subcubePos);
    if (!existingMicrocubes.empty()) return false;
    
    // Inherit material from parent subcube
    std::string material = subcube->getMaterialName();
    
    // Create 27 microcubes (3x3x3)
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + cubePos;
    
    auto& staticMicrocubes = m_getStaticMicrocubes();
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 microcubeLocalPos(x, y, z);
                
                auto newMicrocube = std::make_unique<Microcube>(parentWorldPos, subcubePos, microcubeLocalPos, material);
                Microcube* rawPtr = newMicrocube.get();
                staticMicrocubes.push_back(std::move(newMicrocube));
                
                // Update hash maps for O(1) hover detection
                addMicrocubeToMaps(cubePos, subcubePos, microcubeLocalPos, rawPtr);
            }
        }
    }
    
    // Delete the parent subcube completely
    removeSubcubeFromMaps(cubePos, subcubePos);
    
    // Remove from staticSubcubes vector
    auto& staticSubcubes = m_getStaticSubcubes();
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        if (it->get() == subcube) {
            staticSubcubes.erase(it);
            LOG_DEBUG_FMT("ChunkVoxelManager", "Completely removed parent subcube at cube (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                      << ") - replaced by 27 microcubes");
            break;
        }
    }
    
    // Update collision shape to create microcube collision entities
    LOG_DEBUG_FMT("ChunkVoxelManager", "[COLLISION] Creating collision shapes for 27 new microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z << ")");
    m_removeCollision(cubePos);
    m_addCollision(cubePos);
    
    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::addMicrocube(
    const glm::ivec3& parentCubePos,
    const glm::ivec3& subcubePos,
    const glm::ivec3& microcubePos,
    const std::string& material
) {
    // Validate positions
    if (parentCubePos.x < 0 || parentCubePos.x >= 32 ||
        parentCubePos.y < 0 || parentCubePos.y >= 32 ||
        parentCubePos.z < 0 || parentCubePos.z >= 32) {
        return false;
    }
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) {
        return false;
    }
    if (microcubePos.x < 0 || microcubePos.x >= 3 || 
        microcubePos.y < 0 || microcubePos.y >= 3 || 
        microcubePos.z < 0 || microcubePos.z >= 3) {
        return false;
    }
    
    // Reject if a solid cube occupies the parent cube position (store is the presence authority)
    if (voxelStore.solid(kIdx(parentCubePos))) {
        return false;
    }

    // Check if microcube already exists
    if (getMicrocubeHelper(parentCubePos, subcubePos, microcubePos)) {
        return false;
    }

    // Check if this is the first microcube at this subcube position
    auto existingMicrocubes = getMicrocubesHelper(parentCubePos, subcubePos);
    if (existingMicrocubes.empty()) {
        // Check if parent subcube exists and remove it if found
        Subcube* parentSubcube = getSubcubeHelper(parentCubePos, subcubePos);
        if (parentSubcube) {
            // A finer microcube supersedes the coarser subcube filling the same 1/3 cell — an EXPECTED
            // operation (e.g. a micro-detailed fixture placed over a subcube structure), not corruption.
            // Debug-level so it doesn't spam the log; a fixture eating STRUCTURE here is caught by the
            // realized-world validators (wall-gap / hearth-flush), not by this low-level voxel op.
            LOG_DEBUG_FMT("ChunkVoxelManager", "microcube supersedes existing subcube at cube ("
                      << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                      << ") - removing the subcube (finer voxel replaces coarser)");
            
            // Remove from maps and vector
            removeSubcubeFromMaps(parentCubePos, subcubePos);
            auto& staticSubcubes = m_getStaticSubcubes();
            for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
                if (it->get() == parentSubcube) {
                    staticSubcubes.erase(it);
                    break;
                }
            }
        }
    }
    
    // Create new microcube
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + parentCubePos;
    auto newMicrocube = std::make_unique<Microcube>(parentWorldPos, subcubePos, microcubePos, material);
    Microcube* rawPtr = newMicrocube.get();
    auto& staticMicrocubes = m_getStaticMicrocubes();
    staticMicrocubes.push_back(std::move(newMicrocube));
    
    // Update hash maps
    addMicrocubeToMaps(parentCubePos, subcubePos, microcubePos, rawPtr);
    
    // Update collision shape (skipped during bulk template spawn)
    if (!m_isInBulkOperation()) {
        m_removeCollision(parentCubePos);
        m_addCollision(parentCubePos);
    }
    
    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::removeMicrocube(
    const glm::ivec3& parentCubePos,
    const glm::ivec3& subcubePos,
    const glm::ivec3& microcubePos
) {
    LOG_INFO_FMT("ChunkVoxelManager", "[REMOVE MICROCUBE] Called for cube (" << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
              << ") micro (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
    
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticMicrocubes = m_getStaticMicrocubes();
    
    // Try to find and remove from static microcubes
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ++it) {
        Microcube* microcube = it->get();
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + parentCubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos &&
            microcube->getMicrocubeLocalPosition() == microcubePos) {
            
            LOG_INFO("ChunkVoxelManager", "[REMOVE MICROCUBE] Found microcube to remove");
            
            // Remove from hash maps
            removeMicrocubeFromMaps(parentCubePos, subcubePos, microcubePos);
            
            // Remove from vector (unique_ptr auto-deletes)
            staticMicrocubes.erase(it);
            
            LOG_INFO("ChunkVoxelManager", "[REMOVE MICROCUBE] Checking for remaining microcubes");
            
            // Check if any microcubes remain at this parent position (O(1) map lookup)
            bool hasMicrocubes = microcubeMap.count(parentCubePos) > 0;
            
            if (hasMicrocubes) {
                // Still have microcubes - update collision shape
                LOG_INFO_FMT("ChunkVoxelManager", "[COLLISION] Microcubes remain at parent pos (" 
                          << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                          << ") - updating collision shape");
                m_removeCollision(parentCubePos);
                m_addCollision(parentCubePos);
            } else {
                // No more microcubes - but check if subcubes still exist
                auto remainingSubcubes = getSubcubesHelper(parentCubePos);
                if (!remainingSubcubes.empty()) {
                    LOG_INFO_FMT("ChunkVoxelManager", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                              << " subcubes still exist at (" << parentCubePos.x << "," << parentCubePos.y 
                              << "," << parentCubePos.z << ") - keeping SUBDIVIDED state and updating collision");
                    m_removeCollision(parentCubePos);
                    m_addCollision(parentCubePos);
                } else {
                    // No microcubes AND no subcubes - completely empty position
                    LOG_INFO_FMT("ChunkVoxelManager", "[COLLISION] No microcubes or subcubes remain at parent pos (" 
                              << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                              << ") - removing collision shape and voxel type entry");
                    m_removeCollision(parentCubePos);
                }
            }
            
            // Mark for update
            m_setNeedsUpdate(true);
            m_setDirty(true);
            
            return true;
        }
    }
    
    return false;
}

bool ChunkVoxelManager::clearMicrocubesAt(
    const glm::ivec3& cubePos,
    const glm::ivec3& subcubePos
) {
    auto microcubes = getMicrocubesHelper(cubePos, subcubePos);
    if (microcubes.empty()) return false;
    
    LOG_INFO_FMT("ChunkVoxelManager", "[CLEAR MICROCUBES] Removing all microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
              << ") - leaving empty space");
    
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticMicrocubes = m_getStaticMicrocubes();
    
    // Remove all microcubes at this subcube position
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ) {
        auto& microcube = *it;
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + cubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos) {
            
            // Remove from hash maps
            removeMicrocubeFromMaps(cubePos, subcubePos, microcube->getMicrocubeLocalPosition());
            
            // Remove from vector (unique_ptr auto-deletes)
            it = staticMicrocubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Check if any microcubes remain at the parent cube position (O(1) map lookup)
    bool hasMicrocubes = microcubeMap.count(cubePos) > 0;
    
    if (hasMicrocubes) {
        // Still have microcubes at other subcube positions - update collision
        m_removeCollision(cubePos);
        m_addCollision(cubePos);
    } else {
        // No more microcubes - but check if subcubes still exist
        auto remainingSubcubes = getSubcubesHelper(cubePos);
        if (!remainingSubcubes.empty()) {
            LOG_INFO_FMT("ChunkVoxelManager", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                      << " subcubes still exist at (" << cubePos.x << "," << cubePos.y 
                      << "," << cubePos.z << ") - keeping SUBDIVIDED state and updating collision");
            m_removeCollision(cubePos);
            m_addCollision(cubePos);
        } else {
            // No microcubes AND no subcubes - completely empty position
            LOG_INFO_FMT("ChunkVoxelManager", "[CLEAR MICROCUBES] No microcubes or subcubes remain at (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") - removing collision and voxel type entry");
            m_removeCollision(cubePos);
        }
    }
    
    // Mark for update
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

} // namespace Phyxel
