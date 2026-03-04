#pragma once

#include "core/Types.h"
#include "graphics/ChunkRenderBuffer.h"
#include <vector>
#include <functional>
#include <vulkan/vulkan.h>

namespace VulkanCube {

// Forward declarations
class Cube;
class Subcube;
class Microcube;

namespace Graphics {

/**
 * ChunkRenderManager handles all rendering concerns for a chunk
 * Responsibilities:
 * - Face generation and culling (cube, subcube, microcube)
 * - Instance data management
 * - Vulkan buffer coordination via ChunkRenderBuffer
 * - Texture and color updates
 */
class ChunkRenderManager {
public:
    // Neighbor lookup function type for cross-chunk culling
    using NeighborLookupFunc = std::function<const Cube*(const glm::ivec3& worldPos)>;

    ChunkRenderManager();
    ~ChunkRenderManager();

    // Non-copyable
    ChunkRenderManager(const ChunkRenderManager&) = delete;
    ChunkRenderManager& operator=(const ChunkRenderManager&) = delete;

    // Movable
    ChunkRenderManager(ChunkRenderManager&& other) noexcept;
    ChunkRenderManager& operator=(ChunkRenderManager&& other) noexcept;

    // Initialization
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);

    // Face rebuilding - split into focused methods
    void rebuildAllFaces(
        const std::vector<Cube*>& cubes,
        const std::vector<Subcube*>& subcubes,
        const std::vector<Microcube*>& microcubes,
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube = nullptr
    );

    void rebuildCubeFaces(
        const std::vector<Cube*>& cubes,
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube = nullptr
    );

    void rebuildSubcubeFaces(
        const std::vector<Subcube*>& subcubes,
        const glm::ivec3& worldOrigin
    );

    void rebuildMicrocubeFaces(
        const std::vector<Microcube*>& microcubes,
        const glm::ivec3& worldOrigin
    );

    // Vulkan buffer management
    void createVulkanBuffer();
    void updateVulkanBuffer();
    void cleanupVulkanResources();
    void ensureBufferCapacity(size_t requiredInstances);

    // Partial updates for hover effects
    void updateSingleCubeTexture(
        const glm::ivec3& localPos,
        uint16_t textureIndex,
        const std::vector<Cube*>& cubes
    );

    void updateSingleSubcubeTexture(
        const glm::ivec3& parentLocalPos,
        const glm::ivec3& subcubePos,
        uint16_t textureIndex,
        const std::vector<Subcube*>& subcubes,
        const glm::ivec3& worldOrigin
    );

    void updateSingleCubeColor(
        const glm::ivec3& localPos,
        const glm::vec3& newColor,
        const std::vector<Cube*>& cubes
    );

    void updateSingleSubcubeColor(
        const glm::ivec3& localPos,
        const glm::ivec3& subcubePos,
        const glm::vec3& newColor,
        const std::vector<Subcube*>& subcubes,
        const glm::ivec3& worldOrigin
    );

    // Accessors
    const std::vector<InstanceData>& getFaces() const { return faces; }
    uint32_t getNumInstances() const { return numInstances; }
    bool getNeedsUpdate() const { return needsUpdate; }
    void setNeedsUpdate(bool update) { needsUpdate = update; }

    // Buffer info
    VkBuffer getInstanceBuffer() const { return renderBuffer.getBuffer(); }
    void* getMappedMemory() const { return renderBuffer.getMappedMemory(); }
    size_t getBufferCapacity() const { return renderBuffer.getCapacity(); }
    size_t getMaxInstancesUsed() const { return renderBuffer.getMaxInstancesUsed(); }
    float getBufferUtilization() const {
        return renderBuffer.getCapacity() > 0 
            ? float(faces.size()) / float(renderBuffer.getCapacity()) * 100.0f 
            : 0.0f;
    }

    // Logging
    void logBufferUtilization() const;

private:
    // Helper methods for face visibility checking
    bool isCubeFaceVisible(
        const glm::ivec3& cubePos,
        int faceID,
        const std::vector<Cube*>& cubes,
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube
    ) const;

    // CRITICAL: Assumes cubes vector is indexed by position using formula:
    // index = z + y*32 + x*32*32 (X-major order)
    // This matches Chunk::localToIndex() and enables O(1) lookup.
    // DO NOT use linear search - it causes O(n²) performance!
    const Cube* getCubeAtPosition(
        const glm::ivec3& localPos,
        const std::vector<Cube*>& cubes
    ) const;

    // Member variables
    std::vector<InstanceData> faces;           // Visible faces (CPU pre-filtered)
    uint32_t numInstances;                     // Count of visible faces
    bool needsUpdate;                          // Flag for buffer updates

    ChunkRenderBuffer renderBuffer;            // Vulkan buffer management

    VkDevice device;
    VkPhysicalDevice physicalDevice;
};

} // namespace Graphics
} // namespace VulkanCube
