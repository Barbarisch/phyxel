// DebrisContactTest — the debris point-contact model on the shared micro occupancy
// (DebrisInteractionPlan 1c step 3). CPU mirror: core/DebrisContact.h; GPU: voxel_contact.glsl.
//
// What changes for debris: it used to collide against a CUBE bitfield, blind to sub-voxel
// geometry (a 1/3 slab, a 2-micro fence) and to whether a chunk was even loaded. These tests pin
// the new contract on a real packed pool built from VoxelOccupancyGrid:
//   * full-cube worlds collide EXACTLY as the old cube search did (the regression invariant);
//   * debris rests on sub-voxel surfaces at their true height, and on thin walls;
//   * a real gap is open air; deep inside sub-voxel solid escapes through the nearest real face;
//   * anything next to unknown occupancy is reported UNKNOWN (the solver holds the body).

#include <gtest/gtest.h>

#include <random>

#include "core/DebrisContact.h"
#include "graphics/VoxelLightOccupancy.h"
#include "physics/VoxelOccupancyGrid.h"

using namespace Phyxel;
using DebrisContact::CubeState;
using Graphics::PackedOccupancyPool;
using Physics::VoxelOccupancyGrid;

namespace {

const glm::ivec3 kBox = PackedOccupancyPool::boxMinChunkFor(glm::vec3(0.0f));

void solidCube(VoxelOccupancyGrid& g, const glm::ivec3& lp) { g.setCube(lp, true); }

/// Mark micro cells of one cube from a predicate over in-cube micro coords (0..8 per axis).
template <class Pred>
void microCells(VoxelOccupancyGrid& g, const glm::ivec3& lp, Pred pred) {
    for (int mx = 0; mx < 9; ++mx)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 9; ++mz) {
        if (!pred(mx, my, mz)) continue;
        const glm::ivec3 sp(mx / 3, my / 3, mz / 3), mp(mx % 3, my % 3, mz % 3);
        g.setCube(lp, true);
        g.markSubdivided(lp, true);
        g.setSubcube(lp, sp, true);
        g.markSubcubeSubdivided(lp, sp, true);
        g.setMicrocube(lp, sp, mp, true);
    }
}

struct Pool {
    PackedOccupancyPool packed;
    CubeState cube(const glm::ivec3& wc) const {
        if (Graphics::packedPoolOccupancyState(packed, wc * 9) == Graphics::OccupancyState::Unknown)
            return CubeState::Unknown;
        switch (Graphics::packedPoolCubeOccupancy(packed, wc)) {
            case Graphics::CubeOccupancy::Solid: return CubeState::Solid;
            case Graphics::CubeOccupancy::Mixed: return CubeState::Mixed;
            default:                             return CubeState::Empty;
        }
    }
    bool micro(const glm::ivec3& wm) const { return Graphics::packedPoolSolidAt(packed, wm); }
    DebrisContact::PointContact at(const glm::vec3& x, float margin = 0.02f) const {
        return DebrisContact::pointContact(
            x, margin, [&](const glm::ivec3& c) { return cube(c); },
            [&](const glm::ivec3& m) { return micro(m); });
    }
};

Pool makePool(const std::vector<std::pair<glm::ivec3, const VoxelOccupancyGrid*>>& chunks) {
    std::vector<std::pair<glm::ivec3, Graphics::ChunkLightOccupancy>> blobs;
    for (const auto& [origin, g] : chunks) blobs.push_back({origin, Graphics::buildLightOccupancy(*g)});
    return Pool{Graphics::packOccupancyPool(blobs, kBox)};
}

/// The lab world: a full-cube floor (x,z 0..7, y 0..3, top at y=4) plus sub-voxel features.
VoxelOccupancyGrid labGrid() {
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int x = 0; x < 8; ++x) for (int z = 0; z < 8; ++z) for (int y = 0; y < 4; ++y)
        solidCube(g, {x, y, z});
    microCells(g, {3, 4, 3}, [](int, int my, int) { return my < 3; });   // 1/3 slab, top 4 + 3/9
    microCells(g, {6, 4, 6}, [](int mx, int, int) { return mx < 2; });   // 2-micro fence on x-low side
    microCells(g, {5, 6, 1}, [](int, int my, int) { return my < 8; });   // floating 8/9-thick block
    // A floor cube that is solid except one corner micro: MIXED, but its centre is > 4 micro from
    // any opening, so only the cube fallback can get a sample out of it.
    microCells(g, {6, 3, 1}, [](int mx, int my, int mz) { return !(mx == 0 && my == 0 && mz == 0); });
    solidCube(g, {2, 5, 6});                                             // gap of one cube between
    solidCube(g, {4, 5, 6});                                             //   these two
    return g;
}

}  // namespace

TEST(DebrisContact, FullCubeFloorRestsAtItsTopFace) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});

    auto above = p.at({1.5f, 4.01f, 1.5f});
    ASSERT_TRUE(above.hit);
    EXPECT_FALSE(above.micro);
    EXPECT_NEAR(above.pen, -0.01f, 1e-4f);
    EXPECT_NEAR(above.n.y, 1.0f, 1e-5f);

    auto sunk = p.at({1.5f, 3.99f, 1.5f});
    ASSERT_TRUE(sunk.hit);
    EXPECT_NEAR(sunk.pen, 0.01f, 1e-4f);
    EXPECT_NEAR(sunk.n.y, 1.0f, 1e-5f);

    EXPECT_FALSE(p.at({1.5f, 4.5f, 1.5f}).hit) << "half a cube above the floor is beyond the margin";
}

TEST(DebrisContact, RestsOnAOneThirdSlabAtItsTrueHeightNotOnTheCube) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});
    const float top = 4.0f + 3.0f / 9.0f;

    auto rest = p.at({3.5f, top + 0.01f, 3.5f});
    ASSERT_TRUE(rest.hit) << "the slab is real geometry";
    EXPECT_TRUE(rest.micro);
    EXPECT_NEAR(rest.pen, -0.01f, 1e-4f);
    EXPECT_NEAR(rest.n.y, 1.0f, 1e-5f);

    auto sunk = p.at({3.5f, 4.2f, 3.5f});
    ASSERT_TRUE(sunk.hit);
    EXPECT_NEAR(sunk.pen, top - 4.2f, 1e-4f);
    EXPECT_NEAR(sunk.n.y, 1.0f, 1e-5f);

    EXPECT_FALSE(p.at({3.5f, 4.8f, 3.5f}).hit) << "above the slab, inside the same cube, is air";
}

TEST(DebrisContact, ATwoMicroFenceIsAWallDebrisCanRestAgainst) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});
    const float face = 6.0f + 2.0f / 9.0f;

    auto touch = p.at({face + 0.005f, 4.5f, 6.5f});
    ASSERT_TRUE(touch.hit);
    EXPECT_TRUE(touch.micro);
    EXPECT_NEAR(touch.pen, -0.005f, 1e-4f);
    EXPECT_NEAR(touch.n.x, 1.0f, 1e-5f);
    EXPECT_FALSE(p.at({6.6f, 4.5f, 6.5f}).hit) << "past the fence is open air";
}

TEST(DebrisContact, ARealGapIsOpenAir) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});
    EXPECT_FALSE(p.at({3.5f, 5.5f, 6.5f}).hit);
    auto wall = p.at({3.01f, 5.5f, 6.5f});
    ASSERT_TRUE(wall.hit);
    EXPECT_NEAR(wall.n.x, 1.0f, 1e-5f);
    EXPECT_NEAR(wall.pen, -0.01f, 1e-4f);
}

TEST(DebrisContact, InsideSubVoxelSolidEscapesThroughTheNearestRealFace) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});
    // Shallow (0.8 micro below the top face of the 8/9 block): resolved at micro level, up.
    auto shallow = p.at({5.5f, 6.0f + 7.2f / 9.0f, 1.5f});
    ASSERT_TRUE(shallow.hit);
    EXPECT_TRUE(shallow.micro);
    EXPECT_NEAR(shallow.n.y, 1.0f, 1e-5f);
    EXPECT_NEAR(shallow.pen, 0.8f / 9.0f, 1e-4f);
    // Deeper (2.7 micro in, 0.3 above the block's open underside): the multi-step micro walk
    // finds the underside, so it escapes DOWN at its true distance, not sideways out of the cube.
    auto deep = p.at({5.5f, 6.3f, 1.5f});
    ASSERT_TRUE(deep.hit);
    EXPECT_TRUE(deep.micro);
    EXPECT_NEAR(deep.n.y, -1.0f, 1e-5f);
    EXPECT_NEAR(deep.pen, 0.3f, 1e-4f);
    // A body sunk 0.13 into the 1/3 slab (1.8 micro deep) goes back UP, never sideways.
    auto slab = p.at({3.5f, 4.2f, 3.5f});
    ASSERT_TRUE(slab.hit);
    EXPECT_TRUE(slab.micro);
    EXPECT_NEAR(slab.n.y, 1.0f, 1e-5f);
}

TEST(DebrisContact, BeyondTheMicroWalkTheCubeFallbackStillFindsAWayOut) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});
    // Inside the mostly-solid floor cube (6,3,1), at in-cube micro (5,4,5): no 26-direction walk of
    // <= 4 cells reaches an empty micro (the one empty corner, (0,0,0), lies off every ray), so the
    // micro walk sees only solid and the cube fallback (mixed counted solid) escapes UP to the open
    // cube above, 0.5 away.
    auto c = p.at({6.0f + 5.5f / 9.0f, 3.5f, 1.0f + 5.5f / 9.0f});
    ASSERT_TRUE(c.hit);
    EXPECT_FALSE(c.micro);
    EXPECT_NEAR(c.n.y, 1.0f, 1e-5f);
    EXPECT_NEAR(c.pen, 0.5f, 1e-4f);
}

TEST(DebrisContact, NextToUnknownOccupancyIsUnknownNeverOpenAir) {
    const auto g = labGrid();
    const Pool p = makePool({{{0, 0, 0}, &g}});
    // Cube (31,4,5)'s neighbourhood reaches x = 32: chunk (32,0,0) is in the box but not resident.
    auto edge = p.at({31.5f, 4.5f, 5.5f});
    EXPECT_TRUE(edge.unknown);
    EXPECT_FALSE(edge.hit);
    EXPECT_TRUE(p.at({99999.0f, 4.5f, 5.5f}).unknown) << "outside the box";
    EXPECT_TRUE(Pool{}.at({1.5f, 4.01f, 1.5f}).unknown) << "a pool that was never packed";
}

TEST(DebrisContact, NegativeChunkOriginsResolve) {
    VoxelOccupancyGrid g;
    g.setChunkOrigin({-32, 0, 0});
    for (int x = 28; x < 32; ++x) for (int z = 0; z < 12; ++z) for (int y = 0; y < 4; ++y)
        solidCube(g, {x, y, z});                                          // world x -4..-1
    const Pool p = makePool({{{-32, 0, 0}, &g}});
    auto c = p.at({-2.5f, 3.99f, 5.5f});
    ASSERT_FALSE(c.unknown);
    ASSERT_TRUE(c.hit);
    EXPECT_NEAR(c.pen, 0.01f, 1e-4f);
    EXPECT_NEAR(c.n.y, 1.0f, 1e-5f);
}

// THE regression invariant: on a world of full cubes the new model is bit-for-bit the old
// cube-level search, at both margins the solver uses (0.02 contacts, 0 hard-contact).
TEST(DebrisContact, FullCubeWorldsCollideExactlyLikeTheOldCubeSearch) {
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> coin(0, 2);
    for (int x = 1; x < 15; ++x) for (int y = 1; y < 15; ++y) for (int z = 1; z < 15; ++z)
        if (coin(rng) == 0) solidCube(g, {x, y, z});
    const Pool p = makePool({{{0, 0, 0}, &g}});
    auto cubeSolid = [&](const glm::ivec3& c) { return g.isCubeFilled(c); };

    std::uniform_real_distribution<float> pos(2.0f, 14.0f);
    int compared = 0;
    for (int i = 0; i < 20000; ++i) {
        const glm::vec3 x(pos(rng), pos(rng), pos(rng));
        for (float margin : {0.02f, 0.0f}) {
            const auto now = p.at(x, margin);
            const auto old = DebrisContact::escapeSearch(x, margin, cubeSolid);
            ASSERT_FALSE(now.unknown);
            ASSERT_FALSE(now.micro);
            ASSERT_EQ(now.hit, old.hit) << "at (" << x.x << "," << x.y << "," << x.z << ")";
            if (!old.hit) continue;
            ASSERT_EQ(now.dirIdx, old.dirIdx);
            ASSERT_EQ(now.pen, old.pen);
            ASSERT_EQ(now.n, old.n);
            ++compared;
        }
    }
    EXPECT_GT(compared, 1000) << "the sample must actually exercise contacts";
}

// CONTROL: the old model (a cube bitfield in which a cube with ANY content is solid — what
// rebuildOccupancyFromChunks wrote) on the same points. It must get the sub-voxel cases wrong;
// if it did not, the tests above would not be measuring the change.
TEST(DebrisContact, ControlTheOldCubeBitfieldGetsSubVoxelGeometryWrong) {
    const auto g = labGrid();
    auto oldSolid = [&](const glm::ivec3& c) { return g.isCubeFilled(c); };
    // Resting just above the 1/3 slab: the old model thinks the point is 0.66 deep in solid.
    const auto slab = DebrisContact::escapeSearch(glm::vec3(3.5f, 4.0f + 3.0f / 9.0f + 0.01f, 3.5f),
                                                  0.02f, oldSolid);
    ASSERT_TRUE(slab.hit);
    EXPECT_GE(slab.pen, 0.5f) << "old model: deep penetration where the body is actually at rest";
    EXPECT_LT(slab.n.y, 0.5f) << "old model: and the way out is SIDEWAYS - a kick, not a rest";
    // Just outside the 2-micro fence: the old model has the body inside a solid cube.
    const auto fence = DebrisContact::escapeSearch(glm::vec3(6.0f + 2.0f / 9.0f + 0.005f, 4.5f, 6.5f),
                                                   0.02f, oldSolid);
    ASSERT_TRUE(fence.hit);
    EXPECT_GT(fence.pen, 0.2f);
}
