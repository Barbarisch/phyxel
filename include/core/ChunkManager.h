#pragma once

#include "Types.h"
#include "IChunkManager.h"
#include "Chunk.h"
#include "Cube.h"
#include "utils/CoordinateUtils.h"
#include "core/ChunkStreamingManager.h"
#include "core/DynamicObjectManager.h"
#include "core/FaceUpdateCoordinator.h"
#include "core/ChunkInitializer.h"
#include "core/DirtyChunkTracker.h"
#include "core/ChunkVoxelQuerySystem.h"
#include "core/ChunkVoxelModificationSystem.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace Physics {
    class PhysicsWorld;
}

namespace VulkanCube {
    class WorldStorage; // Forward declaration
}

namespace VulkanCube {

// Manages all chunks in the world for scalable multi-chunk rendering
// 
// REFACTORING STATUS
// Phase 1 - ChunkStreamingManager extraction COMPLETE
// Phase 2 - DynamicObjectManager extraction COMPLETE
// Phase 3 - FaceUpdateCoordinator extraction COMPLETE
// Phase 4 - ChunkInitializer extraction COMPLETE
// Phase 5 - DirtyChunkTracker extraction COMPLETE
// Phase 6 - ChunkVoxelQuerySystem extraction COMPLETE
// Phase 7 - ChunkVoxelModificationSystem extraction COMPLETE
// Phase 20 - Removed duplicate physics update code (delegated to DynamicObjectManager) COMPLETE
// Original: 1,414 lines → Current: 604 lines (-810 lines, -57%)
// 
class ChunkManager : public IChunkManager {
public:
    std::vector<std::unique_ptr<Chunk>> chunks;
    
    // Global dynamic subcube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Subcube>> globalDynamicSubcubes;
    
    // Global dynamic cube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Cube>> globalDynamicCubes;
    
    // Global dynamic microcube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Microcube>> globalDynamicMicrocubes;
    
    // Spatial hash map for O(1) chunk lookup by chunk coordinates
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    
    // Vulkan device and memory management
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // Physics world for proper cleanup of dynamic objects
    Physics::PhysicsWorld* physicsWorld = nullptr;
    
    // Chunk streaming manager (handles chunk loading/unloading/saving)
    ChunkStreamingManager m_streamingManager;
    
    // Dynamic object manager (handles global dynamic subcubes/cubes/microcubes)
    DynamicObjectManager m_dynamicObjectManager;
    
    // Face update coordinator (handles face rebuilding and updates)
    FaceUpdateCoordinator m_faceUpdateCoordinator;
    
    // Chunk initializer (handles chunk creation and world generation)
    ChunkInitializer m_chunkInitializer;
    
    // Dirty chunk tracker (handles selective chunk updates)
    DirtyChunkTracker m_dirtyChunkTracker;
    
    // Voxel query system (handles chunk/cube/subcube lookups)
    ChunkVoxelQuerySystem m_voxelQuerySystem;
    
    // Voxel modification system (handles add/remove/update operations)
    ChunkVoxelModificationSystem m_voxelModificationSystem;
    
    // Chunk streaming settings
    float loadDistance = 160.0f;   // Distance to load chunks (5 chunks * 32 units)
    float unloadDistance = 224.0f; // Distance to unload chunks (7 chunks * 32 units)
    glm::vec3 playerPosition = glm::vec3(0.0f); // Player position for streaming
    
    ChunkManager() = default;
    ~ChunkManager(); // Destructor needs to be defined in .cpp file due to unique_ptr with forward declaration
    
    // Initialize with Vulkan device handles
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Set physics world for proper cleanup of dynamic objects
    void setPhysicsWorld(Physics::PhysicsWorld* physics);
    
    // World storage management
    bool initializeWorldStorage(const std::string& worldPath);
    void setPlayerPosition(const glm::vec3& position) { playerPosition = position; }
    
    // Chunk streaming for infinite worlds
    void updateChunkStreaming(); // Call every frame to load/unload chunks based on player position
    void loadChunksAroundPosition(const glm::vec3& position, float radius);
    void unloadDistantChunks(const glm::vec3& position, float radius);
    
    // World persistence
    bool saveChunk(Chunk* chunk);
    bool saveAllChunks();
    bool saveDirtyChunks();  // Save only chunks that have been modified
    bool loadChunk(const glm::ivec3& chunkCoord);
    bool generateOrLoadChunk(const glm::ivec3& chunkCoord); // Generate if doesn't exist, load if it does
    
    // Post-loading face rebuilding (call after all chunks are loaded)
    void rebuildAllChunkFaces(); // Rebuild faces for all chunks with proper cross-chunk culling
    
    // Initialize hash maps for all existing chunks (call after loading chunks)
    void initializeAllChunkVoxelMaps();
    
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
    void addGlobalDynamicCube(std::unique_ptr<Cube> cube);
    void updateGlobalDynamicCubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicCubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicCubes();
    const std::vector<std::unique_ptr<Cube>>& getGlobalDynamicCubes() const { return globalDynamicCubes; }
    size_t getGlobalDynamicCubeCount() const { return globalDynamicCubes.size(); }
    
    // Global dynamic microcube management
    void addGlobalDynamicMicrocube(std::unique_ptr<Microcube> microcube);
    void updateGlobalDynamicMicrocubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicMicrocubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicMicrocubes();
    const std::vector<std::unique_ptr<Microcube>>& getGlobalDynamicMicrocubes() const { return globalDynamicMicrocubes; }
    size_t getGlobalDynamicMicrocubeCount() const { return globalDynamicMicrocubes.size(); }
    
    // Combined dynamic object management - face data access
    const std::vector<DynamicSubcubeInstanceData>& getGlobalDynamicSubcubeFaces() const { return globalDynamicSubcubeFaces; }
    
    // Dynamic object face data for rendering (both subcubes and full cubes)
    std::vector<DynamicSubcubeInstanceData> globalDynamicSubcubeFaces;
    
    // Convert between coordinate systems (forwarded to Utils::CoordinateUtils)
    static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) { 
        return Utils::CoordinateUtils::worldToChunkCoord(worldPos);
    }
    static glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) { 
        return Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    }
    static glm::ivec3 chunkCoordToOrigin(const glm::ivec3& chunkCoord) { 
        return Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    }
    
    // Fast O(1) chunk lookup functions
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);      // Get chunk by chunk coordinates
    const Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord) const; // Const version
    Chunk* getChunkAtFast(const glm::ivec3& worldPos);        // Fast O(1) world position lookup
    
    // Fast O(1) cube lookup functions
    Cube* getCubeAtFast(const glm::ivec3& worldPos);          // Fast O(1) cube lookup
    bool setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color);  // Fast color update
    bool removeCubeFast(const glm::ivec3& worldPos);          // Fast cube removal
    bool addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color = glm::vec3(1.0f));  // Fast cube addition
    
    // NEW: O(1) VoxelLocation resolution system for optimized hover detection
    VoxelLocation resolveGlobalPosition(const glm::ivec3& worldPos) const override;
    VoxelLocation resolveGlobalPositionWithSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) const;
    bool hasVoxelAt(const glm::ivec3& worldPos) const;
    VoxelLocation::Type getVoxelTypeAt(const glm::ivec3& worldPos) const;
    
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
