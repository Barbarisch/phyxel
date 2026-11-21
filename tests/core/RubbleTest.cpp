// RubbleTest -- docs/VoxelFoundationPlan.md F6.3 (prediction: docs/evidence/voxfound_f6_prediction.txt P1-P5): crumbs
// are powder. A hand-built wall (FractureLab's rig style) is broken by F4.1's fracture, F5.1 extracts bodies and
// crumbs, and the crumbs become rubble in the world (one full cell each), stepped by WorldFluid until they rest.

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/matsim/MatSimCore.h"
#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"
#include "core/voxfound/VoxelGrid.h"
#include "core/voxfound/WorldFluid.h"

using namespace Phyxel::Core::VoxFound;
namespace MS = Phyxel::Core::MatSim;

namespace {

constexpr uint8_t kFloor = 6;   // a copy of Stone, fixed (the lab's floor material)

struct Scene {
    VoxelGrid world;
    std::vector<FractureMaterial> mats;
    Extraction ex;
    std::map<uint8_t, uint64_t> before;   // wall + floor voxels per material, before the break
};

// A 64 x 48 x 4-voxel (8 x 6 x 0.5 m) wall of `mat` on a 2-voxel fixed floor, 16 voxels of pad in z on each side and
// 8 above; hit at the centre, E/T `e`.
Scene brokenWall(uint8_t mat, float e, uint64_t seed) {
    Scene s;
    s.mats = labMaterials();
    s.mats.resize(kFloor + 1, s.mats[kStone]);
    s.mats[kFloor] = s.mats[kStone];
    VoxelGrid& g = s.world;
    g.voxelM = 0.125f;
    g.resize({64, 2 + 48 + 8, 4 + 32});
    for (int x = 0; x < 64; ++x)
        for (int z = 0; z < g.dim.z; ++z)
            for (int y = 0; y < 2; ++y) g.set({x, y, z}, kFloor, 0, true);
    for (int x = 0; x < 64; ++x)
        for (int y = 2; y < 50; ++y)
            for (int z = 16; z < 20; ++z) g.set({x, y, z}, mat, 0, false);
    for (size_t i = 0; i < g.size(); ++i)
        if (g.mat[i]) ++s.before[g.mat[i]];
    Hit hit{{32.0f, 26.0f, 16.0f}, {0, 0, 1}, e * s.mats[mat].toughness, seed, 200.0f};
    const FractureResult r = fractureReference(g, hit, s.mats);
    s.ex = extractBodies(g, r, hit, s.mats);
    return s;
}

// Per world material: solid voxels + body voxels + rubble units / 256 (exact: the units must be whole cells' worth
// only in total, so the check is on units = 256 x voxels)
std::map<uint8_t, uint64_t> unitsNow(const Scene& s, const WorldFluid& wf) {
    std::map<uint8_t, uint64_t> u;
    for (uint8_t m : s.world.mat)
        if (m) u[m] += 256;
    for (const Body& b : s.ex.bodies)
        for (uint8_t m : b.grid.mat)
            if (m) u[m] += 256;
    const MS::Ledger l = wf.ledger();
    for (int id = 0; id < 256; ++id)
        if (l[id] && MS::worldMaterialOfRubble(uint8_t(id))) u[MS::worldMaterialOfRubble(uint8_t(id))] += l[id];
    return u;
}

std::map<uint8_t, uint64_t> unitsBefore(const Scene& s) {
    std::map<uint8_t, uint64_t> u;
    for (const auto& [m, n] : s.before) u[m] = n * 256;
    return u;
}

double rubbleMeanHeight(const VoxelGrid& g) {
    double sum = 0.0, w = 0.0;
    for (size_t i = 0; i < g.fill.size(); ++i)
        if (g.fill[i] && MS::worldMaterialOfRubble(MS::cellMaterial(g.fill[i]))) {
            sum += g.pos(i).y * double(MS::cellFill(g.fill[i]));
            w += MS::cellFill(g.fill[i]);
        }
    return w > 0 ? sum / w : 0.0;
}

}  // namespace

// P1-P3: conservation exact per world material, the rubble rests, and it fell
TEST(Rubble, CrumbsBecomePilesConservingEveryMaterial) {
    struct Case { uint8_t mat; float e; uint64_t seed; const char* name; };
    const Case cases[] = {{kStone, 650.0f, 777, "stone E/T 650"}, {kStone, 2800.0f, 778, "stone E/T 2800"},
                          {kBricks, 650.0f, 779, "brick E/T 650"}, {kBricks, 2800.0f, 780, "brick E/T 2800"}};
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        Scene s = brokenWall(c.mat, c.e, c.seed);
        ASSERT_GT(s.ex.crumbs.size(), 0u) << "the hit made no crumbs: this case proves nothing";
        WorldFluid wf;
        std::string err;
        ASSERT_TRUE(wf.attach(&s.world, &err)) << err;
        ASSERT_TRUE(crumbsToPowder(s.ex.crumbs, wf, &err)) << err;
        const auto want = unitsBefore(s);
        ASSERT_EQ(unitsNow(s, wf), want) << "P1: right after the crumbs became rubble";
        const double h0 = rubbleMeanHeight(s.world);
        int quietAt = -1;
        bool prev = false;
        for (int t = 1; t <= 1200; ++t) {
            const bool q = wf.step(1).lastTickQuiet();
            if (t % 50 == 0) ASSERT_EQ(unitsNow(s, wf), want) << "P1 at tick " << t;
            if (q && prev && quietAt < 0) quietAt = t;
            prev = q;
        }
        ASSERT_EQ(unitsNow(s, wf), want) << "P1 at the end";
        const double h1 = rubbleMeanHeight(s.world);
        // P3: no rubble cell over an empty open voxel
        size_t hanging = 0;
        for (size_t i = 0; i < s.world.size(); ++i) {
            if (!s.world.fill[i]) continue;
            const glm::ivec3 v = s.world.pos(i);
            if (v.y == 0) continue;
            const size_t b = s.world.idx(v - glm::ivec3(0, 1, 0));
            if (s.world.mat[b] == 0 && s.world.fill[b] == 0) ++hanging;
        }
        size_t rubbleCells = 0;
        for (uint32_t f : s.world.fill) rubbleCells += f != 0;
        std::printf("[Rubble] %s: %zu bodies, %zu crumbs -> %zu rubble cells; quiet at tick %d; mean height %.2f -> %.2f "
                    "voxels; %zu cells over air; box grew %d times\n", c.name, s.ex.bodies.size(), s.ex.crumbs.size(),
                    rubbleCells, quietAt, h0, h1, hanging, wf.grows());
        EXPECT_GE(quietAt, 0) << "P2: the rubble did not come to rest in 1,200 ticks";
        EXPECT_LT(h1, h0) << "P3: the rubble did not fall";
        EXPECT_EQ(hanging, 0u) << "P3: rubble resting over air";
    }
}

// P4: identity, P5: refusals
TEST(Rubble, IdentityAndRefusals) {
    for (int m = 1; m <= 63; ++m) {
        const uint8_t r = MS::rubbleOf(uint8_t(m));
        ASSERT_NE(r, 0u);
        EXPECT_EQ(MS::worldMaterialOfRubble(r), m);
        EXPECT_EQ(MS::materialTable()[r].phase, MS::Phase::Powder);
    }
    EXPECT_EQ(MS::rubbleOf(0), 0u);
    EXPECT_EQ(MS::rubbleOf(64), 0u);
    EXPECT_EQ(MS::worldMaterialOfRubble(MS::Mat::Sand), 0u);
    VoxelGrid g;
    g.resize({8, 8, 8});
    g.set({1, 1, 1}, kStone);
    WorldFluid wf;
    std::string err;
    ASSERT_TRUE(wf.attach(&g, &err)) << err;
    EXPECT_FALSE(crumbsToPowder({Crumb{{1, 1, 1}, kStone, 0, 0}}, wf, &err)) << "a crumb over a solid voxel";
    EXPECT_FALSE(crumbsToPowder({Crumb{{2, 2, 2}, 0, 0, 0}}, wf, &err)) << "material 0";
    EXPECT_FALSE(crumbsToPowder({Crumb{{2, 2, 2}, 70, 0, 0}}, wf, &err)) << "material 70";
    EXPECT_FALSE(crumbsToPowder({Crumb{{2, 2, 2}, kStone, 0, 0}, Crumb{{1, 1, 1}, kStone, 0, 0}}, wf, &err))
        << "all or nothing";
    for (uint32_t f : g.fill) EXPECT_EQ(f, 0u) << "a refused batch wrote something";
    EXPECT_TRUE(crumbsToPowder({Crumb{{2, 2, 2}, kStone, 0, 0}}, wf, &err)) << err;
    EXPECT_FALSE(crumbsToPowder({Crumb{{2, 2, 2}, kStone, 0, 0}}, wf, &err)) << "a voxel that already holds fill";
}
