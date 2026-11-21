// WorldFluidTest -- docs/VoxelFoundationPlan.md F6.2 (prediction: docs/evidence/voxfound_f6_prediction.txt W1-W6):
// liquid and powder living in the world's VoxelGrid (grid.fill), stepped by WorldFluid in a box that grows around the
// fill -- equal, voxel for voxel, to ONE MatSim box over the whole grid (the box must not be visible).

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "core/matsim/MatSimCore.h"
#include "core/voxfound/MatSimWorld.h"
#include "core/voxfound/VoxelGrid.h"
#include "core/voxfound/WorldFluid.h"

using namespace Phyxel::Core::MatSim;
using Phyxel::Core::VoxFound::VoxelGrid;
using Phyxel::Core::VoxFound::WorldFluid;
using Phyxel::Core::VoxFound::matSimOccupancy;

namespace {

// The reference: one box over the whole grid, from the grid's fill.
MatSimCore wholeGrid(const VoxelGrid& g, Config cfg = {}) {
    cfg.lattice = 8;
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create({0, 0, 0}, {g.dim.x, g.dim.y, g.dim.z}, 8, matSimOccupancy(g), &why, cfg)) << why;
    for (size_t i = 0; i < g.fill.size(); ++i)
        if (g.fill[i]) {
            const glm::ivec3 v = g.pos(i);
            s.setCell({v.x, v.y, v.z}, cellMaterial(g.fill[i]), cellFill(g.fill[i]));
        }
    return s;
}

// "" when grid.fill equals the reference everywhere, else the first difference.
std::string compare(const VoxelGrid& g, const MatSimCore& ref) {
    for (size_t i = 0; i < g.size(); ++i) {
        const glm::ivec3 v = g.pos(i);
        const Cell c = ref.at({v.x, v.y, v.z});
        const Cell want = (cellMaterial(c) == kSolid || cellFill(c) == 0) ? 0u : c;
        const Cell got = g.fill.empty() ? 0u : g.fill[i];
        if (got != want) {
            char b[160];
            std::snprintf(b, sizeof(b), "voxel (%d,%d,%d): world mat %u fill %u, one box mat %u fill %u", v.x, v.y, v.z,
                          cellMaterial(got), cellFill(got), cellMaterial(want), cellFill(want));
            return b;
        }
    }
    return "";
}

void fillBox(VoxelGrid& g, uint8_t m, glm::ivec3 lo, glm::ivec3 size, uint32_t units = 256) {
    if (g.fill.empty()) g.fill.assign(g.size(), 0u);
    for (int x = lo.x; x < lo.x + size.x; ++x)
        for (int y = lo.y; y < lo.y + size.y; ++y)
            for (int z = lo.z; z < lo.z + size.z; ++z)
                if (g.mat[g.idx({x, y, z})] == 0) g.fill[g.idx({x, y, z})] = makeCell(m, units);
}

// 8 x 3 x 3 m: water 1 m wide, 2 m deep against the -x end; the rest open (the box starts at x < 3 m and must grow)
VoxelGrid damWorld() {
    VoxelGrid g;
    g.resize({64, 24, 24});
    fillBox(g, Mat::Water, {0, 0, 0}, {8, 16, 24});
    return g;
}

// 6 x 3 x 6 m: a wall (x 24-25, y < 12), a pool, a sand column, a ledge
VoxelGrid mixedWorld() {
    VoxelGrid g;
    g.resize({48, 24, 48});
    for (int y = 0; y < 12; ++y)
        for (int z = 0; z < 48; ++z)
            for (int x = 24; x < 26; ++x) g.set({x, y, z}, 1);
    for (int x = 30; x < 40; ++x)
        for (int z = 30; z < 40; ++z) g.set({x, 8, z}, 1);   // a ledge 1 m up
    fillBox(g, Mat::Water, {0, 0, 0}, {10, 10, 12});
    fillBox(g, Mat::Sand, {14, 0, 30}, {4, 16, 4});
    fillBox(g, Mat::Water, {32, 9, 32}, {4, 4, 4}, 128);
    return g;
}

Ledger gridLedger(const VoxelGrid& g) {
    Ledger l{};
    for (Cell c : g.fill) if (c) l[cellMaterial(c)] += cellFill(c);
    return l;
}

// Steps the world and the reference side by side for `ticks`, comparing every `every`; the ledger after every call.
std::string runBoth(VoxelGrid& g, WorldFluid& wf, MatSimCore& ref, int ticks, int every, const Ledger& l0) {
    for (int t = 0; t < ticks; t += every) {
        wf.step(every);
        ref.step(every);
        if (wf.ledger() != l0) return "W1: the world ledger changed by tick " + std::to_string(wf.tick());
        const std::string d = compare(g, ref);
        if (!d.empty()) return "tick " + std::to_string(wf.tick()) + ": " + d;
    }
    return "";
}

}  // namespace

// W1-W3: the dam break -- the ledger exact, equal to one box over the grid, and the box grew
TEST(WorldFluid, DamBreakEqualsOneBoxAndGrows) {
    VoxelGrid g = damWorld();
    MatSimCore ref = wholeGrid(g);
    const Ledger l0 = gridLedger(g);
    WorldFluid wf;
    std::string err;
    ASSERT_TRUE(wf.attach(&g, &err)) << err;
    const std::string d = runBoth(g, wf, ref, 2000, 100, l0);
    std::printf("[WorldFluid] dam break: 2000 ticks, the box grew %d times (now %d x %d x %d at (%d,%d,%d)); %s\n",
                wf.grows(), wf.boxSize().x, wf.boxSize().y, wf.boxSize().z, wf.boxLo().x, wf.boxLo().y, wf.boxLo().z,
                d.empty() ? "equal to one box over the grid every 100 ticks" : d.c_str());
    EXPECT_EQ(d, "");
    EXPECT_GE(wf.grows(), 2) << "W3: the box never had to grow, so W2 proved nothing";
    // the comparator's red: one tick more on the reference differs
    ref.step(1);
    EXPECT_NE(compare(g, ref), "") << "the comparator missed a one-tick shift";
}

// W2 on a second scene: sand, water, a wall, a ledge
TEST(WorldFluid, MixedSceneEqualsOneBox) {
    VoxelGrid g = mixedWorld();
    MatSimCore ref = wholeGrid(g);
    const Ledger l0 = gridLedger(g);
    WorldFluid wf;
    std::string err;
    ASSERT_TRUE(wf.attach(&g, &err)) << err;
    const std::string d = runBoth(g, wf, ref, 1500, 100, l0);
    std::printf("[WorldFluid] mixed scene: 1500 ticks, %d grows; %s\n", wf.grows(), d.empty() ? "equal" : d.c_str());
    EXPECT_EQ(d, "");
}

// W4: a wall broken mid-run lets the water through, the same as the one-box run given the same edit
TEST(WorldFluid, WallBrokenMidRunEqualsOneBox) {
    VoxelGrid g;
    g.resize({64, 24, 24});
    for (int x = 20; x < 22; ++x)
        for (int y = 0; y < 24; ++y)
            for (int z = 0; z < 24; ++z) g.set({x, y, z}, 1);
    fillBox(g, Mat::Water, {0, 0, 0}, {20, 16, 24});
    MatSimCore ref = wholeGrid(g);
    const Ledger l0 = gridLedger(g);
    WorldFluid wf;
    std::string err;
    ASSERT_TRUE(wf.attach(&g, &err)) << err;
    ASSERT_EQ(runBoth(g, wf, ref, 300, 100, l0), "");
    uint64_t beyond0 = 0;
    for (size_t i = 0; i < g.size(); ++i) beyond0 += g.pos(i).x >= 22 && g.fill[i];
    for (int x = 20; x < 22; ++x)   // break the wall below 1 m
        for (int y = 0; y < 8; ++y)
            for (int z = 0; z < 24; ++z) g.set({x, y, z}, 0);
    wf.wallsChanged();
    ref.resolidify(matSimOccupancy(g));
    const std::string d = runBoth(g, wf, ref, 1200, 100, l0);
    uint64_t beyond = 0;
    for (size_t i = 0; i < g.size(); ++i) beyond += g.pos(i).x >= 22 && g.fill[i];
    std::printf("[WorldFluid] wall broken at tick 300: wet voxels past it %llu -> %llu, %d grows; %s\n",
                (unsigned long long)beyond0, (unsigned long long)beyond, wf.grows(), d.empty() ? "equal" : d.c_str());
    EXPECT_EQ(d, "");
    EXPECT_EQ(beyond0, 0u);
    EXPECT_GT(beyond, 0u) << "W4: the water did not go through the broken wall";
}

// W5: a settled world costs nothing
TEST(WorldFluid, SettledWorldEvaluatesNothing) {
    VoxelGrid g;
    g.resize({32, 16, 32});
    fillBox(g, Mat::Water, {0, 0, 0}, {32, 4, 32});   // a flat pool, wall to wall
    WorldFluid wf;
    std::string err;
    ASSERT_TRUE(wf.attach(&g, &err)) << err;
    int quietAt = -1;
    bool prev = false;
    for (int t = 1; t <= 600 && quietAt < 0; ++t) {
        const bool q = wf.step(1).lastTickQuiet();
        if (q && prev) quietAt = t;
        prev = q;
    }
    ASSERT_GE(quietAt, 0) << "the pool never settled";
    const StepStats s = wf.step(100);
    std::printf("[WorldFluid] pool quiet at tick %d; the next 100 ticks evaluated %llu blocks\n", quietAt,
                (unsigned long long)s.evaluatedBlocks);
    EXPECT_EQ(s.evaluatedBlocks, 0u);
}

// W6: bad fill refused at attach (nothing attached); bad edits refused
TEST(WorldFluid, RefusesBadFill) {
    VoxelGrid g;
    g.resize({8, 8, 8});
    g.set({1, 1, 1}, 1);
    g.fill.assign(g.size(), 0u);
    WorldFluid wf;
    std::string err;
    g.fill[g.idx({1, 1, 1})] = makeCell(Mat::Water, 10);
    EXPECT_FALSE(wf.attach(&g, &err)) << "fill in a solid voxel";
    g.fill[g.idx({1, 1, 1})] = 0;
    g.fill[0] = makeCell(Mat::Water, 257);
    EXPECT_FALSE(wf.attach(&g, &err)) << "units over FULL";
    g.fill[0] = makeCell(kSolid, 10);
    EXPECT_FALSE(wf.attach(&g, &err)) << "a material that is not liquid or powder";
    g.fill[0] = 0;
    g.fill.resize(3);
    EXPECT_FALSE(wf.attach(&g, &err)) << "the wrong size";
    EXPECT_FALSE(wf.attached());
    g.fill.assign(g.size(), 0u);
    ASSERT_TRUE(wf.attach(&g, &err)) << err;
    EXPECT_FALSE(wf.setFill({1, 1, 1}, Mat::Water, 10, &err)) << "fill into a solid voxel";
    EXPECT_FALSE(wf.setFill({0, 0, 0}, Mat::Water, 300, &err)) << "over FULL";
    EXPECT_FALSE(wf.setFill({9, 0, 0}, Mat::Water, 10, &err)) << "outside the grid";
    EXPECT_TRUE(wf.setFill({0, 7, 0}, Mat::Sand, 256, &err)) << err;
    wf.step(50);
    EXPECT_EQ(wf.ledger()[Mat::Sand], 256u);
}

// F6.2 diagnostic (not a gate): the dam break with the pressure rule off on both sides -- isolates whether a re-made
// box differs through the head field (state carried across ticks) or elsewhere.
TEST(WorldFluidDiag, DamBreakPressureOff) {
    VoxelGrid g = damWorld();
    Config off;
    off.pressure = false;
    MatSimCore ref = wholeGrid(g, off);
    const Ledger l0 = gridLedger(g);
    WorldFluid wf;
    WorldFluid::Params p;
    p.cfg = off;
    std::string err;
    ASSERT_TRUE(wf.attach(&g, &err, p)) << err;
    const std::string d = runBoth(g, wf, ref, 2000, 100, l0);
    std::printf("[WorldFluidDiag] pressure off: %d grows; %s\n", wf.grows(), d.empty() ? "equal" : d.c_str());
    EXPECT_EQ(d, "");
}
