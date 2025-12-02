#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <cstddef>

namespace VulkanCube {

// Forward declaration
class Chunk;

/**
 * @brief Tracks and manages dirty chunks for selective update optimization
 * 
 * This module handles:
 * - Dirty chunk tracking (mark chunks that need GPU buffer updates)
 * - Selective update coordination (only update dirty chunks)
 * - Duplicate prevention (avoid marking same chunk multiple times)
 * - Efficient batch updates
 * 
 * Uses callback pattern to access ChunkManager state without tight coupling.
 */
class DirtyChunkTracker {
public:
    // Callback types for ChunkManager state access
    using ChunkVectorAccessFunc = std::function<std::vector<std::unique_ptr<Chunk>>&()>;
    using UpdateChunkFunc = std::function<void(size_t)>;
    using GetChunkIndexFunc = std::function<size_t(Chunk*)>;
    
    DirtyChunkTracker() = default;
    ~DirtyChunkTracker() = default;
    
    /**
     * @brief Configure callbacks for accessing ChunkManager state
     */
    void setCallbacks(
        ChunkVectorAccessFunc getChunksFunc,
        UpdateChunkFunc updateChunkFunc,
        GetChunkIndexFunc getChunkIndexFunc
    );
    
    /**
     * @brief Mark a chunk as dirty by index
     * 
     * Sets chunk's needsUpdate flag and adds to dirty list (no duplicates)
     * 
     * @param chunkIndex Index of the chunk to mark dirty
     */
    void markChunkDirty(size_t chunkIndex);
    
    /**
     * @brief Mark a chunk as dirty by pointer
     * 
     * Converts chunk pointer to index and marks dirty
     * 
     * @param chunk Pointer to the chunk to mark dirty
     */
    void markChunkDirty(Chunk* chunk);
    
    /**
     * @brief Update all dirty chunks and clear dirty list
     * 
     * Early exits if no dirty chunks. Updates only marked chunks.
     */
    void updateDirtyChunks();
    
    /**
     * @brief Clear dirty chunk list and reset flag
     */
    void clearDirtyChunkList();
    
    /**
     * @brief Check if any chunks are dirty
     */
    bool hasDirty() const { return m_hasDirtyChunks; }
    
    /**
     * @brief Get count of dirty chunks
     */
    size_t getDirtyCount() const { return m_dirtyChunkIndices.size(); }
    
private:
    // Callbacks for ChunkManager state access
    ChunkVectorAccessFunc m_getChunks;
    UpdateChunkFunc m_updateChunk;
    GetChunkIndexFunc m_getChunkIndex;
    
    // Dirty chunk tracking state
    std::vector<size_t> m_dirtyChunkIndices;
    bool m_hasDirtyChunks = false;
};

} // namespace VulkanCube
