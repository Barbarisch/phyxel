#pragma once

#include "core/NavGraph.h"
#include <glm/glm.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Phyxel {
namespace Core {

// ============================================================================
// PathService — async owner of NavGraph path queries (Layer-1 cross-cutting
// service from docs/NavigationArchitecture.md). Mirrors AI::TTSService's
// worker-thread model.
//
// Path queries are A* over the NavGraph, which is cheap individually but adds
// up across many NPCs and must not block the frame. Callers submit a request
// (requestPath) and get back a Handle immediately; a worker thread runs the
// query against the (thread-safe) NavGraph and stashes the result. The caller
// polls tryGetResult() on later frames and keeps moving / holds meanwhile.
//
// This first slice is the async core only: queue + worker + poll. Caching,
// voxel-change dirtying/re-queue, smoothing, and ORCA/RVO local avoidance are
// follow-up slices (see the design doc).
// ============================================================================
class PathService {
public:
    using Handle = uint64_t;
    static constexpr Handle kInvalid = 0;

    /// Non-owning NavGraph pointer; must outlive the service. start() the worker
    /// before submitting requests; the destructor stop()s and joins it.
    explicit PathService(NavGraph* graph);
    ~PathService();

    PathService(const PathService&) = delete;
    PathService& operator=(const PathService&) = delete;

    void start();   ///< spin up the worker thread (idempotent).
    void stop();    ///< signal + join the worker, drop pending work (idempotent).

    /// Queue an A* query. Returns a handle to poll, or kInvalid if not running.
    /// Higher `priority` requests are dequeued first (nearby/visible NPCs win);
    /// ties resolve FIFO.
    Handle requestPath(const NavAgentProfile& agent, const glm::vec3& from,
                       const glm::vec3& to, int priority = 0);

    /// If the query for `h` has finished, move its result into `out`, forget it,
    /// and return true. Otherwise return false (still queued / in flight / unknown).
    bool tryGetResult(Handle h, NavGraph::PathResult& out);

    /// Forget a handle: drop it if still queued, discard its result if already
    /// computed, and ignore it if it lands mid-flight. Safe to call with any handle.
    void cancel(Handle h);

    // --- Path cache invalidation (call from the main thread on terrain changes) ---
    // The worker caches computed paths keyed by quantized from/to cell + agent profile, so
    // repeated/identical queries skip A*. Terrain edits make cached paths stale, so the
    // owner (NPCManager) must evict affected entries when voxels change.

    /// Drop every cached path (e.g. a wholesale world/region rebuild).
    void invalidateAllCache();
    /// Evict cached paths that pass within `radius` (Chebyshev, cells) of column (x,z).
    /// Tested against the unsmoothed node path so a straight smoothed segment can't hide
    /// a crossing.
    void invalidateCacheNear(int x, int z, int radius = 1);
    /// Evict cached paths with any node inside the XZ box [minXZ,maxXZ] expanded by margin.
    void invalidateCacheRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ, int margin = 1);

    bool   running() const { return m_running.load(); }
    size_t pendingCount() const;   ///< queued requests (diagnostics/tests).
    size_t cacheSize() const;      ///< cached path count (diagnostics/tests).
    size_t cacheHits() const;      ///< cumulative cache hits (diagnostics/tests).

private:
    void workerLoop();

    struct Request {
        Handle          handle;
        NavAgentProfile agent;
        glm::vec3       from;
        glm::vec3       to;
        int             priority;
    };

    // Cache key: quantized from/to cell + the agent-profile fields that change a path.
    struct CacheKey {
        int fx, fy, fz, tx, ty, tz;
        int height, stepHeight, maxFallY, jumpHeight;
        bool canClimb;
        bool operator==(const CacheKey& o) const {
            return fx==o.fx && fy==o.fy && fz==o.fz && tx==o.tx && ty==o.ty && tz==o.tz &&
                   height==o.height && stepHeight==o.stepHeight && maxFallY==o.maxFallY &&
                   jumpHeight==o.jumpHeight && canClimb==o.canClimb;
        }
    };
    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const noexcept {
            size_t h = 1469598103934665603ull;
            auto mix = [&h](int v) { h = (h ^ static_cast<size_t>(static_cast<uint32_t>(v))) * 1099511628211ull; };
            mix(k.fx); mix(k.fy); mix(k.fz); mix(k.tx); mix(k.ty); mix(k.tz);
            mix(k.height); mix(k.stepHeight); mix(k.maxFallY); mix(k.jumpHeight); mix(k.canClimb);
            return h;
        }
    };
    struct CacheEntry {
        NavGraph::PathResult        result;
        std::list<CacheKey>::iterator lruIt;   // position in m_lru (front = most recent)
    };

    static CacheKey makeKey(const Request& r);
    void   cacheTouch(std::unordered_map<CacheKey, CacheEntry, CacheKeyHash>::iterator it);
    void   cacheStore(const CacheKey& key, const NavGraph::PathResult& result);

    static constexpr size_t kCacheCap = 512;   // LRU capacity (paths are cheap to recompute)

    NavGraph* m_graph = nullptr;

    std::thread             m_worker;
    mutable std::mutex      m_mutex;            ///< guards queue/results/cancelled/cache.
    std::condition_variable m_cv;
    std::deque<Request>     m_queue;
    std::unordered_map<Handle, NavGraph::PathResult> m_results;
    std::unordered_set<Handle> m_cancelled;     ///< handles cancelled while queued/in flight.

    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> m_cache;
    std::list<CacheKey> m_lru;                   ///< LRU order; front = most recently used.
    size_t m_cacheHits = 0;

    std::atomic<bool>   m_running{false};
    std::atomic<Handle> m_nextHandle{1};
};

} // namespace Core
} // namespace Phyxel
