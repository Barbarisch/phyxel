#include "core/DirtyChunkTracker.h"
#include <glm/glm.hpp>
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
    ++m_drainCalls;
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
    // Nearest to the focus first (budgeted drains only - an unbudgeted drain meshes all).
    if (budgeted && (m_hasFocus || m_hasPrio) && toUpdate.size() > 1) {
        const glm::vec3 f = m_focus;
        auto dist2 = [&](const glm::ivec3& cc) {
            if (!m_hasFocus) return 0.0f;
            const glm::vec3 lo = glm::vec3(cc) * 32.0f, hi = lo + 32.0f;
            const glm::vec3 d = f - glm::clamp(f, lo, hi);
            return glm::dot(d, d);
        };
        auto outsidePrio = [&](const glm::ivec3& cc) { return (!m_hasPrio || inPriority(cc)) ? 0 : 1; };
        std::stable_sort(toUpdate.begin(), toUpdate.end(), [&](const glm::ivec3& a, const glm::ivec3& b) {
            const int pa = outsidePrio(a), pb = outsidePrio(b);
            if (pa != pb) return pa < pb;
            return dist2(a) < dist2(b);
        });
    }
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
            if (chunkIndex != SIZE_MAX) { m_updateChunk(chunkIndex); ++m_processedTotal; }
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

size_t DirtyChunkTracker::getDirtyCountNear(const glm::vec3& worldPos, float radius) const {
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    const float r2 = radius * radius;
    size_t n = 0;
    for (const glm::ivec3& cc : m_dirtyChunkCoords) {
        const glm::vec3 lo = glm::vec3(cc) * 32.0f, hi = lo + 32.0f;
        const glm::vec3 nearest = glm::clamp(worldPos, lo, hi);   // point-to-box distance
        const glm::vec3 d = worldPos - nearest;
        if (glm::dot(d, d) <= r2) ++n;
    }
    return n;
}

int64_t DirtyChunkTracker::packCoord(const glm::ivec3& c) {
    return (static_cast<int64_t>(static_cast<uint32_t>(c.x) & 0x1FFFFF) << 42) |
           (static_cast<int64_t>(static_cast<uint32_t>(c.y) & 0x1FFFFF) << 21) |
            static_cast<int64_t>(static_cast<uint32_t>(c.z) & 0x1FFFFF);
}

bool DirtyChunkTracker::inPriority(const glm::ivec3& cc) const {
    if (!m_prioCoords.empty()) return m_prioCoords.count(packCoord(cc)) > 0;
    const float x0 = cc.x * 32.0f, z0 = cc.z * 32.0f;
    return x0 + 32.0f > m_prio.minX && x0 < m_prio.maxX && z0 + 32.0f > m_prio.minZ && z0 < m_prio.maxZ;
}

void DirtyChunkTracker::setPriorityCoords(const std::vector<glm::ivec3>& coords) {
    m_prioCoords.clear();
    for (const auto& c : coords) m_prioCoords.insert(packCoord(c));
    m_hasPrio = !m_prioCoords.empty();
}

size_t DirtyChunkTracker::getDirtyCountInPriority() const {
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    if (!m_hasPrio) return m_dirtyChunkCoords.size();
    size_t n = 0;
    for (const glm::ivec3& cc : m_dirtyChunkCoords) if (inPriority(cc)) ++n;
    return n;
}

size_t DirtyChunkTracker::getDirtyCountInXZ(float minX, float maxX, float minZ, float maxZ) const {
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    size_t n = 0;
    for (const glm::ivec3& cc : m_dirtyChunkCoords) {
        const float x0 = cc.x * 32.0f, x1 = x0 + 32.0f, z0 = cc.z * 32.0f, z1 = z0 + 32.0f;
        if (x1 > minX && x0 < maxX && z1 > minZ && z0 < maxZ) ++n;
    }
    return n;
}

void DirtyChunkTracker::clearDirtyChunkList() {
    m_dirtyChunkCoords.clear();
    m_idleChunkCoords.clear();
    m_hasDirtyChunks = false;
}

} // namespace Phyxel
