#include "core/ChunkVoxelModificationSystem.h"
#include "utils/Logger.h"

namespace Phyxel {

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
    bool result = chunk->removeCube(localPos);
    
    if (result) {
        m_markChunkDirty(chunk);
        // Note: Face regeneration would happen in updateChunk()
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
    if (!chunk) return false;

    glm::ivec3 localPos = worldToLocalCoord(worldPos);
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
