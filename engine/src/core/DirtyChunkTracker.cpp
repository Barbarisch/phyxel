#include "core/DirtyChunkTracker.h"
#include "core/Chunk.h"
#include <algorithm>
#include <chrono>

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
    updateDirtyChunks(0.0);  // 0 = unlimited (drain the whole list this call)
}

void DirtyChunkTracker::updateDirtyChunks(double budgetMs) {
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
    const bool budgeted = budgetMs > 0.0;
    const auto start = std::chrono::high_resolution_clock::now();

    // Update marked chunks. Always process at least one (guarantees progress),
    // then bail once over budget so the rest spread to the next call/frame.
    size_t processed = 0;
    for (; processed < toUpdate.size(); ++processed) {
        size_t chunkIndex = toUpdate[processed];
        if (chunkIndex < chunks.size()) {
            m_updateChunk(chunkIndex);
        }
        if (budgeted && (processed + 1) < toUpdate.size()) {
            double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - start).count();
            if (elapsed >= budgetMs) {
                ++processed;
                break;
            }
        }
    }

    // Re-queue any chunks we didn't get to. Prepend them (they were dirty first)
    // ahead of anything added concurrently, deduping so a chunk re-marked during
    // processing isn't meshed twice.
    if (processed < toUpdate.size()) {
        std::lock_guard<std::mutex> lock(m_dirtyMutex);
        std::vector<size_t> merged(toUpdate.begin() + processed, toUpdate.end());
        for (size_t idx : m_dirtyChunkIndices) {
            if (std::find(merged.begin(), merged.end(), idx) == merged.end()) {
                merged.push_back(idx);
            }
        }
        m_dirtyChunkIndices.swap(merged);
        m_hasDirtyChunks = !m_dirtyChunkIndices.empty();
    }
}

void DirtyChunkTracker::clearDirtyChunkList() {
    m_dirtyChunkIndices.clear();
    m_hasDirtyChunks = false;
}

} // namespace Phyxel
