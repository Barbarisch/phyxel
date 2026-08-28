#include <gtest/gtest.h>

#include <set>

#include "core/TowerForge.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// A tower has to WORK, not just read as one from outside (user, 2026-08-28:
// "not some crude imitation that you claim is a structure but is actually just
// a dumb pile of voxels"). The acceptance test is therefore not a screenshot
// and not a cell count: an AGENT WALKS IN THE DOOR AND CLIMBS TO THE TOP using
// the engine's own AgentBox rules — 4-micro step-up, 16-micro headroom.
//
// That is also why the treads are subcube plates: a full-cube step is 9 micro
// and the agent cannot take it, so a cube stair is decoration.
// ============================================================================

namespace {

TowerSpec spec() {
    TowerSpec s;
    s.shape = "round";
    s.heightCubes = 12;
    s.storeyCubes = 3;
    s.arrowLoops = true;
    s.doorSide = 'S';
    return s;
}

Rect bbox(int size) { return Rect{0, 0, size, size}; }

/// Rasterize a plan into micro occupancy — the same solid set the settlement stamps.
struct TowerWorld {
    std::set<std::tuple<int, int, int>> solid;

    explicit TowerWorld(const TowerPlan& p) {
        for (const auto& w : p.walls)
            for (int y = w.fromMicroY; y < w.toMicroY; ++y)
                for (int mx = 0; mx < 9; ++mx)
                    for (int mz = 0; mz < 9; ++mz)
                        solid.insert({w.cx * 9 + mx, y, w.cz * 9 + mz});
        for (const auto& pl : p.plates)
            for (int y = pl.yMicro; y < pl.yMicro + pl.thicknessMicro; ++y)
                for (int mx = 0; mx < 9; ++mx)
                    for (int mz = 0; mz < 9; ++mz)
                        solid.insert({pl.cx * 9 + mx, y, pl.cz * 9 + mz});
    }

    std::function<bool(int, int, int)> occ() const {
        return [this](int x, int y, int z) {
            if (y < 0) return true;                    // ground under the tower
            return solid.count({x, y, z}) > 0;
        };
    }
};

}  // namespace

// Calibration: before trusting any tower result, confirm what the probe considers a
// climbable step in a world I control completely. Ground at y<0, one 3-micro plate.
TEST(TowerForgeTest, ProbeCalibrationASubcubeStepIsClimbable) {
    auto occ = [](int x, int y, int z) {
        if (y < 0) return true;                       // ground
        if (x >= 9 && x < 18 && z >= 0 && z < 9 && y >= 0 && y < 3) return true;  // the step
        return false;
    };
    TraversalProbe probe(occ, AgentBox{});
    EXPECT_TRUE(probe.fits(4, 0, 4)) << "the agent cannot stand on open ground";
    EXPECT_TRUE(probe.supported(4, 0, 4));
    EXPECT_TRUE(probe.fits(13, 3, 4)) << "the agent cannot stand on the 3-micro plate";
    EXPECT_TRUE(probe.supported(13, 3, 4));
    const bool climbed = probe.reachable({4, 0, 4}, {13, 3, 4}, {13, 3, 4},
                                         {-9, 0, -9}, {27, 27, 18});
    EXPECT_TRUE(climbed) << "the probe will not take a 3-micro step up — then no subcube "
                            "stair can ever pass, and the tower test is measuring the wrong thing";
}

TEST(TowerForgeTest, RefusesAFootprintTooSmallToBeATower) {
    const auto tiny = planTower(bbox(4), spec());
    EXPECT_FALSE(tiny.ok);
    EXPECT_FALSE(tiny.refusal.empty()) << "refused without saying why";

    TowerSpec stubby = spec();
    stubby.heightCubes = 4;                            // under two storeys
    const auto flat = planTower(bbox(8), stubby);
    EXPECT_FALSE(flat.ok);
}

TEST(TowerForgeTest, HasAHollowShaftFloorsAndADoorway) {
    const auto p = planTower(bbox(8), spec());
    ASSERT_TRUE(p.ok) << p.refusal;
    EXPECT_GE(p.floorCount, 2) << "a tower with fewer than two floors is a plinth";
    EXPECT_FALSE(p.walls.empty());
    EXPECT_FALSE(p.loopCells.empty()) << "no arrow loops — a mural tower cannot be fought from";

    // The doorway is a REAL gap: the wall runs in the door column must leave the
    // agent's height clear at the bottom.
    int lowestWallTop = 1 << 30;
    const int doorCx = p.doorFeetMicro.x / 9, doorCz = p.doorFeetMicro.z / 9;
    bool sawDoorColumn = false;
    for (const auto& w : p.walls)
        if (w.cx == doorCx && w.cz == doorCz) {
            sawDoorColumn = true;
            if (w.fromMicroY > 0) lowestWallTop = std::min(lowestWallTop, w.fromMicroY);
        }
    ASSERT_TRUE(sawDoorColumn);
    EXPECT_GE(lowestWallTop, 16) << "the doorway is shorter than the agent (16 micro)";
}

// Treads must be climbable BY THE ENGINE'S RULE, not by eye: consecutive treads may rise at
// most maxStepUpMicro (4). Subcube plates rise 3; a cube stair would rise 9 and fail here.
TEST(TowerForgeTest, EveryTreadIsWithinTheAgentsStepUp) {
    const auto p = planTower(bbox(8), spec());
    ASSERT_TRUE(p.ok) << p.refusal;
    std::vector<int> treadTops;
    for (const auto& pl : p.plates)
        if (pl.tread) treadTops.push_back(pl.yMicro + pl.thicknessMicro);
    ASSERT_GE(treadTops.size(), 10u) << "too few treads to reach anywhere";
    std::sort(treadTops.begin(), treadTops.end());
    const AgentBox box;
    for (size_t i = 1; i < treadTops.size(); ++i)
        EXPECT_LE(treadTops[i] - treadTops[i - 1], box.maxStepUpMicro)
            << "tread " << i << " rises " << (treadTops[i] - treadTops[i - 1])
            << " micro — the agent can only step " << box.maxStepUpMicro;
}

// THE ACCEPTANCE TEST: walk in the door and climb to the top chamber.
TEST(TowerForgeTest, AnAgentWalksInTheDoorAndClimbsToTheTop) {
    const auto p = planTower(bbox(8), spec());
    ASSERT_TRUE(p.ok) << p.refusal;
    const TowerWorld world(p);
    TraversalProbe probe(world.occ(), AgentBox{});

    const glm::ivec3 lo{-9, 0, -9};
    const glm::ivec3 hi{8 * 9 + 9, spec().heightCubes * 9 + 9, 8 * 9 + 9};

    // Start on the doorway threshold, settled onto whatever holds it up.
    const int startY = probe.settle(p.doorFeetMicro.x, p.doorFeetMicro.y + 4,
                                    p.doorFeetMicro.z, 0);
    ASSERT_NE(startY, INT_MIN) << "the doorway has no floor to stand on";
    const glm::ivec3 start{p.doorFeetMicro.x, startY, p.doorFeetMicro.z};

    const glm::ivec3 goalLo = p.topFeetMicro - glm::ivec3(5, 3, 5);
    const glm::ivec3 goalHi = p.topFeetMicro + glm::ivec3(5, 6, 5);
    EXPECT_TRUE(probe.reachable(start, goalLo, goalHi, lo, hi))
        << "an agent cannot climb from the door to the top chamber — the tower does not "
           "function as a tower, whatever it looks like from outside";
}

// Diagnostic: where does the climb actually stop? flood() exists so a failed reach can be
// LOCATED instead of merely reported.
TEST(TowerForgeTest, DiagnoseTheClimb) {
    const auto p = planTower(bbox(8), spec());
    ASSERT_TRUE(p.ok) << p.refusal;
    int treads = 0, floors = 0, maxTread = 0, maxFloor = 0;
    for (const auto& pl : p.plates) {
        if (pl.tread) { ++treads; maxTread = std::max(maxTread, pl.yMicro); }
        else          { ++floors; maxFloor = std::max(maxFloor, pl.yMicro); }
    }
    std::cout << "  treads=" << treads << " (top y=" << maxTread << ")  floorPlates=" << floors
              << " (top y=" << maxFloor << ")  floors=" << p.floorCount << "\n"
              << "  door feet=(" << p.doorFeetMicro.x << "," << p.doorFeetMicro.y << ","
              << p.doorFeetMicro.z << ")  goal=(" << p.topFeetMicro.x << "," << p.topFeetMicro.y
              << "," << p.topFeetMicro.z << ")\n";

    std::cout << "  first treads:";
    int shown = 0;
    for (const auto& pl : p.plates)
        if (pl.tread && shown++ < 14)
            std::cout << " (" << pl.cx << "," << pl.cz << ")@" << pl.yMicro;
    std::cout << "\n  base floor plates:";
    shown = 0;
    for (const auto& pl : p.plates)
        if (!pl.tread && pl.yMicro == 0 && shown++ < 8)
            std::cout << " (" << pl.cx << "," << pl.cz << ")";
    std::cout << "\n";

    const TowerWorld world(p);
    TraversalProbe probe(world.occ(), AgentBox{});
    const glm::ivec3 lo{-9, 0, -9}, hi{8 * 9 + 9, spec().heightCubes * 9 + 9, 8 * 9 + 9};
    // Is the very first step-up possible at all? Probe the cell just inside the door.
    const int startY = probe.settle(p.doorFeetMicro.x, p.doorFeetMicro.y + 4, p.doorFeetMicro.z, 0);
    std::cout << "  settled start y=" << startY << "\n";
    if (startY == INT_MIN) return;
    // Is the entry path actually open? Sample the door column and the cell inside it.
    const auto& occ = world.occ();
    const int dcx = p.doorFeetMicro.x, dcz = p.doorFeetMicro.z;
    std::cout << "  door column solid at y=2,8,20,30: " << occ(dcx, 2, dcz) << occ(dcx, 8, dcz)
              << occ(dcx, 20, dcz) << occ(dcx, 30, dcz) << "\n";
    std::cout << "  one cube inward (+z) solid at y=2,8: " << occ(dcx, 2, dcz + 9)
              << occ(dcx, 8, dcz + 9) << "\n";
    std::cout << "  probe fits in doorway: " << probe.fits(dcx, 0, dcz)
              << "  fits inward: " << probe.fits(dcx, 0, dcz + 9) << "\n";

    const auto reach = probe.flood({p.doorFeetMicro.x, startY, p.doorFeetMicro.z}, lo, hi);
    int highest = -1;
    for (const auto& c : reach) highest = std::max(highest, c.y);
    std::cout << "  reachable cells=" << reach.size() << "  highest feet y=" << highest
              << "  (goal y=" << p.topFeetMicro.y << ")\n";
}

// A sensitivity control: seal the stair and the SAME probe must fail. Without this the
// reachability result above could be passing for the wrong reason.
TEST(TowerForgeTest, TheProofFailsWhenTheStairIsRemoved) {
    const auto p = planTower(bbox(8), spec());
    ASSERT_TRUE(p.ok) << p.refusal;

    TowerPlan stripped = p;
    stripped.plates.clear();
    for (const auto& pl : p.plates)
        if (!pl.tread) stripped.plates.push_back(pl);      // floors kept, stair gone

    const TowerWorld world(stripped);
    TraversalProbe probe(world.occ(), AgentBox{});
    const glm::ivec3 lo{-9, 0, -9};
    const glm::ivec3 hi{8 * 9 + 9, spec().heightCubes * 9 + 9, 8 * 9 + 9};
    const int startY = probe.settle(p.doorFeetMicro.x, p.doorFeetMicro.y + 4,
                                    p.doorFeetMicro.z, 0);
    ASSERT_NE(startY, INT_MIN);
    const glm::ivec3 start{p.doorFeetMicro.x, startY, p.doorFeetMicro.z};
    const glm::ivec3 goalLo = p.topFeetMicro - glm::ivec3(5, 3, 5);
    const glm::ivec3 goalHi = p.topFeetMicro + glm::ivec3(5, 6, 5);
    EXPECT_FALSE(probe.reachable(start, goalLo, goalHi, lo, hi))
        << "the agent reached the top WITHOUT a stair — the reachability test proves nothing";
}
