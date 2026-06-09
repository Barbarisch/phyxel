#include <gtest/gtest.h>
#include "core/PathService.h"
#include "core/NavGraph.h"
#include <chrono>
#include <thread>

using namespace Phyxel;
using Phyxel::Core::NavGraph;
using Phyxel::Core::NavAgentProfile;
using Phyxel::Core::PathService;

namespace {

// Flat solid ground plane at y=0 everywhere (one walkable surface per column).
Core::VoxelQueryFunc flatWorld() {
    return [](const glm::ivec3& p) -> bool { return p.y == 0; };
}

NavAgentProfile humanoid() {
    NavAgentProfile a; a.height = 2; a.stepHeight = 1; a.maxFallY = 4; return a;
}

// Poll a handle until it resolves or we time out. Returns true if a result arrived.
bool waitForResult(PathService& svc, PathService::Handle h, NavGraph::PathResult& out,
                   int timeoutMs = 2000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (svc.tryGetResult(h, out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

class PathServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_unique<NavGraph>(flatWorld());
        graph->buildRegion({0, 0}, {16, 16}, humanoid());
        svc = std::make_unique<PathService>(graph.get());
        svc->start();
    }
    void TearDown() override { svc->stop(); }

    std::unique_ptr<NavGraph> graph;
    std::unique_ptr<PathService> svc;
};

// A queued request resolves to the same path the synchronous query produces. The
// service smooths waypoints (string-pull), so compare against the smoothed reference;
// the unsmoothed node sequence still matches the raw A* result end-to-end.
TEST_F(PathServiceTest, AsyncPathMatchesSyncPath) {
    const glm::vec3 from(0.5f, 1.0f, 0.5f);
    const glm::vec3 to(8.5f, 1.0f, 6.5f);

    NavGraph::PathResult sync = graph->findPath(from, to, humanoid());
    ASSERT_TRUE(sync.found);
    std::vector<glm::vec3> expected = graph->smoothWaypoints(sync.waypoints, humanoid());

    PathService::Handle h = svc->requestPath(humanoid(), from, to);
    ASSERT_NE(h, PathService::kInvalid);

    NavGraph::PathResult async;
    ASSERT_TRUE(waitForResult(*svc, h, async)) << "worker never delivered a result";
    EXPECT_TRUE(async.found);
    EXPECT_EQ(async.waypoints.size(), expected.size());   // smoothed, fewer than raw
    EXPECT_LT(async.waypoints.size(), sync.waypoints.size()) << "flat path should string-pull";
    EXPECT_EQ(async.nodes.front(), sync.nodes.front());
    EXPECT_EQ(async.nodes.back(), sync.nodes.back());
}

// Polling an unknown / not-yet-ready handle returns false (no spurious results).
TEST_F(PathServiceTest, TryGetResultFalseUntilReady) {
    NavGraph::PathResult out;
    EXPECT_FALSE(svc->tryGetResult(12345u, out));   // never requested
    EXPECT_FALSE(svc->tryGetResult(PathService::kInvalid, out));
}

// A result is delivered exactly once: the second poll of the same handle is empty.
TEST_F(PathServiceTest, ResultDeliveredOnce) {
    PathService::Handle h = svc->requestPath(humanoid(), {0.5f, 1.0f, 0.5f}, {4.5f, 1.0f, 4.5f});
    NavGraph::PathResult out;
    ASSERT_TRUE(waitForResult(*svc, h, out));
    EXPECT_FALSE(svc->tryGetResult(h, out)) << "the handle should be forgotten after retrieval";
}

// An unreachable goal (outside the built region → no surface) resolves to found=false.
TEST_F(PathServiceTest, UnreachableGoalResolvesNotFound) {
    PathService::Handle h = svc->requestPath(humanoid(), {0.5f, 1.0f, 0.5f}, {500.5f, 1.0f, 500.5f});
    NavGraph::PathResult out;
    ASSERT_TRUE(waitForResult(*svc, h, out));
    EXPECT_FALSE(out.found);
}

// Cancelling a handle means its result is never handed back.
TEST_F(PathServiceTest, CancelDiscardsResult) {
    PathService::Handle h = svc->requestPath(humanoid(), {0.5f, 1.0f, 0.5f}, {9.5f, 1.0f, 9.5f});
    svc->cancel(h);
    NavGraph::PathResult out;
    // Even after the worker would have finished, a cancelled handle yields nothing.
    EXPECT_FALSE(waitForResult(*svc, h, out, 300));
}

// requestPath after stop() returns kInvalid (service not running).
TEST_F(PathServiceTest, RequestAfterStopIsInvalid) {
    svc->stop();
    EXPECT_EQ(svc->requestPath(humanoid(), {0.5f, 1.0f, 0.5f}, {1.5f, 1.0f, 1.5f}),
              PathService::kInvalid);
}

// A repeated identical query is served from cache (no second A*), and the cache holds
// the computed path.
TEST_F(PathServiceTest, CacheHitOnRepeatedQuery) {
    const glm::vec3 from(0.5f, 1.0f, 0.5f), to(8.5f, 1.0f, 8.5f);

    NavGraph::PathResult r1;
    ASSERT_TRUE(waitForResult(*svc, svc->requestPath(humanoid(), from, to), r1));
    EXPECT_EQ(svc->cacheHits(), 0u);      // first was a miss
    EXPECT_GE(svc->cacheSize(), 1u);

    NavGraph::PathResult r2;
    ASSERT_TRUE(waitForResult(*svc, svc->requestPath(humanoid(), from, to), r2));
    EXPECT_EQ(svc->cacheHits(), 1u);      // second served from cache
    EXPECT_EQ(r2.waypoints.size(), r1.waypoints.size());
    EXPECT_TRUE(r2.found);
}

// invalidateAllCache() empties the cache.
TEST_F(PathServiceTest, InvalidateAllClearsCache) {
    NavGraph::PathResult r;
    ASSERT_TRUE(waitForResult(*svc, svc->requestPath(humanoid(), {0.5f, 1, 0.5f}, {6.5f, 1, 6.5f}), r));
    ASSERT_GE(svc->cacheSize(), 1u);
    svc->invalidateAllCache();
    EXPECT_EQ(svc->cacheSize(), 0u);
}

// invalidateCacheNear evicts only paths that cross the given column.
TEST_F(PathServiceTest, InvalidateNearEvictsCrossingPathOnly) {
    NavGraph::PathResult r;
    ASSERT_TRUE(waitForResult(*svc, svc->requestPath(humanoid(), {0.5f, 1, 0.5f}, {8.5f, 1, 8.5f}), r));
    ASSERT_EQ(svc->cacheSize(), 1u);

    svc->invalidateCacheNear(15, 0, 0);   // far from the (0,0)->(8,8) path: survives
    EXPECT_EQ(svc->cacheSize(), 1u);

    svc->invalidateCacheNear(0, 0, 0);     // start column is on the path: evicted
    EXPECT_EQ(svc->cacheSize(), 0u);
}

// Stress: many concurrent queries while the graph is rebuilt on this (main) thread.
// Exercises the shared/exclusive locking — must not crash, deadlock, or corrupt.
TEST_F(PathServiceTest, ConcurrentQueriesDuringRebuild) {
    std::vector<PathService::Handle> handles;
    for (int i = 0; i < 200; ++i) {
        handles.push_back(svc->requestPath(humanoid(), {0.5f, 1.0f, 0.5f},
                                           {float(i % 16) + 0.5f, 1.0f, float((i * 3) % 16) + 0.5f}));
    }
    // Hammer the graph with rebuilds (exclusive lock) while the worker reads (shared lock).
    for (int r = 0; r < 200; ++r) graph->rebuildColumn(r % 16, (r * 5) % 16, humanoid());

    int delivered = 0;
    NavGraph::PathResult out;
    for (PathService::Handle h : handles) {
        if (waitForResult(*svc, h, out, 2000)) ++delivered;
    }
    EXPECT_EQ(delivered, static_cast<int>(handles.size()));
}
