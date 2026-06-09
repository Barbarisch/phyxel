#include <gtest/gtest.h>
#include "core/NavGraph.h"
#include <algorithm>

using namespace Phyxel;
using Phyxel::Core::NavGraph;
using Phyxel::Core::NavNodeId;
using Phyxel::Core::NavAgentProfile;

namespace {

// Test world: solid ground plane at y=0 everywhere, plus an elevated platform at
// y=5 over the 2..3 x 2..3 patch (with open air between ground and platform).
Core::VoxelQueryFunc twoLevelWorld() {
    return [](const glm::ivec3& p) -> bool {
        if (p.y == 0) return true;                                   // ground
        if (p.y == 5 && p.x >= 2 && p.x <= 3 && p.z >= 2 && p.z <= 3) // platform
            return true;
        return false;
    };
}

NavAgentProfile humanoid() {
    NavAgentProfile a; a.height = 2; a.stepHeight = 1; a.maxFallY = 4; return a;
}

bool contains(const std::vector<NavNodeId>& v, const NavNodeId& n) {
    return std::find(v.begin(), v.end(), n) != v.end();
}

} // namespace

class NavGraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_unique<NavGraph>(twoLevelWorld());
        graph->buildRegion({0, 0}, {4, 4}, humanoid());
    }
    std::unique_ptr<NavGraph> graph;
};

TEST_F(NavGraphTest, OpenGroundHasOneSurface) {
    const auto& col = graph->columnSurfaces(0, 0);
    ASSERT_EQ(col.size(), 1u);
    EXPECT_EQ(col[0].floorY, 0);
}

TEST_F(NavGraphTest, ColumnUnderPlatformHasTwoSurfaces) {
    const auto& col = graph->columnSurfaces(2, 2);
    ASSERT_EQ(col.size(), 2u) << "ground + platform top should both be standable";
    EXPECT_EQ(col[0].floorY, 0);   // ground (scanned first, bottom-to-top)
    EXPECT_EQ(col[1].floorY, 5);   // platform
}

TEST_F(NavGraphTest, SurfaceCountAccountsForBothLevels) {
    // 25 ground columns (5x5) + 4 platform tops (2x2) = 29 surfaces.
    EXPECT_EQ(graph->surfaceCount(), 29u);
}

TEST_F(NavGraphTest, GroundNeighborsConnectOnSameLevel) {
    auto n = graph->neighbors(NavNodeId{0, 0, 0}, humanoid());
    // Corner of the region: only +x and +z neighbors exist.
    EXPECT_TRUE(contains(n, NavNodeId{1, 0, 0}));
    EXPECT_TRUE(contains(n, NavNodeId{0, 1, 0}));
    EXPECT_EQ(n.size(), 2u);
}

TEST_F(NavGraphTest, TallStepIsNotTraversable) {
    // From ground at (2,1) toward column (2,2): the ground level (floorY 0) connects,
    // but the platform top (floorY 5) is a 5-block step — beyond stepHeight=1.
    auto n = graph->neighbors(NavNodeId{2, 1, 0}, humanoid());
    EXPECT_TRUE(contains(n, NavNodeId{2, 2, 0}))  << "ground-to-ground step should connect";
    EXPECT_FALSE(contains(n, NavNodeId{2, 2, 1})) << "5-block step up must not connect";
}

TEST_F(NavGraphTest, SurfaceAtResolvesCorrectLevel) {
    // Standing on the ground under the platform.
    NavNodeId onGround = graph->surfaceAt(glm::vec3(2.5f, 1.0f, 2.5f));
    ASSERT_TRUE(onGround.valid());
    EXPECT_EQ(onGround.level, 0);

    // Standing on top of the platform.
    NavNodeId onPlatform = graph->surfaceAt(glm::vec3(2.5f, 6.0f, 2.5f));
    ASSERT_TRUE(onPlatform.valid());
    EXPECT_EQ(onPlatform.level, 1);
}

TEST_F(NavGraphTest, RebuildColumnIsIncremental) {
    // Removing the platform voxels from the query would need a new graph; instead verify
    // rebuildColumn re-detects the same surfaces (idempotent) without touching neighbors.
    graph->rebuildColumn(2, 2, humanoid());
    EXPECT_EQ(graph->columnSurfaces(2, 2).size(), 2u);
}

// ============================================================================
// A* path queries
// ============================================================================

namespace {
// Ground plane at y=0, plus a 2-tall wall at x==2 spanning z=0..4. If `gap` is true,
// the wall is open at z==0 so agents can squeeze around.
Core::VoxelQueryFunc wallWorld(bool gap) {
    return [gap](const glm::ivec3& p) -> bool {
        if (p.y == 0) return true;                                  // ground
        if (p.x == 2 && (p.y == 1 || p.y == 2) && p.z >= 0 && p.z <= 4) {
            if (gap && p.z == 0) return false;                      // leave a gap at z=0
            return true;
        }
        return false;
    };
}
} // namespace

TEST_F(NavGraphTest, PathToSelfIsSingleWaypoint) {
    auto path = graph->findPath(glm::vec3(0.5f, 1.0f, 0.5f), glm::vec3(0.5f, 1.0f, 0.5f), humanoid());
    ASSERT_TRUE(path.found);
    EXPECT_EQ(path.waypoints.size(), 1u);
}

TEST_F(NavGraphTest, PathAcrossFlatGround) {
    auto path = graph->findPath(glm::vec3(0.5f, 1.0f, 0.5f), glm::vec3(4.5f, 1.0f, 4.5f), humanoid());
    ASSERT_TRUE(path.found);
    EXPECT_EQ(path.nodes.front(), (NavNodeId{0, 0, 0}));
    EXPECT_EQ(path.nodes.back(),  (NavNodeId{4, 4, 0}));
    EXPECT_EQ(path.waypoints.size(), path.nodes.size());
    EXPECT_GT(path.nodesExpanded, 0);
}

TEST(NavGraphAStar, RoutesAroundWallThroughGap) {
    NavGraph g(wallWorld(/*gap=*/true));
    g.buildRegion({0, 0}, {4, 4}, humanoid());
    // Cross the wall from left (x=0) to right (x=4) at z=2: must detour through the z=0 gap.
    auto path = g.findPath(glm::vec3(0.5f, 1.0f, 2.5f), glm::vec3(4.5f, 1.0f, 2.5f), humanoid());
    ASSERT_TRUE(path.found);
    EXPECT_GT(path.nodes.size(), 5u) << "should be longer than the blocked straight line";
    bool usesGap = std::any_of(path.nodes.begin(), path.nodes.end(),
                               [](const NavNodeId& n) { return n.z == 0; });
    EXPECT_TRUE(usesGap) << "detour must pass through the z=0 gap";
}

TEST(NavGraphAStar, NoPathWhenFullyWalled) {
    NavGraph g(wallWorld(/*gap=*/false));
    g.buildRegion({0, 0}, {4, 4}, humanoid());
    auto path = g.findPath(glm::vec3(0.5f, 1.0f, 2.5f), glm::vec3(4.5f, 1.0f, 2.5f), humanoid());
    EXPECT_FALSE(path.found) << "a solid wall with no gap and no climbable step blocks the path";
}

// --- Path smoothing (string-pull) ---

// A flat 4-connected staircase collapses to a straight diagonal (start + end only).
TEST_F(NavGraphTest, SmoothCollapsesFlatPath) {
    auto path = graph->findPath(glm::vec3(0.5f, 1.0f, 0.5f), glm::vec3(4.5f, 1.0f, 4.5f), humanoid());
    ASSERT_TRUE(path.found);
    ASSERT_GT(path.waypoints.size(), 2u) << "raw 4-connected path should zig-zag";
    auto smooth = graph->smoothWaypoints(path.waypoints, humanoid());
    EXPECT_EQ(smooth.size(), 2u);
    EXPECT_EQ(smooth.front(), path.waypoints.front());
    EXPECT_EQ(smooth.back(),  path.waypoints.back());
}

// hasClearWalk: open across flat ground, blocked through a wall.
TEST(NavGraphSmoothing, ClearWalkRespectsWalls) {
    NavGraph g(wallWorld(/*gap=*/true));
    g.buildRegion({0, 0}, {4, 4}, humanoid());
    EXPECT_TRUE (g.hasClearWalk({0.5f, 1.0f, 0.5f}, {0.5f, 1.0f, 4.5f}, humanoid())); // open lane at x=0
    EXPECT_FALSE(g.hasClearWalk({0.5f, 1.0f, 2.5f}, {4.5f, 1.0f, 2.5f}, humanoid())); // straight through wall
}

// Smoothing a wall detour must NOT shortcut through the wall: every kept segment is walkable.
TEST(NavGraphSmoothing, DetourNotShortcutThroughWall) {
    NavGraph g(wallWorld(/*gap=*/true));
    g.buildRegion({0, 0}, {4, 4}, humanoid());
    auto path = g.findPath(glm::vec3(0.5f, 1.0f, 2.5f), glm::vec3(4.5f, 1.0f, 2.5f), humanoid());
    ASSERT_TRUE(path.found);
    auto smooth = g.smoothWaypoints(path.waypoints, humanoid());
    EXPECT_GT(smooth.size(), 2u) << "cannot collapse to a straight line through the wall";
    for (size_t i = 1; i < smooth.size(); ++i)
        EXPECT_TRUE(g.hasClearWalk(smooth[i - 1], smooth[i], humanoid()))
            << "smoothed segment " << i << " tunnels through an obstacle";
}
