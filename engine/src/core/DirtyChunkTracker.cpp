#include "core/DirtyChunkTracker.h"
#include "core/Chunk.h"
#include <algorithm>

namespace Phyxel {

void DirtyChunkTracker::setCallbacks(
    ChunkVectorAccessFunc getChunksFunc,
    UpdateChunkFunc updateChunkFunc,
    GetChunkIndexFunc getChunkIndexFunc
) {
    m_getChunks = getChunksFunc;
    m_updateChunk = updateChunkFunc;
    m_getChunkIndex = getChunkIndexFunc;
}

void DirtyChunkTracker::markChunkDirty(size_t chunkIndex) {
    auto& chunks = m_getChunks();
    if (chunkIndex >= chunks.size()) return;
    
    // Mark the chunk as needing update (for GPU rendering)
    chunks[chunkIndex]->setNeedsUpdate(true);
    
    // Mark the chunk as dirty (for database persistence)
    chunks[chunkIndex]->setDirty(true);
    
    // Add to dirty list if not already present (avoid duplicates)
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (std::find(m_dirtyChunkIndices.begin(), m_dirtyChunkIndices.end(), chunkIndex) == m_dirtyChunkIndices.end()) {
        m_dirtyChunkIndices.push_back(chunkIndex);
        m_hasDirtyChunks = true;
    }
}

void DirtyChunkTracker::markChunkDirty(Chunk* chunk) {
    if (!chunk) return;
    
    size_t chunkIndex = m_getChunkIndex(chunk);
    if (chunkIndex != SIZE_MAX) {
        markChunkDirty(chunkIndex);
    }
}

void DirtyChunkTracker::updateDirtyChunks() {
    // Atomically drain the dirty list
    std::vector<size_t> toUpdate;
    {
        std::lock_guard<std::mutex> lock(m_dirtyMutex);
        if (!m_hasDirtyChunks || m_dirtyChunkIndices.empty()) {
            return;
        }
        std::swap(toUpdate, m_dirtyChunkIndices);
        m_hasDirtyChunks = false;
    }
    
    auto& chunks = m_getChunks();
    
    // Update only the chunks that have been marked as dirty
    for (size_t chunkIndex : toUpdate) {
        if (chunkIndex < chunks.size()) {
            m_updateChunk(chunkIndex);
        }
    }
}

void DirtyChunkTracker::clearDirtyChunkList() {
    m_dirtyChunkIndices.clear();
    m_hasDirtyChunks = false;
}

} // namespace Phyxel
