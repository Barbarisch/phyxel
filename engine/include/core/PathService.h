#pragma once

#include "core/NavGraph.h"
#include <glm/glm.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
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

    bool   running() const { return m_running.load(); }
    size_t pendingCount() const;   ///< queued requests (diagnostics/tests).

private:
    void workerLoop();

    struct Request {
        Handle          handle;
        NavAgentProfile agent;
        glm::vec3       from;
        glm::vec3       to;
        int             priority;
    };

    NavGraph* m_graph = nullptr;

    std::thread             m_worker;
    mutable std::mutex      m_mutex;            ///< guards queue/results/cancelled.
    std::condition_variable m_cv;
    std::deque<Request>     m_queue;
    std::unordered_map<Handle, NavGraph::PathResult> m_results;
    std::unordered_set<Handle> m_cancelled;     ///< handles cancelled while queued/in flight.
    std::atomic<bool>   m_running{false};
    std::atomic<Handle> m_nextHandle{1};
};

} // namespace Core
} // namespace Phyxel
