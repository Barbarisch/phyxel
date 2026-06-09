#include "core/PathService.h"
#include "utils/Logger.h"
#include <algorithm>

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
    LOG_INFO("PathService", "Worker thread stopped");
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
        }

        // Heavy work OUTSIDE our lock; NavGraph::findPath takes its own (shared) lock.
        NavGraph::PathResult result = m_graph->findPath(req.from, req.to, req.agent);
        // String-pull so agents walk straight diagonals instead of stair-stepping along
        // the 4-connected grid. Worker-side, so the main thread pays nothing for it.
        if (result.found && result.waypoints.size() > 2)
            result.waypoints = m_graph->smoothWaypoints(result.waypoints, req.agent);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cancelled.erase(req.handle)) continue;   // cancelled mid-flight — discard
            m_results[req.handle] = std::move(result);
        }
    }
}

} // namespace Core
} // namespace Phyxel
