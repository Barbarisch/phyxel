#pragma once

#include "Types.h"
#include "Chunk.h"
#include "Cube.h"
#include "Subcube.h"
#include "utils/CoordinateUtils.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <functional>

namespace Phyxel {

// Forward declaration of ChunkCoordHash (defined in ChunkStreamingManager.h)
struct ChunkCoordHash;

// Provides O(1) voxel query and lookup operations across chunks
// Consolidates all chunk/cube/subcube lookup logic with spatial hash map
class ChunkVoxelQuerySystem {
public:
    // Callback function types for accessing chunk data
    using ChunkMapAccessFunc = std::function<std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>&()>;
    using ChunkVectorAccessFunc = std::function<std::vector<std::unique_ptr<Chunk>>&()>;
    
    ChunkVoxelQuerySystem() = default;
    ~ChunkVoxelQuerySystem() = default;
    
    // Configure callbacks for chunk data access
    void setCallbacks(
        ChunkMapAccessFunc chunkMapAccessFunc,
        ChunkVectorAccessFunc chunkVectorAccessFunc
    );
    
    // ========================================================================
    // CHUNK LOOKUP METHODS
    // ========================================================================
    
    // O(1) chunk lookup by chunk coordinate
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);
    const Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord) const;
    
    // O(1) chunk lookup by world position (converts to chunk coord)
    Chunk* getChunkAtFast(const glm::ivec3& worldPos);
    Chunk* getChunkAt(const glm::ivec3& worldPos);  // Legacy compatibility
    
    // ========================================================================
    // CUBE QUERY METHODS
    // ========================================================================
    
    // O(1) cube lookup by world position
    Cube* getCubeAtFast(const glm::ivec3& worldPos);
    Cube* getCubeAt(const glm::ivec3& worldPos);  // Legacy compatibility
    
    // ========================================================================
    // SUBCUBE QUERY METHODS
    // ========================================================================
    
    // Get subcube at world position and subcube local position
    Subcube* getSubcubeAt(const glm::ivec3& worldPos, const glm::ivec3& subcubePos);
    
    // ========================================================================
    // VOXEL LOCATION RESOLUTION (for hover detection)
    // ========================================================================
    
    // O(1) voxel location resolution - determines if position has cube/subcube
    VoxelLocation resolveGlobalPosition(const glm::ivec3& worldPos) const;
    
    // Resolve with specific subcube validation
    VoxelLocation resolveGlobalPositionWithSubcube(
        const glm::ivec3& worldPos,
        const glm::ivec3& subcubePos
    ) const;
    
    // Check if voxel exists at position
    bool hasVoxelAt(const glm::ivec3& worldPos) const;
    
    // Get voxel type (EMPTY, CUBE, SUBDIVIDED)
    VoxelLocation::Type getVoxelTypeAt(const glm::ivec3& worldPos) const;
    
private:
    // Callbacks for accessing chunk data
    ChunkMapAccessFunc m_chunkMapAccess;
    ChunkVectorAccessFunc m_chunkVectorAccess;
    
    // Helper: Convert world position to chunk coordinate
    glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) const {
        return Utils::CoordinateUtils::worldToChunkCoord(worldPos);
    }
    
    // Helper: Convert world position to local coordinate within chunk
    glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) const {
        return Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    }
};

} // namespace Phyxel
