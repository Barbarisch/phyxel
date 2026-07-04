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

void DirtyChunkTracker::markChunkForRemesh(size_t chunkIndex) {
    auto& chunks = m_getChunks();
    if (chunkIndex >= chunks.size()) return;

    // Render mesh is stale, voxel data is NOT: no setDirty(true) here — DB-dirty
    // would make the streaming evictor re-save an unchanged chunk to SQLite.
    chunks[chunkIndex]->setNeedsUpdate(true);

    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (std::find(m_dirtyChunkIndices.begin(), m_dirtyChunkIndices.end(), chunkIndex) == m_dirtyChunkIndices.end()) {
        m_dirtyChunkIndices.push_back(chunkIndex);
        m_hasDirtyChunks = true;
    }
}

void DirtyChunkTracker::markChunkForRemesh(Chunk* chunk) {
    if (!chunk) return;

    size_t chunkIndex = m_getChunkIndex(chunk);
    if (chunkIndex != SIZE_MAX) {
        markChunkForRemesh(chunkIndex);
    }
}

void DirtyChunkTracker::markChunkForRemeshIdle(size_t chunkIndex) {
    auto& chunks = m_getChunks();
    if (chunkIndex >= chunks.size()) return;

    // Cosmetic tier: no needsUpdate flag yet — it's set when the chunk is promoted
    // for processing (setting it now would make unrelated updateChunk calls pay the
    // full remesh early). No DB-dirty either (voxel data unchanged).
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (std::find(m_idleChunkIndices.begin(), m_idleChunkIndices.end(), chunkIndex) == m_idleChunkIndices.end() &&
        std::find(m_dirtyChunkIndices.begin(), m_dirtyChunkIndices.end(), chunkIndex) == m_dirtyChunkIndices.end()) {
        m_idleChunkIndices.push_back(chunkIndex);
    }
}

void DirtyChunkTracker::markChunkForRemeshIdle(Chunk* chunk) {
    if (!chunk) return;

    size_t chunkIndex = m_getChunkIndex(chunk);
    if (chunkIndex != SIZE_MAX) {
        markChunkForRemeshIdle(chunkIndex);
    }
}

void DirtyChunkTracker::updateDirtyChunks() {
    updateDirtyChunks(0.0);  // 0 = unlimited (drain the whole list this call)
}

void DirtyChunkTracker::updateDirtyChunks(double budgetMs) {
    // Adaptive backoff: a single chunk remesh (full mesh + light bake) can cost far
    // more than the whole budget (~50ms Debug), and the "always process ≥1" progress
    // guarantee means a long backlog (streaming churn) would otherwise burn that cost
    // EVERY frame — a constant ~18 FPS band while flying. When the last call blew well
    // past its budget, sit out a couple of frames so the cost amortizes (~45 FPS feel);
    // small edit remeshes stay effectively instant (a ≤2-frame delay is imperceptible).
    if (budgetMs > 0.0 && m_backoffFrames > 0) {
        --m_backoffFrames;
        return;
    }

    // Atomically drain the dirty list. When the mandatory queue is empty, promote ONE
    // idle-tier chunk (cosmetic neighbour re-culls) — they only run in quiet frames.
    std::vector<size_t> toUpdate;
    {
        std::lock_guard<std::mutex> lock(m_dirtyMutex);
        if (m_hasDirtyChunks && !m_dirtyChunkIndices.empty()) {
            std::swap(toUpdate, m_dirtyChunkIndices);
            m_hasDirtyChunks = false;
        } else if (!m_idleChunkIndices.empty()) {
            toUpdate.push_back(m_idleChunkIndices.front());
            m_idleChunkIndices.erase(m_idleChunkIndices.begin());
        } else {
            return;
        }
    }
    {
        // Promoted idle chunks need their needsUpdate flag set now (deferred at mark
        // time — see markChunkForRemeshIdle).
        auto& allChunks = m_getChunks();
        for (size_t idx : toUpdate) {
            if (idx < allChunks.size()) allChunks[idx]->setNeedsUpdate(true);
        }
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

    if (budgeted) {
        const double totalMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start).count();
        if (totalMs > budgetMs * 4.0) m_backoffFrames = 2;
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
    m_idleChunkIndices.clear();
    m_hasDirtyChunks = false;
}

} // namespace Phyxel
