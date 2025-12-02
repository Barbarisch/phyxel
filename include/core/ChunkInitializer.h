#pragma once

#include "Types.h"
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace VulkanCube {

// Forward declarations
class Chunk;
struct ChunkCoordHash; // Forward declare - defined in ChunkStreamingManager.h

// Type alias for chunk spatial hash map
using ChunkMap = std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>;

/**
 * @brief Handles chunk initialization, world generation, and setup coordination
 * 
 * This module manages:
 * - Multi-chunk creation and initialization
 * - Single chunk creation with cross-chunk culling updates
 * - Voxel hash map initialization for all chunks
 * - Global face rebuilding with cross-chunk culling
 * - Chunk population and Vulkan buffer setup
 * 
 * Uses callback pattern to access ChunkManager state without tight coupling.
 */
class ChunkInitializer {
public:
    // Callback types for ChunkManager state access
    using ChunkVectorAccessFunc = std::function<std::vector<std::unique_ptr<Chunk>>&()>;
    using ChunkMapAccessFunc = std::function<ChunkMap&()>;
    using DeviceAccessFunc = std::function<std::pair<VkDevice, VkPhysicalDevice>()>;
    using GetChunkAtCoordFunc = std::function<Chunk*(const glm::ivec3&)>;
    using RebuildChunkWithCullingFunc = std::function<void(Chunk&)>;
    
    ChunkInitializer() = default;
    ~ChunkInitializer() = default;
    
    /**
     * @brief Configure callbacks for accessing ChunkManager state
     */
    void setCallbacks(
        ChunkVectorAccessFunc getChunksFunc,
        ChunkMapAccessFunc getChunkMapFunc,
        DeviceAccessFunc getDeviceFunc,
        GetChunkAtCoordFunc getChunkAtCoordFunc,
        RebuildChunkWithCullingFunc rebuildChunkFunc
    );
    
    /**
     * @brief Create multiple chunks from origin positions
     * 
     * Performs:
     * 1. Clear existing chunks
     * 2. Create and initialize each chunk
     * 3. Populate with cubes
     * 4. Initial face generation
     * 5. Cross-chunk culling pass
     * 
     * @param origins World space origins for each chunk
     */
    void createChunks(const std::vector<glm::ivec3>& origins);
    
    /**
     * @brief Create single chunk and update adjacent chunks
     * 
     * Creates chunk and updates all 26 neighbors with cross-chunk culling
     * 
     * @param origin World space origin of the chunk
     */
    void createChunk(const glm::ivec3& origin);
    
    /**
     * @brief Initialize voxel hash maps for all chunks
     * 
     * Optimizes O(1) hover detection by building spatial hash maps
     */
    void initializeAllChunkVoxelMaps();
    
    /**
     * @brief Rebuild faces for all chunks with cross-chunk culling
     * 
     * Three-pass process:
     * 1. End bulk operations and build collision systems
     * 2. Rebuild faces with cross-chunk occlusion culling
     * 3. Update Vulkan buffers
     */
    void rebuildAllChunkFaces();
    
    /**
     * @brief Perform cross-chunk occlusion culling for all chunks
     */
    void performOcclusionCulling();
    
private:
    // Callbacks for ChunkManager state access
    ChunkVectorAccessFunc m_getChunks;
    ChunkMapAccessFunc m_getChunkMap;
    DeviceAccessFunc m_getDevice;
    GetChunkAtCoordFunc m_getChunkAtCoord;
    RebuildChunkWithCullingFunc m_rebuildChunk;
};

} // namespace VulkanCube
