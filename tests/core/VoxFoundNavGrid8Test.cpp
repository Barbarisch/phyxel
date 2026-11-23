// docs/VoxelFoundationPlan.md F7.22 -- nav agent units on the 1/8 m grid (docs/evidence/voxfound_f7_prediction.txt N1-N3).
// A world of 1/8 m voxels (a function); the graph at perM 8 queries it directly, the RED graph samples it at 1/9 m the
// way occupiedMicro samples the switched store (the 1/9 cell's centre). The default NavAgentProfile (1.889 m tall,
// step-up 0.444 m).
#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>

#include "core/NavGraph.h"

using namespace Phyxel::Core;

namespace {

using Vox8 = std::function<bool(int, int, int)>;   // 1/8 m voxel solid?

int floorDiv(int a, int b) { return a >= 0 ? a / b : (a - b + 1) / b; }

std::unique_ptr<NavGraph> graph8(const Vox8& v) {
    auto fill = [v](const glm::ivec3& c) {
        int n = 0;
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
                for (int z = 0; z < 8; ++z) n += v(c.x * 8 + x, c.y * 8 + y, c.z * 8 + z);
        return n == 0 ? CellFill::Empty : n == 512 ? CellFill::Solid : CellFill::Partial;
    };
    auto g = std::make_unique<NavGraph>(CellFillFunc(fill),
                                        MicroQueryFunc([v](const glm::ivec3& m) { return v(m.x, m.y, m.z); }));
    g->setCellsPerM(8);
    return g;
}

// TODAY on the switched world: the 1/8 m content sampled at the 1/9 m cell's centre (WorldReads::occupiedMicro's rule)
std::unique_ptr<NavGraph> graph9(const Vox8& v) {
    auto s9 = [v](int x, int y, int z) {
        auto c = [](int m) { return int(std::floor((m + 0.5) * 8.0 / 9.0)); };
        return v(c(x), c(y), c(z));
    };
    auto fill = [s9](const glm::ivec3& c) {
        int n = 0;
        for (int x = 0; x < 9; ++x)
            for (int y = 0; y < 9; ++y)
                for (int z = 0; z < 9; ++z) n += s9(c.x * 9 + x, c.y * 9 + y, c.z * 9 + z);
        return n == 0 ? CellFill::Empty : n == 729 ? CellFill::Solid : CellFill::Partial;
    };
    return std::make_unique<NavGraph>(CellFillFunc(fill),
                                      MicroQueryFunc([s9](const glm::ivec3& m) { return s9(m.x, m.y, m.z); }));
}

// A corridor 6 m long (x 0..5), 3 m wide (z 0..2), floor below y = 0; a 1 m wall at x = 2 (voxels 16..23) up to 4 m
// with a 1 m wide door at z = 1 (voxels 8..15), `doorH` voxels tall.
Vox8 doorWorld(int doorH) {
    return [doorH](int x, int y, int z) {
        if (x < 0 || x >= 48 || z < 0 || z >= 24) return true;   // the corridor's walls
        if (y < 0) return true;                                    // the floor
        if (x >= 16 && x < 24 && y < 32) return !(z >= 8 && z < 16 && y < doorH);
        return false;
    };
}

// The same corridor, no wall; the floor steps up by `stepH` voxels at x = 3 m.
Vox8 stepWorld(int stepH) {
    return [stepH](int x, int y, int z) {
        if (x < 0 || x >= 48 || z < 0 || z >= 24) return true;
        if (y < 0) return true;
        if (x >= 24 && y < stepH) return true;
        return false;
    };
}

bool walks(NavGraph& g, float goalY) {
    NavAgentProfile agent;
    g.buildRegion({0, 0}, {5, 2}, agent);
    return g.findPath(glm::vec3(0.5f, 0.0f, 1.5f), glm::vec3(5.5f, goalY, 1.5f), agent).found;
}

}  // namespace

TEST(VoxFoundNavGrid8, N1_TwoMetreDoorPasses) {
    EXPECT_TRUE(walks(*graph8(doorWorld(16)), 0.0f));
    EXPECT_TRUE(walks(*graph9(doorWorld(16)), 0.0f)) << "control: today's sampler passes a 2.0 m door too";
}

TEST(VoxFoundNavGrid8, N2_DoorBelowTheAgentIsRefused) {
    EXPECT_FALSE(walks(*graph8(doorWorld(15)), 0.0f)) << "a 1.875 m door is below the agent's 1.889 m";
    EXPECT_TRUE(walks(*graph9(doorWorld(15)), 0.0f)) << "RED: the 1/9 sampler reads 17 clear cells (1.889 m)";
}

TEST(VoxFoundNavGrid8, N3_StepLimitInMetres) {
    EXPECT_TRUE(walks(*graph8(stepWorld(3)), 0.375f)) << "0.375 m <= the 0.444 m step-up";
    EXPECT_FALSE(walks(*graph8(stepWorld(4)), 0.5f)) << "0.5 m > the 0.444 m step-up";
    EXPECT_TRUE(walks(*graph9(stepWorld(4)), 0.5f)) << "RED: the 1/9 sampler reads the 0.5 m step as 0.444 m";
}

TEST(VoxFoundNavGrid8, AgentConversion) {
    NavGraph g(CellFillFunc([](const glm::ivec3&) { return CellFill::Empty; }),
               MicroQueryFunc([](const glm::ivec3&) { return false; }));
    NavAgentProfile a;
    EXPECT_EQ(g.scaledAgent(a).heightMicro, a.heightMicro) << "at 9: unchanged";
    g.setCellsPerM(8);
    const NavAgentProfile s = g.scaledAgent(a);
    EXPECT_EQ(s.heightMicro, 16);    // 17/9 m = 1.889 -> 16 cells (2.0 m): a need rounds up
    EXPECT_EQ(s.stepUpMicro, 3);     // 4/9 m = 0.444 -> 3 cells (0.375 m): a tolerance rounds down
    EXPECT_EQ(s.halfWidthMicro, 2);  // 2/9 m = 0.222 -> 2 cells (0.25 m)
}
