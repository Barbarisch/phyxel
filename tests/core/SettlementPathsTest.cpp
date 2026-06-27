#include <gtest/gtest.h>

#include <unordered_map>

#include "core/PathPlanner.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// Settlement path network (3c) L3 — connecting buildings on real CUBE-resolution terrain must produce
// a WALKABLE network, not lines on a map. Adjacent terrain columns differ by a whole cube (9 micro) >
// the 4-micro step-up, so doors at different heights are unreachable from each other on bare ground;
// planSettlementPaths must grade the connections (cut/fill) so a TraversalProbe walks door->door.
// ============================================================================

namespace {
const AgentBox kAgent{2, 16, 4};

// Terraced terrain rising in X: each cube-column (9 micro) is flat, then steps up a whole cube (9 micro)
// -> a staircase of 9-micro risers, impassable to the 4-micro step-up. Flat in Z.
int terraceMicro(int x, int /*z*/) { return 9 + 9 * (x / 9); }

// Compose occupancy: a path corridor REPLACES the terrain column (fill below the graded surface, cut
// everything above it); off the corridor, bare terraces. Carve PERPENDICULAR to the local travel
// direction at the cell's EXACT height (a square patch would shift the surface along the run).
struct Net {
    std::unordered_map<long long, int> corridor;
    static long long key(int x, int z) { return (static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL); }
    void stamp(const SettlementPaths& net, int hw) {
        for (const auto& p : net.paths) {
            const auto& cs = p.cells;
            for (size_t i = 0; i < cs.size(); ++i) {
                bool tX = false, tZ = false;
                if (i + 1 < cs.size()) { tX |= cs[i + 1].x != cs[i].x; tZ |= cs[i + 1].z != cs[i].z; }
                if (i > 0)             { tX |= cs[i].x != cs[i - 1].x; tZ |= cs[i].z != cs[i - 1].z; }
                if (!tX && !tZ) tX = true;
                if (tX) for (int dz = -hw; dz <= hw; ++dz) corridor[key(cs[i].x, cs[i].z + dz)] = cs[i].surfaceY;
                if (tZ) for (int dx = -hw; dx <= hw; ++dx) corridor[key(cs[i].x + dx, cs[i].z)] = cs[i].surfaceY;
            }
        }
    }
    bool occ(int x, int y, int z) const {
        auto it = corridor.find(key(x, z));
        return y < (it != corridor.end() ? it->second : terraceMicro(x, z));
    }
};

DoorAnchor doorAt(int x, int z) { return {x, z, terraceMicro(x, z)}; }
}  // namespace

// Three buildings up a terraced slope: bare terrain blocks travel between them (9-micro steps); the
// graded network connects them so the character walks door 0 -> door 1 -> door 2.
TEST(SettlementPathsTest, NetworkConnectsBuildingsAcrossTerraces) {
    const std::vector<DoorAnchor> doors = {doorAt(18, 20), doorAt(54, 20), doorAt(90, 20)};
    auto ground = [](int x, int z) { return terraceMicro(x, z); };

    const SettlementPaths net = planSettlementPaths(doors, ground, kAgent);
    EXPECT_EQ(net.edges, 2);                            // spanning tree over 3 doors
    EXPECT_EQ(net.connected, 2) << "every spanning-tree edge should grade into a walkable ramp";
    EXPECT_TRUE(net.failedEdges.empty());

    const glm::ivec3 lo(0, 0, 0), hi(110, 130, 40);
    auto box = [&](const DoorAnchor& d) {
        return std::make_pair(glm::ivec3(d.x - 2, d.surfaceY - 1, d.z - 2),
                              glm::ivec3(d.x + 2, d.surfaceY + 1, d.z + 2));
    };
    // Start on the flat apron at door 0 (where a path meets a threshold), not on the immediately-climbing ramp.
    ASSERT_FALSE(net.paths.empty());
    const PathCell e0 = net.paths[0].cells.front();
    const glm::ivec3 start(e0.x, e0.surfaceY, e0.z);

    // TEETH: on bare terraces the far door is unreachable (9-micro steps exceed the step-up).
    TraversalProbe bare([](int x, int y, int z) { return y < terraceMicro(x, z); }, kAgent);
    auto b2 = box(doors[2]);
    ASSERT_FALSE(bare.reachable(start, b2.first, b2.second, lo, hi))
        << "bare terraced terrain must block travel to the far door (else no teeth)";

    Net net2; net2.stamp(net, kAgent.halfWidthMicro);
    TraversalProbe walk([&](int x, int y, int z) { return net2.occ(x, y, z); }, kAgent);
    for (size_t i = 1; i < doors.size(); ++i) {
        auto bx = box(doors[i]);
        EXPECT_TRUE(walk.reachable(start, bx.first, bx.second, lo, hi))
            << "the graded network should make door " << i << " reachable from door 0";
    }
}

// Every graded edge's route is walkable riser-by-riser (the path invariant), end to end.
TEST(SettlementPathsTest, EveryEdgeRouteWithinStepUp) {
    const std::vector<DoorAnchor> doors = {doorAt(18, 20), doorAt(54, 20), doorAt(90, 20)};
    auto ground = [](int x, int z) { return terraceMicro(x, z); };
    const SettlementPaths net = planSettlementPaths(doors, ground, kAgent);
    ASSERT_EQ(net.connected, 2);
    for (const auto& p : net.paths)
        for (size_t i = 1; i < p.cells.size(); ++i)
            EXPECT_LE(std::abs(p.cells[i].surfaceY - p.cells[i - 1].surfaceY), kAgent.maxStepUpMicro);
}

// An edge too steep for a straight ramp must be REPORTED (failedEdges), not silently dropped or faked
// as connected. Doors 4 micro apart but 90 micro up: (5-1)*gradeCap(2)=8 < 90 -> infeasible.
TEST(SettlementPathsTest, SteepEdgeReportedNotDropped) {
    const std::vector<DoorAnchor> doors = {{10, 10, 9}, {14, 10, 99}};
    auto ground = [](int, int) { return 9; };
    const SettlementPaths net = planSettlementPaths(doors, ground, kAgent);
    EXPECT_EQ(net.edges, 1);
    EXPECT_EQ(net.connected, 0) << "a too-steep edge must not be graded as a straight ramp";
    ASSERT_EQ(net.failedEdges.size(), 1u) << "the un-gradeable edge must be reported, not dropped";
    EXPECT_EQ(net.failedEdges[0], std::make_pair(0, 1));
    EXPECT_TRUE(net.paths.empty());
}

// A single door (or none) yields an empty, well-formed network (no crash, no phantom edges).
TEST(SettlementPathsTest, DegenerateInputsYieldEmptyNetwork) {
    auto ground = [](int x, int z) { return terraceMicro(x, z); };
    EXPECT_EQ(planSettlementPaths({}, ground, kAgent).edges, 0);
    EXPECT_EQ(planSettlementPaths({doorAt(10, 10)}, ground, kAgent).edges, 0);
}
