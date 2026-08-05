#include "core/DirtyChunkTracker.h"
#include "core/Chunk.h"
#include "utils/CoordinateUtils.h"
#include <algorithm>
#include <chrono>

namespace Phyxel {

namespace {
inline glm::ivec3 coordOf(const Chunk& chunk) {
    return Utils::CoordinateUtils::worldToChunkCoord(chunk.getWorldOrigin());
}
} // namespace

void DirtyChunkTracker::setCallbacks(
    ChunkVectorAccessFunc getChunksFunc,
    UpdateChunkFunc updateChunkFunc,
    GetChunkIndexFunc getChunkIndexFunc,
    GetChunkAtCoordFunc getChunkAtCoordFunc
) {
    m_getChunks = getChunksFunc;
    m_updateChunk = updateChunkFunc;
    m_getChunkIndex = getChunkIndexFunc;
    m_getChunkAtCoord = getChunkAtCoordFunc;
}

void DirtyChunkTracker::markChunkDirty(size_t chunkIndex) {
    auto& chunks = m_getChunks();
    if (chunkIndex >= chunks.size()) return;
    markChunkDirty(chunks[chunkIndex].get());
}

void DirtyChunkTracker::markChunkDirty(Chunk* chunk) {
    if (!chunk) return;

    // Mark the chunk as needing update (for GPU rendering)
    chunk->setNeedsUpdate(true);

    // Mark the chunk as dirty (for database persistence)
    chunk->setDirty(true);

    // Add to dirty list if not already present (avoid duplicates)
    const glm::ivec3 coord = coordOf(*chunk);
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (std::find(m_dirtyChunkCoords.begin(), m_dirtyChunkCoords.end(), coord) ==
        m_dirtyChunkCoords.end()) {
        m_dirtyChunkCoords.push_back(coord);
        m_hasDirtyChunks = true;
    }
}

void DirtyChunkTracker::markChunkForRemesh(size_t chunkIndex) {
    auto& chunks = m_getChunks();
    if (chunkIndex >= chunks.size()) return;
    markChunkForRemesh(chunks[chunkIndex].get());
}

void DirtyChunkTracker::markChunkForRemesh(Chunk* chunk) {
    if (!chunk) return;

    // Render mesh is stale, voxel data is NOT: no setDirty(true) here — DB-dirty
    // would make the streaming evictor re-save an unchanged chunk to SQLite.
    chunk->setNeedsUpdate(true);

    const glm::ivec3 coord = coordOf(*chunk);
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (std::find(m_dirtyChunkCoords.begin(), m_dirtyChunkCoords.end(), coord) ==
        m_dirtyChunkCoords.end()) {
        m_dirtyChunkCoords.push_back(coord);
        m_hasDirtyChunks = true;
    }
}

void DirtyChunkTracker::markChunkForRemeshIdle(size_t chunkIndex) {
    auto& chunks = m_getChunks();
    if (chunkIndex >= chunks.size()) return;
    markChunkForRemeshIdle(chunks[chunkIndex].get());
}

void DirtyChunkTracker::markChunkForRemeshIdle(Chunk* chunk) {
    if (!chunk) return;

    // Cosmetic tier: no needsUpdate flag yet — it's set when the chunk is promoted
    // for processing (setting it now would make unrelated updateChunk calls pay the
    // full remesh early). No DB-dirty either (voxel data unchanged).
    const glm::ivec3 coord = coordOf(*chunk);
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (std::find(m_idleChunkCoords.begin(), m_idleChunkCoords.end(), coord) ==
            m_idleChunkCoords.end() &&
        std::find(m_dirtyChunkCoords.begin(), m_dirtyChunkCoords.end(), coord) ==
            m_dirtyChunkCoords.end()) {
        m_idleChunkCoords.push_back(coord);
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
    std::vector<glm::ivec3> toUpdate;
    bool promotedIdle = false;
    {
        std::lock_guard<std::mutex> lock(m_dirtyMutex);
        if (m_hasDirtyChunks && !m_dirtyChunkCoords.empty()) {
            std::swap(toUpdate, m_dirtyChunkCoords);
            m_hasDirtyChunks = false;
        } else if (!m_idleChunkCoords.empty()) {
            toUpdate.push_back(m_idleChunkCoords.front());
            m_idleChunkCoords.erase(m_idleChunkCoords.begin());
            promotedIdle = true;
        } else {
            return;
        }
    }

    const bool budgeted = budgetMs > 0.0;
    const auto start = std::chrono::high_resolution_clock::now();

    // Update marked chunks. Coords resolve to the chunk's CURRENT index at process
    // time — an evicted coord resolves to null and simply drops out (the old
    // index-keyed queue retargeted to whatever chunk shifted into the slot).
    // Always process at least one (guarantees progress), then bail once over budget
    // so the rest spread to the next call/frame.
    size_t processed = 0;
    for (; processed < toUpdate.size(); ++processed) {
        Chunk* chunk = m_getChunkAtCoord ? m_getChunkAtCoord(toUpdate[processed]) : nullptr;
        if (chunk) {
            // Promoted idle chunks get their deferred needsUpdate flag now.
            if (promotedIdle) chunk->setNeedsUpdate(true);
            const size_t chunkIndex = m_getChunkIndex(chunk);
            if (chunkIndex != SIZE_MAX) m_updateChunk(chunkIndex);
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
        std::vector<glm::ivec3> merged(toUpdate.begin() + processed, toUpdate.end());
        for (const glm::ivec3& c : m_dirtyChunkCoords) {
            if (std::find(merged.begin(), merged.end(), c) == merged.end()) {
                merged.push_back(c);
            }
        }
        m_dirtyChunkCoords.swap(merged);
        m_hasDirtyChunks = !m_dirtyChunkCoords.empty();
    }
}

void DirtyChunkTracker::clearDirtyChunkList() {
    m_dirtyChunkCoords.clear();
    m_idleChunkCoords.clear();
    m_hasDirtyChunks = false;
}

} // namespace Phyxel
