#pragma once

#include "Types.h"
#include <glm/glm.hpp>

namespace VulkanCube {

/**
 * Interface for ChunkManager to allow mocking in tests.
 * Exposes only the methods needed by consumers like VoxelRaycaster.
 */
class IChunkManager {
public:
    virtual ~IChunkManager() = default;

    /**
     * Resolve a global voxel position to a VoxelLocation.
     * This is the primary method used for raycasting and voxel queries.
     * 
     * @param globalPos The global coordinate of the voxel
     * @return VoxelLocation containing the chunk, local position, and type
     */
    virtual VoxelLocation resolveGlobalPosition(const glm::ivec3& globalPos) const = 0;
};

} // namespace VulkanCube
