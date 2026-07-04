#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <cstddef>
#include <mutex>

namespace Phyxel {

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
     * @brief Queue a chunk for budgeted re-meshing WITHOUT marking it dirty for
     * database persistence.
     *
     * markChunkDirty() also sets the chunk's DB-dirty flag, which makes the streaming
     * evictor re-save the chunk to SQLite. Use this variant when only the render mesh
     * is stale (e.g. cross-chunk culling after a neighbour streams in) — the voxel
     * DATA is unchanged, so a DB write would be pure waste (and mass evictions of
     * such chunks caused multi-hundred-ms save stalls).
     */
    void markChunkForRemesh(size_t chunkIndex);
    void markChunkForRemesh(Chunk* chunk);

    /**
     * @brief LOW-PRIORITY remesh: processed only when the main dirty queue is empty.
     *
     * For cosmetic-only remeshes — a neighbour re-cull after a chunk streams in
     * removes now-hidden boundary faces (overdraw) and refreshes boundary light, but
     * skipping it never creates holes. A streamed-in chunk used to cost 7 full
     * remeshes (~50ms each in Debug) in the frame loop; with neighbours on the idle
     * tier it costs 1 while the camera is churning chunks, and the rest converge
     * once the primary queue goes quiet.
     */
    void markChunkForRemeshIdle(size_t chunkIndex);
    void markChunkForRemeshIdle(Chunk* chunk);

    /**
     * @brief Update all dirty chunks and clear dirty list
     *
     * Early exits if no dirty chunks. Updates only marked chunks.
     */
    void updateDirtyChunks();

    /**
     * @brief Update dirty chunks within a per-call time budget (ms).
     *
     * Processes at least one chunk, then stops once the elapsed re-mesh time
     * exceeds budgetMs, re-queuing the remaining chunks for the next call. This
     * spreads a large dirty backlog (e.g. a 64-chunk world-gen) across frames so
     * the mesh+GPU commit never stalls the main thread for seconds at once.
     * budgetMs <= 0 means unlimited (identical to updateDirtyChunks()).
     */
    void updateDirtyChunks(double budgetMs);
    
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
    std::mutex m_dirtyMutex;  // Protects dirty indices from concurrent markDirty calls
    std::vector<size_t> m_dirtyChunkIndices;
    std::vector<size_t> m_idleChunkIndices;   // low-priority tier (guarded by m_dirtyMutex)
    bool m_hasDirtyChunks = false;
    // Adaptive backoff (see updateDirtyChunks): frames to skip after a call whose
    // single-chunk minimum blew far past the budget, so a churn backlog amortizes
    // instead of taxing every frame.
    int m_backoffFrames = 0;
};

} // namespace Phyxel
