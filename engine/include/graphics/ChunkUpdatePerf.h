#pragma once

// --- B0: chunk-update sub-cost instrumentation (docs/ChunkUpdateHitchPlan.md) ---
// Localizes the ~40 ms frame hitch inside the unprofiled updateDirtyChunks phase by timing its
// distinct costs separately (buffer create / realloc / upload / dirty-update-total), alongside the
// T0 mesh timer. Lock-free integer-microsecond atomics, header-only (single shared instance via an
// inline function-local static — ODR-guaranteed). Read via get_render_stats.chunk_update_timing.
// This is DIAGNOSTIC only; no behaviour change. Remove or keep once the hitch is attributed (B0).

#include <atomic>
#include <array>
#include <cstdint>
#include <chrono>

namespace Phyxel {
namespace Graphics {

enum class ChunkPerfPhase : int {
    BufferCreate = 0,   // ChunkRenderManager::createVulkanBuffer (first-time per-chunk alloc)
    BufferRealloc,      // ChunkRenderBuffer::reallocateBuffer     (growth: destroy+free+create+alloc)
    BufferUpload,       // ChunkRenderManager::updateVulkanBuffer  (ensureCapacity + memcpy, all 3 bufs)
    DirtyUpdateTotal,   // ChunkManager::updateDirtyChunks(budget) (whole per-frame chunk-update phase)
    COUNT
};

struct ChunkPerfStat {
    uint64_t count  = 0;
    double   lastMs = 0.0;
    double   maxMs  = 0.0;
    double   avgMs  = 0.0;
};

namespace detail {
    struct PhaseCounters {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> lastUs{0};
        std::atomic<uint64_t> maxUs{0};
        std::atomic<uint64_t> totalUs{0};
    };
    inline std::array<PhaseCounters, static_cast<int>(ChunkPerfPhase::COUNT)>& chunkPerfCounters() {
        static std::array<PhaseCounters, static_cast<int>(ChunkPerfPhase::COUNT)> c;
        return c;
    }
}

inline void recordChunkPerf(ChunkPerfPhase phase, double ms) {
    auto& c = detail::chunkPerfCounters()[static_cast<int>(phase)];
    const uint64_t us = static_cast<uint64_t>(ms < 0.0 ? 0.0 : ms * 1000.0);
    c.lastUs.store(us, std::memory_order_relaxed);
    c.totalUs.fetch_add(us, std::memory_order_relaxed);
    c.count.fetch_add(1, std::memory_order_relaxed);
    uint64_t prevMax = c.maxUs.load(std::memory_order_relaxed);
    while (us > prevMax &&
           !c.maxUs.compare_exchange_weak(prevMax, us, std::memory_order_relaxed)) {}
}

inline ChunkPerfStat getChunkPerf(ChunkPerfPhase phase) {
    auto& c = detail::chunkPerfCounters()[static_cast<int>(phase)];
    ChunkPerfStat s;
    s.count  = c.count.load(std::memory_order_relaxed);
    s.lastMs = c.lastUs.load(std::memory_order_relaxed) / 1000.0;
    s.maxMs  = c.maxUs.load(std::memory_order_relaxed) / 1000.0;
    const uint64_t total = c.totalUs.load(std::memory_order_relaxed);
    s.avgMs = s.count ? (static_cast<double>(total) / static_cast<double>(s.count)) / 1000.0 : 0.0;
    return s;
}

inline void resetChunkPerf() {
    for (auto& c : detail::chunkPerfCounters()) {
        c.count.store(0, std::memory_order_relaxed);
        c.lastUs.store(0, std::memory_order_relaxed);
        c.maxUs.store(0, std::memory_order_relaxed);
        c.totalUs.store(0, std::memory_order_relaxed);
    }
}

// RAII scope timer: records the phase's wall time on scope exit (covers every return path).
struct ScopedChunkPerf {
    ChunkPerfPhase phase;
    std::chrono::high_resolution_clock::time_point start;
    explicit ScopedChunkPerf(ChunkPerfPhase p)
        : phase(p), start(std::chrono::high_resolution_clock::now()) {}
    ~ScopedChunkPerf() {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start).count();
        recordChunkPerf(phase, ms);
    }
};

} // namespace Graphics
} // namespace Phyxel
