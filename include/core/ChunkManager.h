#pragma once

#include "Types.h"
#include "Chunk.h"
#include "DynamicCube.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace Physics {
    class PhysicsWorld;
}

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
    
    // Global dynamic subcube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Subcube>> globalDynamicSubcubes;
    
    // Global dynamic cube management (not tied to specific chunks)
    std::vector<std::unique_ptr<DynamicCube>> globalDynamicCubes;
    
    // Spatial hash map for O(1) chunk lookup by chunk coordinates
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    
    // Performance optimization: Track chunks that need updating
    std::vector<size_t> dirtyChunkIndices;  // Indices of chunks marked needsUpdate=true
    bool hasDirtyChunks = false;            // Quick check to avoid vector operations
    
    // Vulkan device and memory management
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // Physics world for proper cleanup of dynamic objects
    Physics::PhysicsWorld* physicsWorld = nullptr;
    
    ChunkManager() = default;
    ~ChunkManager() { cleanup(); }
    
    // Initialize with Vulkan device handles
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Set physics world for proper cleanup of dynamic objects
    void setPhysicsWorld(Physics::PhysicsWorld* physics);
    
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
    void updateAllChunks();
    
    // Face culling and rebuilding
    void calculateChunkFaceCulling();
    void rebuildGlobalDynamicSubcubeFaces();
    
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
    
    // Global dynamic subcube management
    void addGlobalDynamicSubcube(std::unique_ptr<Subcube> subcube);
    void updateGlobalDynamicSubcubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicSubcubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicSubcubes();
    void rebuildGlobalDynamicFaces();  // Rebuild global dynamic faces
    const std::vector<std::unique_ptr<Subcube>>& getGlobalDynamicSubcubes() const { return globalDynamicSubcubes; }
    size_t getGlobalDynamicSubcubeCount() const { return globalDynamicSubcubes.size(); }
    
    // Global dynamic cube management
    void addGlobalDynamicCube(std::unique_ptr<DynamicCube> cube);
    void updateGlobalDynamicCubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicCubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicCubes();
    const std::vector<std::unique_ptr<DynamicCube>>& getGlobalDynamicCubes() const { return globalDynamicCubes; }
    size_t getGlobalDynamicCubeCount() const { return globalDynamicCubes.size(); }
    
    // Combined dynamic object management - face data access
    const std::vector<DynamicSubcubeInstanceData>& getGlobalDynamicSubcubeFaces() const { return globalDynamicSubcubeFaces; }
    
    // Dynamic object face data for rendering (both subcubes and full cubes)
    std::vector<DynamicSubcubeInstanceData> globalDynamicSubcubeFaces;
    
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
    
    // Fast O(1) chunk lookup functions
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);      // Get chunk by chunk coordinates
    const Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord) const; // Const version
    Chunk* getChunkAtFast(const glm::ivec3& worldPos);        // Fast O(1) world position lookup
    
    // Fast O(1) cube lookup functions
    Cube* getCubeAtFast(const glm::ivec3& worldPos);          // Fast O(1) cube lookup
    bool setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color);  // Fast color update
    bool removeCubeFast(const glm::ivec3& worldPos);          // Fast cube removal
    bool addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color = glm::vec3(1.0f));  // Fast cube addition
    
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
    
    // ========================================================================
    // EFFICIENT SELECTIVE UPDATE SYSTEM
    // ========================================================================
    
    // Event-specific efficient update methods
    void updateAfterCubeBreak(const glm::ivec3& worldPos);        // Updates only affected faces when cube is broken
    void updateAfterCubePlace(const glm::ivec3& worldPos);        // Updates only affected faces when cube is placed  
    void updateAfterCubeSubdivision(const glm::ivec3& worldPos);  // Updates when cube is subdivided into subcubes
    void updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos); // Updates when subcube breaks
    
    // Core selective update methods
    void updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded); // Central method for position-based updates
    void updateNeighborFaces(const glm::ivec3& worldPos);          // Updates faces of up to 6 neighboring cubes
    void updateSingleCubeFaces(const glm::ivec3& worldPos);        // Updates faces of single cube only
    
    // Cross-chunk boundary helpers
    std::vector<glm::ivec3> getAffectedNeighborPositions(const glm::ivec3& worldPos); // Get all 6 neighbor positions (may span chunks)
    void updateFacesAtPosition(const glm::ivec3& worldPos);        // Update faces for cube at specific position
    
    // Cleanup all resources
    void cleanup();
    
private:
    // Memory management helper
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    // Cross-chunk occlusion culling helpers
    bool isCubeAt(const glm::ivec3& worldPosition) const;
    uint32_t calculateOcclusionFaceMask(const glm::ivec3& chunkOrigin, int relativeX, int relativeY, int relativeZ) const;
};

} // namespace VulkanCube
