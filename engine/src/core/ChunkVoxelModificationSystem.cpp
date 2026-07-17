#include "core/ChunkVoxelModificationSystem.h"
#include "core/Chunk.h"
#include "utils/Logger.h"

namespace Phyxel {

namespace {
// Phase 4.4: removing a BOUNDARY voxel exposes the face of the adjacent chunk — if that
// neighbour is sealed, its collision grid is out of the physics query list, so a character
// digging across the seam would fall through until the deferred remesh re-evaluates it.
// Unseal exposed neighbours SYNCHRONOUSLY with the removal (idempotent, cheap no-op when the
// neighbour isn't sealed). The deferred remesh still refreshes faces/occlusion.
template <typename GetChunkFunc>
void unsealExposedNeighbors(const glm::ivec3& worldPos, const glm::ivec3& localPos,
                            const GetChunkFunc& getChunk) {
    static const glm::ivec3 kDirs[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::ivec3& d : kDirs) {
        const bool onBoundary = (d.x > 0 && localPos.x == 31) || (d.x < 0 && localPos.x == 0) ||
                                (d.y > 0 && localPos.y == 31) || (d.y < 0 && localPos.y == 0) ||
                                (d.z > 0 && localPos.z == 31) || (d.z < 0 && localPos.z == 0);
        if (!onBoundary) continue;
        if (Chunk* n = getChunk(worldPos + d)) n->unsealForEdit();
    }
}
}  // namespace

void ChunkVoxelModificationSystem::setCallbacks(
    GetChunkFunc getChunkFunc,
    MarkChunkDirtyFunc markDirtyFunc,
    UpdateAfterCubeBreakFunc updateBreakFunc,
    UpdateAfterCubePlaceFunc updatePlaceFunc
) {
    m_getChunk = getChunkFunc;
    m_markChunkDirty = markDirtyFunc;
    m_updateAfterCubeBreak = updateBreakFunc;
    m_updateAfterCubePlace = updatePlaceFunc;
}

// ========================================================================
// FAST CUBE MODIFICATION METHODS (Optimized)
// ========================================================================

bool ChunkVoxelModificationSystem::removeCubeFast(const glm::ivec3& worldPos) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    // deferRebuild=true: skip the per-call full-chunk re-mesh; markChunkDirty +
    // the per-frame updateDirtyChunks() pass re-meshes each touched chunk ONCE.
    bool result = chunk->removeCube(localPos, /*deferRebuild=*/true);

    if (result) {
        m_markChunkDirty(chunk);
        // Face regeneration is deferred to updateChunk() via the dirty tracker.
        unsealExposedNeighbors(worldPos, localPos, m_getChunk);   // 4.4: collision NOW, not later
    }

    return result;
}

bool ChunkVoxelModificationSystem::addCubeFast(const glm::ivec3& worldPos) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;  // No chunk exists at this position
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    bool result = chunk->addCube(localPos);
    
    if (result) {
        m_markChunkDirty(chunk);
        // Note: Face regeneration would happen in updateChunk()
    }
    
    return result;
}

// ========================================================================
// LEGACY CUBE MODIFICATION METHODS (Backward compatibility)
// ========================================================================

bool ChunkVoxelModificationSystem::removeCube(const glm::ivec3& worldPos) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    
    // Use the Chunk class's removeCube method
    bool result = chunk->removeCube(localPos);
    if (result) {
        // Chunk is now marked dirty and will be saved properly on next save
        // No need for immediate database deletion - saveChunk handles all deletions

        // Use efficient selective update instead of full chunk rebuild
        m_updateAfterCubeBreak(worldPos);
        unsealExposedNeighbors(worldPos, localPos, m_getChunk);   // 4.4: collision NOW, not later
    }
    return result;
}

bool ChunkVoxelModificationSystem::addCube(const glm::ivec3& worldPos) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    
    // Use the Chunk class's addCube method
    bool result = chunk->addCube(localPos);
    
    if (result) {
        // Use efficient selective update instead of full chunk rebuild
        m_updateAfterCubePlace(worldPos);
    }
    return result;
}

bool ChunkVoxelModificationSystem::addCubeWithMaterial(const glm::ivec3& worldPos, const std::string& material) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    
    // Use the Chunk class's addCube method
    bool result = chunk->addCube(localPos, material);
    
    if (result) {
        // Use efficient selective update instead of full chunk rebuild
        m_updateAfterCubePlace(worldPos);
    }
    return result;
}

bool ChunkVoxelModificationSystem::addSubcubeWithMaterial(const glm::ivec3& worldPos, const glm::ivec3& subcubePos, const std::string& material) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) {
        LOG_DEBUG("VoxelMod", "addSubcubeWithMaterial FAIL: no chunk for world({},{},{})",
                  worldPos.x, worldPos.y, worldPos.z);
        return false;
    }

    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    LOG_DEBUG("VoxelMod", "addSubcubeWithMaterial: world({},{},{})->local({},{},{}) sub({},{},{})",
              worldPos.x, worldPos.y, worldPos.z, localPos.x, localPos.y, localPos.z,
              subcubePos.x, subcubePos.y, subcubePos.z);
    bool result = chunk->addSubcube(localPos, subcubePos, material);

    if (result) {
        m_updateAfterCubePlace(worldPos);
    }
    return result;
}

bool ChunkVoxelModificationSystem::removeSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;

    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    bool result = chunk->removeSubcube(localPos, subcubePos);

    if (result) {
        m_updateAfterCubeBreak(worldPos);
    }
    return result;
}

bool ChunkVoxelModificationSystem::addMicrocubeWithMaterial(const glm::ivec3& worldPos, const glm::ivec3& subcubePos,
                                                             const glm::ivec3& microcubePos, const std::string& material) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;

    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    bool result = chunk->addMicrocube(localPos, subcubePos, microcubePos, material);

    if (result) {
        m_updateAfterCubePlace(worldPos);
    }
    return result;
}

bool ChunkVoxelModificationSystem::removeMicrocube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos,
                                                    const glm::ivec3& microcubePos) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;

    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    bool result = chunk->removeMicrocube(localPos, subcubePos, microcubePos);

    if (result) {
        m_updateAfterCubeBreak(worldPos);
    }
    return result;
}

} // namespace Phyxel
