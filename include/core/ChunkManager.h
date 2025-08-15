#pragma once

#include "Types.h"
#include "Chunk.h"
#include <vector>
#include <unordered_map>
#include <memory>

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
    std::vector<std::unique_ptr<Chunk>> chunks;
    
    // Spatial hash map for O(1) chunk lookup by chunk coordinates
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    
    // Performance optimization: Track chunks that need updating
    std::vector<size_t> dirtyChunkIndices;  // Indices of chunks marked needsUpdate=true
    bool hasDirtyChunks = false;            // Quick check to avoid vector operations
    
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
    
    // OPTIMIZED: Update only chunks that have been modified (O(dirty) instead of O(all))
    // Call this every frame - it's efficient and only processes changed chunks
    void updateDirtyChunks();
    
    // DEPRECATED: Update all chunks (inefficient for large worlds)
    // Use updateDirtyChunks() instead for better performance
    void updateAllChunks();
    
    // Rebuild faces from cubes (call after modifying cubes)
    void rebuildChunkFaces(Chunk& chunk);
    
    // Rebuild faces with cross-chunk occlusion culling
    void rebuildChunkFacesWithCrosschunkCulling(Chunk& chunk);
    
    // Get chunk at world position (for adding/removing cubes)
    Chunk* getChunkAt(const glm::ivec3& worldPos);
    
    // Cube manipulation helpers
    Cube* getCubeAt(const glm::ivec3& worldPos);          // Get cube at world position
    void setCubeColor(const glm::ivec3& worldPos, const glm::vec3& color);
    void setCubeColorEfficient(const glm::ivec3& worldPos, const glm::vec3& color); // Efficient version for hover
    bool removeCube(const glm::ivec3& worldPos);          // Returns true if cube was removed
    bool addCube(const glm::ivec3& worldPos, const glm::vec3& color = glm::vec3(1.0f));
    
    // Subcube manipulation helpers
    Subcube* getSubcubeAt(const glm::ivec3& worldPos, const glm::ivec3& subcubePos); // Get subcube at position
    void setSubcubeColorEfficient(const glm::ivec3& worldPos, const glm::ivec3& subcubePos, const glm::vec3& color);
    glm::vec3 getSubcubeColor(const glm::ivec3& worldPos, const glm::ivec3& subcubePos);
    
    // Convert between coordinate systems
    static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) { 
        // Proper division that handles negative numbers correctly
        glm::ivec3 chunk;
        chunk.x = worldPos.x >= 0 ? worldPos.x / 32 : (worldPos.x - 31) / 32;
        chunk.y = worldPos.y >= 0 ? worldPos.y / 32 : (worldPos.y - 31) / 32;
        chunk.z = worldPos.z >= 0 ? worldPos.z / 32 : (worldPos.z - 31) / 32;
        return chunk;
    }
    static glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) { 
        // Proper modulo that handles negative numbers correctly
        glm::ivec3 local;
        local.x = ((worldPos.x % 32) + 32) % 32;
        local.y = ((worldPos.y % 32) + 32) % 32;
        local.z = ((worldPos.z % 32) + 32) % 32;
        return local;
    }
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
    
    // Dirty chunk tracking for performance optimization
    void markChunkDirty(size_t chunkIndex);
    void markChunkDirty(Chunk* chunk);               // Overload for chunk pointer
    void clearDirtyChunkList();
    size_t getChunkIndex(const Chunk* chunk) const;  // Helper to find chunk index from pointer
    
    // Cleanup all resources
    void cleanup();
    
private:
    // Cross-chunk occlusion culling helpers
    bool isCubeAt(const glm::ivec3& worldPosition) const;
    uint32_t calculateOcclusionFaceMask(const glm::ivec3& chunkOrigin, int relativeX, int relativeY, int relativeZ) const;
    
    // Helper to find memory type
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

} // namespace VulkanCube
