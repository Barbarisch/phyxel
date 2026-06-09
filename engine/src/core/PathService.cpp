#include "core/PathService.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

PathService::PathService(NavGraph* graph) : m_graph(graph) {}

PathService::~PathService() {
    stop();
}

void PathService::start() {
    if (m_running.exchange(true)) return;   // already running
    m_worker = std::thread(&PathService::workerLoop, this);
    LOG_INFO("PathService", "Worker thread started");
}

void PathService::stop() {
    if (!m_running.exchange(false)) return;  // already stopped
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_results.clear();
    m_cancelled.clear();
    m_cache.clear();
    m_lru.clear();
    LOG_INFO("PathService", "Worker thread stopped");
}

PathService::CacheKey PathService::makeKey(const Request& r) {
    auto fl = [](float v) { return static_cast<int>(std::floor(v)); };
    return CacheKey{
        fl(r.from.x), fl(r.from.y), fl(r.from.z),
        fl(r.to.x),   fl(r.to.y),   fl(r.to.z),
        r.agent.height, r.agent.stepHeight, r.agent.maxFallY, r.agent.jumpHeight, r.agent.canClimb
    };
}

// Move an entry to the front of the LRU list. Caller holds m_mutex.
void PathService::cacheTouch(std::unordered_map<CacheKey, CacheEntry, CacheKeyHash>::iterator it) {
    m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);   // O(1) move to front
}

// Insert/refresh a cache entry, evicting the least-recently-used over capacity.
// Caller holds m_mutex.
void PathService::cacheStore(const CacheKey& key, const NavGraph::PathResult& result) {
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {            // refresh existing
        it->second.result = result;
        cacheTouch(it);
        return;
    }
    m_lru.push_front(key);
    m_cache.emplace(key, CacheEntry{result, m_lru.begin()});
    if (m_cache.size() > kCacheCap) {     // evict LRU
        const CacheKey& victim = m_lru.back();
        m_cache.erase(victim);
        m_lru.pop_back();
    }
}

PathService::Handle PathService::requestPath(const NavAgentProfile& agent, const glm::vec3& from,
                                             const glm::vec3& to, int priority) {
    if (!m_running.load() || !m_graph) return kInvalid;
    const Handle h = m_nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(Request{h, agent, from, to, priority});
    }
    m_cv.notify_one();
    return h;
}

bool PathService::tryGetResult(Handle h, NavGraph::PathResult& out) {
    if (h == kInvalid) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_results.find(h);
    if (it == m_results.end()) return false;
    out = std::move(it->second);
    m_results.erase(it);
    return true;
}

void PathService::cancel(Handle h) {
    if (h == kInvalid) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results.erase(h);
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
        if (it->handle == h) { m_queue.erase(it); return; }   // still queued — just drop it
    }
    m_cancelled.insert(h);   // may be in flight; the worker will discard its result
}

size_t PathService::pendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void PathService::workerLoop() {
    while (true) {
        Request req;
        CacheKey key{};
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running.load() || !m_queue.empty(); });
            if (!m_running.load()) return;

            // Dequeue the highest-priority request (ties: oldest first). Linear scan —
            // the in-flight NPC count is small; revisit with a heap if it grows.
            auto best = m_queue.begin();
            for (auto it = std::next(m_queue.begin()); it != m_queue.end(); ++it) {
                if (it->priority > best->priority) best = it;
            }
            req = *best;
            m_queue.erase(best);

            // Cache hit? Serve immediately without touching the graph. Still under the lock.
            key = makeKey(req);
            auto ci = m_cache.find(key);
            if (ci != m_cache.end()) {
                ++m_cacheHits;
                cacheTouch(ci);
                if (!m_cancelled.erase(req.handle))
                    m_results[req.handle] = ci->second.result;   // copy out
                continue;
            }
        }

        // Miss: heavy work OUTSIDE our lock; NavGraph::findPath takes its own (shared) lock.
        NavGraph::PathResult result = m_graph->findPath(req.from, req.to, req.agent);
        // String-pull so agents walk straight diagonals instead of stair-stepping along
        // the 4-connected grid. Worker-side, so the main thread pays nothing for it.
        if (result.found && result.waypoints.size() > 2)
            result.waypoints = m_graph->smoothWaypoints(result.waypoints, req.agent);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cacheStore(key, result);                       // cache even cancelled results — reusable
            if (m_cancelled.erase(req.handle)) continue;   // cancelled mid-flight — discard delivery
            m_results[req.handle] = std::move(result);
        }
    }
}

// --- Cache invalidation (main thread) ---

void PathService::invalidateAllCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_lru.clear();
}

// True if any unsmoothed node of `r` lies within Chebyshev `radius` of column (x,z).
static bool pathTouchesColumn(const NavGraph::PathResult& r, int x, int z, int radius) {
    for (const NavNodeId& n : r.nodes) {
        if (std::abs(n.x - x) <= radius && std::abs(n.z - z) <= radius) return true;
    }
    return false;
}

void PathService::invalidateCacheNear(int x, int z, int radius) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (pathTouchesColumn(it->second.result, x, z, radius)) {
            m_lru.erase(it->second.lruIt);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void PathService::invalidateCacheRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ, int margin) {
    const int x0 = std::min(minXZ.x, maxXZ.x) - margin, x1 = std::max(minXZ.x, maxXZ.x) + margin;
    const int z0 = std::min(minXZ.y, maxXZ.y) - margin, z1 = std::max(minXZ.y, maxXZ.y) + margin;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        bool hit = false;
        for (const NavNodeId& n : it->second.result.nodes) {
            if (n.x >= x0 && n.x <= x1 && n.z >= z0 && n.z <= z1) { hit = true; break; }
        }
        if (hit) {
            m_lru.erase(it->second.lruIt);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

size_t PathService::cacheSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.size();
}

size_t PathService::cacheHits() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cacheHits;
}

} // namespace Core
} // namespace Phyxel
