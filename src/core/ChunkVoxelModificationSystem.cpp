#include "core/ChunkVoxelModificationSystem.h"
#include "utils/Logger.h"

namespace VulkanCube {

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

} // namespace VulkanCube
