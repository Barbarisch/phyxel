#pragma once

#include "Types.h"
#include <vector>
#include <unordered_map>

namespace VulkanCube {

// Custom hash function for glm::ivec3 to use as key in unordered_map
struct ChunkCoordHash {
    std::size_t operator()(const glm::ivec3& coord) const {
        // Combine X, Y, Z coordinates into a single hash
        // Using prime numbers to reduce hash collisions
        return std::hash<int>()(coord.x) ^ 
               (std::hash<int>()(coord.y) << 1) ^ 
               (std::hash<int>()(coord.z) << 2);
    }
};

// Manages all chunks in the world for scalable multi-chunk rendering
class ChunkManager {
public:
    std::vector<Chunk> chunks;
    
    // Spatial hash map for O(1) chunk lookup by chunk coordinates
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    
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
    
    // Update all chunks that need updating (call this every frame for dynamic content)
    void updateAllChunks();
    
    // Rebuild faces from cubes (call after modifying cubes)
    void rebuildChunkFaces(Chunk& chunk);
    
    // Get chunk at world position (for adding/removing cubes)
    Chunk* getChunkAt(const glm::ivec3& worldPos);
    
    // Cube manipulation helpers
    Cube* getCubeAt(const glm::ivec3& worldPos);          // Get cube at world position
    void setCubeColor(const glm::ivec3& worldPos, const glm::vec3& color);
    bool removeCube(const glm::ivec3& worldPos);          // Returns true if cube was removed
    bool addCube(const glm::ivec3& worldPos, const glm::vec3& color = glm::vec3(1.0f));
    
    // Convert between coordinate systems
    static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) { return worldPos / 32; }
    static glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) { return worldPos % 32; }
    static glm::ivec3 chunkCoordToOrigin(const glm::ivec3& chunkCoord) { return chunkCoord * 32; }
    
    // CRITICAL: Index formula MUST match loop order in populateChunk()
    // Loop order: for(x) for(y) for(z) → Z-minor indexing (Z coefficient = 1)
    // Formula: z + y*32 + x*1024 (Z changes fastest, matches innermost loop)
    // DO NOT change to X-minor without changing populateChunk() loop order!
    static size_t localToIndex(const glm::ivec3& localPos) { return localPos.z + localPos.y * 32 + localPos.x * 32 * 32; }
    
    // Fast O(1) chunk lookup functions
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);      // Get chunk by chunk coordinates
    Chunk* getChunkAtFast(const glm::ivec3& worldPos);        // Fast O(1) world position lookup
    
    // Fast O(1) cube lookup functions
    Cube* getCubeAtFast(const glm::ivec3& worldPos);          // Fast O(1) cube lookup
    bool setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color);  // Fast color update
    bool removeCubeFast(const glm::ivec3& worldPos);          // Fast cube removal
    bool addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color = glm::vec3(1.0f));  // Fast cube addition
    
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
