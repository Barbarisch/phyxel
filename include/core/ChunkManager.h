#pragma once

#include "Types.h"
#include <vector>
#include <unordered_map>

namespace VulkanCube {

// Manages all chunks in the world for scalable multi-chunk rendering
class ChunkManager {
public:
    std::vector<Chunk> chunks;
    
    // Vulkan device and memory management
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    ChunkManager() = default;
    ~ChunkManager() { cleanup(); }
    
    // Initialize with Vulkan device handles
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Create multiple chunks at specified world origins
    void createChunks(const std::vector<glm::ivec3>& origins);
    
    // Create a single chunk at specified origin
    void createChunk(const glm::ivec3& origin);
    
    // Update chunk data (for dynamic content)
    void updateChunk(size_t chunkIndex);
    
    // Get chunk at world position (for adding/removing cubes)
    Chunk* getChunkAt(const glm::ivec3& worldPos);
    
    // Calculate face visibility for all chunks (face culling optimization)
    void calculateChunkFaceCulling();
    
    // Perform occlusion culling across chunks (check cube neighbors across chunk boundaries)
    void performOcclusionCulling();
    
    // Get performance statistics for all chunks
    struct ChunkStats {
        uint32_t totalCubes = 0;
        uint32_t totalVertices = 0;
        uint32_t totalVisibleFaces = 0;
        uint32_t totalHiddenFaces = 0;
        uint32_t fullyOccludedCubes = 0;
        uint32_t partiallyOccludedCubes = 0;
    };
    ChunkStats getPerformanceStats() const;
    
    // Cleanup all resources
    void cleanup();
    
private:
    // Create Vulkan buffer for a chunk
    void createChunkBuffer(Chunk& chunk);
    
    // Fill chunk with initial cube data (32x32x32 grid)
    void populateChunk(Chunk& chunk);
    
    // Calculate face mask for a cube at relative position within chunk
    uint32_t calculateCubeFaceMask(int x, int y, int z) const;
    
    // Cross-chunk occlusion culling helpers
    bool isCubeAt(const glm::ivec3& worldPosition) const;
    uint32_t calculateOcclusionFaceMask(const glm::ivec3& chunkOrigin, int relativeX, int relativeY, int relativeZ) const;
    
    // Helper to find memory type
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

} // namespace VulkanCube
