#include "core/ChunkInitializer.h"
#include "core/Chunk.h"
#include "core/ChunkStreamingManager.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"

namespace VulkanCube {

void ChunkInitializer::setCallbacks(
    ChunkVectorAccessFunc getChunksFunc,
    ChunkMapAccessFunc getChunkMapFunc,
    DeviceAccessFunc getDeviceFunc,
    GetChunkAtCoordFunc getChunkAtCoordFunc,
    RebuildChunkWithCullingFunc rebuildChunkFunc
) {
    m_getChunks = getChunksFunc;
    m_getChunkMap = getChunkMapFunc;
    m_getDevice = getDeviceFunc;
    m_getChunkAtCoord = getChunkAtCoordFunc;
    m_rebuildChunk = rebuildChunkFunc;
}

void ChunkInitializer::createChunks(const std::vector<glm::ivec3>& origins) {
    auto& chunks = m_getChunks();
    auto& chunkMap = m_getChunkMap();
    auto [device, physicalDevice] = m_getDevice();
    
    chunks.clear();
    chunkMap.clear();
    chunks.reserve(origins.size());
    
    for (const auto& origin : origins) {
        auto chunk = std::make_unique<Chunk>(origin);
        
        // Initialize with Vulkan device
        chunk->initialize(device, physicalDevice);
        
        // Add to spatial hash map for O(1) lookup
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(origin);
        chunkMap[chunkCoord] = chunk.get();
        
        // Fill chunk with 32x32x32 cubes at relative positions
        chunk->populateWithCubes();
        
        // Generate face instances from cubes (initial pass - no cross-chunk culling yet)
        chunk->rebuildFaces();
        
        // Create Vulkan buffer for this chunk
        chunk->createVulkanBuffer();
        
        chunks.push_back(std::move(chunk));
    }
    
    // Second pass: Rebuild all chunks with cross-chunk culling now that all chunks exist
    for (auto& chunk : chunks) {
        m_rebuildChunk(*chunk);
        chunk->updateVulkanBuffer();  // Update the GPU buffer with new face data
    }
}

void ChunkInitializer::createChunk(const glm::ivec3& origin) {
    auto& chunks = m_getChunks();
    auto& chunkMap = m_getChunkMap();
    auto [device, physicalDevice] = m_getDevice();
    
    auto chunk = std::make_unique<Chunk>(origin);
    
    // Initialize with Vulkan device
    chunk->initialize(device, physicalDevice);
    
    // Add to spatial hash map for O(1) lookup
    glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(origin);
    chunkMap[chunkCoord] = chunk.get();
    
    chunk->populateWithCubes();
    
    // Generate face instances from cubes (initial pass)
    chunk->rebuildFaces();
    
    chunk->createVulkanBuffer();
    
    chunks.push_back(std::move(chunk));
    
    // Update cross-chunk culling for this chunk and its neighbors
    Chunk* newChunk = chunks.back().get();
    m_rebuildChunk(*newChunk);
    newChunk->updateVulkanBuffer();
    
    // Also update adjacent chunks to account for the new chunk
    glm::ivec3 chunkCoordOfNew = Utils::CoordinateUtils::worldToChunkCoord(origin);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue; // Skip the new chunk itself
                
                glm::ivec3 adjacentCoord = chunkCoordOfNew + glm::ivec3(dx, dy, dz);
                Chunk* adjacentChunk = m_getChunkAtCoord(adjacentCoord);
                if (adjacentChunk) {
                    m_rebuildChunk(*adjacentChunk);
                    adjacentChunk->updateVulkanBuffer();
                }
            }
        }
    }
}

void ChunkInitializer::initializeAllChunkVoxelMaps() {
    auto& chunks = m_getChunks();
    
    LOG_INFO_FMT("Chunk", "Initializing voxel hash maps for all " << chunks.size() << " chunks...");
    
    for (auto& chunk : chunks) {
        chunk->initializeVoxelMaps();
    }
    
    LOG_INFO("Chunk", "Completed voxel map initialization for optimized O(1) hover detection");
}

void ChunkInitializer::rebuildAllChunkFaces() {
    auto& chunks = m_getChunks();
    
    LOG_INFO("Chunk", "Rebuilding faces for all loaded chunks with cross-chunk culling...");
    
    // First: End bulk operations for all chunks and build proper collision systems
    for (auto& chunk : chunks) {
        chunk->endBulkOperation();
    }
    
    // Second pass: rebuild basic faces for each chunk
    for (auto& chunk : chunks) {
        chunk->rebuildFaces();
    }
    
    // Second pass: perform cross-chunk occlusion culling
    performOcclusionCulling();
    
    // Third pass: update Vulkan buffers with new face data
    for (auto& chunk : chunks) {
        chunk->updateVulkanBuffer();
    }
    
    LOG_INFO_FMT("Chunk", "Face rebuilding complete for " << chunks.size() << " chunks");
}

void ChunkInitializer::performOcclusionCulling() {
    auto& chunks = m_getChunks();
    
    LOG_DEBUG("Chunk", "Regenerating chunks with updated cross-chunk occlusion culling...");
    
    // With CPU pre-filtering, we need to regenerate all chunk geometry
    // when occlusion relationships change between chunks
    for (auto& chunk : chunks) {
        // Regenerate face instances with cross-chunk occlusion culling
        m_rebuildChunk(*chunk);
        
        // Update GPU buffer with new face data
        chunk->updateVulkanBuffer();
    }
}

} // namespace VulkanCube
