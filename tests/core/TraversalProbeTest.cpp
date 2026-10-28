#include <gtest/gtest.h>

#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

namespace {
// Agent: ~0.5 m wide (halfWidth 2), ~1.75 m tall (16), step-up 4 (~0.44 m).
const AgentBox kAgent{2, 16, 4};

// A z-corridor world: solid floor y in [0,3); side walls confine z to [8,15) so the agent must
// pass THROUGH x (no going around). An optional barrier wall across the corridor, and an optional
// low ceiling, let each test isolate one rule.
struct World {
    int barrierX0 = -1, barrierX1 = -1, barrierTop = 0;   // solid y in [3, barrierTop)
    int ceilX0 = 999, ceilX1 = 999, ceilY = 999;          // solid y in [ceilY, ceilY+3)
    bool occ(int x, int y, int z) const {
        if (y < 0) return true;
        if (y < 3) return true;                            // floor
        if (z < 8 || z >= 15) return y < 28;               // corridor side walls (tall)
        if (x >= barrierX0 && x < barrierX1 && y >= 3 && y < barrierTop) return true;
        if (x >= ceilX0 && x < ceilX1 && y >= ceilY && y < ceilY + 3) return true;
        return false;
    }
};

TraversalProbe probe(const World& w) {
    return TraversalProbe([w](int x, int y, int z) { return w.occ(x, y, z); }, kAgent);
}
// start at x=10 floor, goal near x=40 floor; bounds cover the corridor.
bool walk(const World& w) {
    return probe(w).reachable({10, 3, 11}, {38, 3, 8}, {44, 6, 14}, {0, 0, 0}, {60, 40, 22});
}
}  // namespace

TEST(TraversalProbeTest, FlatCorridorIsTraversable) {
    EXPECT_TRUE(walk(World{}));
}

TEST(TraversalProbeTest, LowLedgeWithinStepUpIsClimbed) {
    World w; w.barrierX0 = 24; w.barrierX1 = 27; w.barrierTop = 3 + 3;   // 3-micro step (<= 4)
    EXPECT_TRUE(walk(w)) << "agent should step up a riser within its step-up";
}

TEST(TraversalProbeTest, LedgeTallerThanStepUpBlocks) {
    World w; w.barrierX0 = 24; w.barrierX1 = 27; w.barrierTop = 3 + 5;   // 5-micro step (> 4)
    EXPECT_FALSE(walk(w)) << "agent must NOT climb a riser above its step-up";
}

TEST(TraversalProbeTest, TallWallBlocks) {
    World w; w.barrierX0 = 24; w.barrierX1 = 27; w.barrierTop = 20;      // full wall
    EXPECT_FALSE(walk(w));
}

TEST(TraversalProbeTest, LowCeilingBlocksEntry) {
    World w; w.ceilX0 = 20; w.ceilX1 = 45; w.ceilY = 3 + 10;             // head-room only 10 < 16
    EXPECT_FALSE(walk(w)) << "agent must not pass under a ceiling lower than its standing height";
}

TEST(TraversalProbeTest, AdequateCeilingAllowsPassage) {
    World w; w.ceilX0 = 20; w.ceilX1 = 45; w.ceilY = 3 + 16;             // head-room 16 >= height
    EXPECT_TRUE(walk(w));
}

// ---------------------------------------------------------------------------
// flood() — the reachable SET, so a failed reachability check can be LOCATED
// instead of merely reported. It must agree with reachable() (same stepping rule)
// AND actually stop at a barrier: a flood that leaks past a wall would point the
// settlement validator's pinch diagnosis at the wrong place.
// ---------------------------------------------------------------------------
TEST(TraversalProbeTest, FloodAgreesWithReachableOnAnOpenCorridor) {
    const World w;
    const auto set = probe(w).flood({10, 3, 11}, {0, 0, 0}, {60, 40, 22});
    ASSERT_FALSE(set.empty());
    // Everything reachable() accepts must appear in the flood.
    bool sawGoal = false;
    for (const auto& c : set)
        if (c.x >= 38 && c.x <= 44 && c.z >= 8 && c.z <= 14) { sawGoal = true; break; }
    EXPECT_TRUE(sawGoal) << "flood did not reach a cell reachable() says is reachable";
    EXPECT_TRUE(walk(w));
}

TEST(TraversalProbeTest, FloodStopsAtAWallAndStaysOnTheStartSide) {
    World w;
    w.barrierX0 = 24; w.barrierX1 = 27; w.barrierTop = 20;   // full wall across the corridor
    const auto set = probe(w).flood({10, 3, 11}, {0, 0, 0}, {60, 40, 22});
    ASSERT_FALSE(set.empty());
    int beyond = 0;
    for (const auto& c : set) if (c.x >= 27) ++beyond;
    EXPECT_EQ(beyond, 0) << beyond << " flooded cells leaked PAST a full-height wall";
    EXPECT_FALSE(walk(w));
}

TEST(TraversalProbeTest, FloodOfAnUnsupportedStartIsEmpty) {
    const World w;
    // Start in mid-air above the corridor with nothing to settle onto below within bounds:
    // bound the floor away so settle() runs out of world.
    const auto set = probe(w).flood({10, 30, 11}, {0, 25, 0}, {60, 40, 22});
    EXPECT_TRUE(set.empty()) << "an unsupported start must flood nothing (it is bad probe input, "
                                "not a walkable region)";
}

// The probe works anywhere in the world: the same corridor translated to the micro coordinates of
// a real WorldForge bridge (x = -3432 u, z = -4248 u -> -30888 / -38232 micro, BridgeSpanTest
// 2026-10-27) and far on the positive side. Red on the original BFS: its visited-set key packed
// each coordinate + 32768 into 17 bits, so below -32768 micro (-3641 u) or above 98303 micro the
// fields went negative or overflowed and collided - every step read as already visited and the
// probe called a flat deck unwalkable. The walkability gate at any site beyond 3.6 km from the
// origin inherited the same false "unreachable".
TEST(TraversalProbeTest, FarCoordinatesTraverseLikeTheOrigin) {
    for (const glm::ivec3 off : {glm::ivec3(-30888, 0, -38232), glm::ivec3(200000, 0, 150000), glm::ivec3(-30888, 500, 20)}) {
        World w;
        TraversalProbe pr([w, off](int x, int y, int z) { return w.occ(x - off.x, y - off.y, z - off.z); }, kAgent);
        const bool ok = pr.reachable(glm::ivec3(10, 3, 11) + off, glm::ivec3(38, 3, 8) + off, glm::ivec3(44, 6, 14) + off,
                                     glm::ivec3(0, 0, 0) + off, glm::ivec3(60, 40, 22) + off);
        EXPECT_TRUE(ok) << "the flat corridor is unwalkable when translated by (" << off.x << "," << off.y << "," << off.z << ") micro";
        World wall; wall.barrierX0 = 24; wall.barrierX1 = 27; wall.barrierTop = 3 + 12;   // control: a wall still blocks there
        TraversalProbe pw([wall, off](int x, int y, int z) { return wall.occ(x - off.x, y - off.y, z - off.z); }, kAgent);
        EXPECT_FALSE(pw.reachable(glm::ivec3(10, 3, 11) + off, glm::ivec3(38, 3, 8) + off, glm::ivec3(44, 6, 14) + off,
                                  glm::ivec3(0, 0, 0) + off, glm::ivec3(60, 40, 22) + off));
    }
}
