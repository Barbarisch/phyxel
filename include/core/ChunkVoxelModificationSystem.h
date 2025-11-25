#pragma once

#include "Types.h"
#include "Chunk.h"
#include "Cube.h"
#include "Subcube.h"
#include "utils/CoordinateUtils.h"
#include <glm/glm.hpp>
#include <functional>

namespace VulkanCube {

// Forward declarations
class ChunkVoxelQuerySystem;

// Handles all voxel modification operations (add/remove/update cubes and subcubes)
// Separates write operations from read operations (ChunkVoxelQuerySystem)
class ChunkVoxelModificationSystem {
public:
    // Callback function types for chunk access and updates
    using GetChunkFunc = std::function<Chunk*(const glm::ivec3&)>;
    using MarkChunkDirtyFunc = std::function<void(Chunk*)>;
    using UpdateAfterCubeBreakFunc = std::function<void(const glm::ivec3&)>;
    using UpdateAfterCubePlaceFunc = std::function<void(const glm::ivec3&)>;
    
    ChunkVoxelModificationSystem() = default;
    ~ChunkVoxelModificationSystem() = default;
    
    // Configure callbacks for chunk access and modification tracking
    void setCallbacks(
        GetChunkFunc getChunkFunc,
        MarkChunkDirtyFunc markDirtyFunc,
        UpdateAfterCubeBreakFunc updateBreakFunc,
        UpdateAfterCubePlaceFunc updatePlaceFunc
    );
    
    // ========================================================================
    // FAST CUBE MODIFICATION METHODS (Optimized)
    // ========================================================================
    
    // Set cube color (optimized version with dirty marking)
    bool setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color);
    
    // Remove cube at position (optimized version)
    bool removeCubeFast(const glm::ivec3& worldPos);
    
    // Add cube at position (optimized version)
    bool addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color);
    
    // ========================================================================
    // LEGACY CUBE MODIFICATION METHODS (Backward compatibility)
    // ========================================================================
    
    // Legacy: Set cube color (redirects to fast version)
    void setCubeColor(const glm::ivec3& worldPos, const glm::vec3& color);
    
    // Legacy: Remove cube with face updates
    bool removeCube(const glm::ivec3& worldPos);
    
    // Legacy: Add cube with face updates
    bool addCube(const glm::ivec3& worldPos, const glm::vec3& color);
    
    // ========================================================================
    // COLOR MODIFICATION METHODS (Efficient versions)
    // ========================================================================
    
    // Efficient cube color change (no texture modification)
    void setCubeColorEfficient(const glm::ivec3& worldPos, const glm::vec3& color);
    
    // Efficient subcube color change (no texture modification)
    void setSubcubeColorEfficient(
        const glm::ivec3& worldPos,
        const glm::ivec3& subcubePos,
        const glm::vec3& color
    );
    
private:
    // Callbacks for chunk access and update coordination
    GetChunkFunc m_getChunk;
    MarkChunkDirtyFunc m_markChunkDirty;
    UpdateAfterCubeBreakFunc m_updateAfterCubeBreak;
    UpdateAfterCubePlaceFunc m_updateAfterCubePlace;
    
    // Helper: Convert world position to local coordinate within chunk
    glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) const {
        return Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    }
};

} // namespace VulkanCube
