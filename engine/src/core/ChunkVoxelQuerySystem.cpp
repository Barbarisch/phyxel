#include "core/ChunkVoxelQuerySystem.h"
#include "core/ChunkStreamingManager.h"  // For ChunkCoordHash
#include "utils/Logger.h"

namespace Phyxel {

void ChunkVoxelQuerySystem::setCallbacks(
    ChunkMapAccessFunc chunkMapAccessFunc,
    ChunkVectorAccessFunc chunkVectorAccessFunc
) {
    m_chunkMapAccess = chunkMapAccessFunc;
    m_chunkVectorAccess = chunkVectorAccessFunc;
}

// ========================================================================
// CHUNK LOOKUP METHODS
// ========================================================================

Chunk* ChunkVoxelQuerySystem::getChunkAtCoord(const glm::ivec3& chunkCoord) {
    auto& chunkMap = m_chunkMapAccess();
    auto it = chunkMap.find(chunkCoord);
    return (it != chunkMap.end()) ? it->second : nullptr;
}

const Chunk* ChunkVoxelQuerySystem::getChunkAtCoord(const glm::ivec3& chunkCoord) const {
    auto& chunkMap = const_cast<ChunkVoxelQuerySystem*>(this)->m_chunkMapAccess();
    auto it = chunkMap.find(chunkCoord);
    return (it != chunkMap.end()) ? it->second : nullptr;
}

Chunk* ChunkVoxelQuerySystem::getChunkAtFast(const glm::ivec3& worldPos) {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    return getChunkAtCoord(chunkCoord);
}

Chunk* ChunkVoxelQuerySystem::getChunkAt(const glm::ivec3& worldPos) {
    // Redirect to optimized version
    return getChunkAtFast(worldPos);
}

// ========================================================================
// CUBE QUERY METHODS
// ========================================================================

Cube* ChunkVoxelQuerySystem::getCubeAtFast(const glm::ivec3& worldPos) {
    // Step 1: Get chunk using O(1) hash map lookup
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return nullptr;
    
    // Step 2: Calculate local position
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    
    // Step 3: Use Chunk class's getCubeAt method
    return chunk->getCubeAt(localPos);
}

Cube* ChunkVoxelQuerySystem::getCubeAt(const glm::ivec3& worldPos) {
    // Redirect to optimized version
    return getCubeAtFast(worldPos);
}

// ========================================================================
// SUBCUBE QUERY METHODS
// ========================================================================

// ========================================================================
// VOXEL LOCATION RESOLUTION (for hover detection)
// ========================================================================

VoxelLocation ChunkVoxelQuerySystem::resolveGlobalPosition(const glm::ivec3& worldPos) const {
    // O(1) chunk lookup
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) {
        return VoxelLocation(); // No chunk exists
    }
    
    // O(1) voxel resolution within chunk
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    return chunk->resolveLocalPosition(localPos);
}

bool ChunkVoxelQuerySystem::hasVoxelAt(const glm::ivec3& worldPos) const {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    return chunk->hasVoxelAt(localPos);
}

float ChunkVoxelQuerySystem::subVoxelFloor(const glm::ivec3& worldPos) const {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) return 0.0f;
    // Cube-only since D3c-5 (no 1/9 m sub-voxel content): a cube is fully solid, else no floor.
    return chunk->hasVoxelAt(worldToLocalCoord(worldPos)) ? -1.0f : 0.0f;
}

VoxelLocation::Type ChunkVoxelQuerySystem::getVoxelTypeAt(const glm::ivec3& worldPos) const {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) return VoxelLocation::EMPTY;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    return chunk->getVoxelType(localPos);
}

bool ChunkVoxelQuerySystem::occupiedMicro(const glm::ivec3& micro) const {
    auto divFloor = [](int a) { return a >= 0 ? a / 9 : -((-a + 8) / 9); };
    const glm::ivec3 cube(divFloor(micro.x), divFloor(micro.y), divFloor(micro.z));
    const Chunk* chunk = getChunkAtCoord(worldToChunkCoord(cube));
    if (!chunk) return false;
    const glm::ivec3 local = worldToLocalCoord(cube);
    return chunk->getVoxelType(local) == VoxelLocation::CUBE;   // cube-only since D3c-5
}

} // namespace Phyxel
