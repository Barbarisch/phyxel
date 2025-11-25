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

bool ChunkVoxelModificationSystem::setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    bool result = chunk->setCubeColor(localPos, color);
    
    if (result) {
        m_markChunkDirty(chunk);
    }
    
    return result;
}

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

bool ChunkVoxelModificationSystem::addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;  // No chunk exists at this position
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    bool result = chunk->addCube(localPos, color);
    
    if (!result) {
        // If addCube failed, try to set color of existing cube
        result = chunk->setCubeColor(localPos, color);
    }
    
    if (result) {
        m_markChunkDirty(chunk);
        // Note: Face regeneration would happen in updateChunk()
    }
    
    return result;
}

// ========================================================================
// LEGACY CUBE MODIFICATION METHODS (Backward compatibility)
// ========================================================================

void ChunkVoxelModificationSystem::setCubeColor(const glm::ivec3& worldPos, const glm::vec3& color) {
    // Redirect to optimized version
    setCubeColorFast(worldPos, color);
}

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

bool ChunkVoxelModificationSystem::addCube(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    
    // Use the Chunk class's addCube or setCubeColor method
    bool result = chunk->addCube(localPos, color);
    if (!result) {
        // If addCube failed, try to update existing cube color
        result = chunk->setCubeColor(localPos, color);
    }
    
    if (result) {
        // Use efficient selective update instead of full chunk rebuild
        m_updateAfterCubePlace(worldPos);
    }
    return result;
}

// ========================================================================
// COLOR MODIFICATION METHODS (Efficient versions)
// ========================================================================

void ChunkVoxelModificationSystem::setCubeColorEfficient(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    
    // Color changes should not affect textures - textures and colors are separate properties
    // Note: Currently the system uses textures instead of colors for cube faces
    // If you want to change cube appearance, modify the texture index per face instead
    
    // TODO: If we implement a color tinting system in the future, 
    // we would update color properties here without touching texture indices
}

void ChunkVoxelModificationSystem::setSubcubeColorEfficient(
    const glm::ivec3& worldPos,
    const glm::ivec3& subcubePos,
    const glm::vec3& color
) {
    Chunk* chunk = m_getChunk(worldPos);
    if (!chunk) return;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    std::vector<Subcube*> subcubes = chunk->getSubcubesAt(localPos);
    if (subcubes.empty()) return;
    
    // Color changes should not affect textures - textures and colors are separate properties
    // Note: Currently the system uses textures instead of colors for subcube faces
    // If you want to change subcube appearance, modify the texture index per face instead
    
    // TODO: If we implement a color tinting system in the future, 
    // we would update color properties here without touching texture indices
}

} // namespace VulkanCube
