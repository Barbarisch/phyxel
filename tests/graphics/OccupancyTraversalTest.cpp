// OccupancyTraversalTest.cpp — the two-level segment test (cube cells first, micro only inside
// mixed cubes) must answer EXACTLY what the micro march answers, on every kind of geometry the
// engine produces. It exists because the probe field's per-fragment leak guard (G-141) needed a
// segment test that costs a tenth of the micro march without giving up sub-voxel truth: a 1-micro
// wall must block a segment exactly as a full cube does (LightingPipeline.md rule R8).
//
// No Vulkan: packedPoolSegmentBlocked is the CPU mirror of phxSegmentBlocked, line for line, and
// packedPoolSegmentHitsSolid is the micro march the shaders have used since M2.
#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "graphics/VoxelLightOccupancy.h"
#include "graphics/VoxelLightOccupancyGpu.h"
#include "physics/VoxelOccupancyGrid.h"

using Phyxel::Graphics::PackedOccupancyPool;
using Phyxel::Graphics::buildLightOccupancy;
using Phyxel::Graphics::packOccupancyPool;
using Phyxel::Graphics::packedPoolSegmentBlocked;
using Phyxel::Graphics::packedPoolSegmentHitsSolid;
using Phyxel::Graphics::packedPoolSolidAt;
using Phyxel::Physics::VoxelOccupancyGrid;

namespace {

void addSolidCube(VoxelOccupancyGrid& g, const glm::ivec3& lp) { g.setCube(lp, true); }
void addSubcube(VoxelOccupancyGrid& g, const glm::ivec3& lp, const glm::ivec3& sp) {
    g.setCube(lp, true); g.markSubdivided(lp, true); g.setSubcube(lp, sp, true);
}
void addMicrocube(VoxelOccupancyGrid& g, const glm::ivec3& lp, const glm::ivec3& sp, const glm::ivec3& mp) {
    g.setCube(lp, true); g.markSubdivided(lp, true); g.setSubcube(lp, sp, true);
    g.markSubcubeSubdivided(lp, sp, true); g.setMicrocube(lp, sp, mp, true);
}

/// A world with every kind of matter, across a NEGATIVE chunk origin: a solid slab, a solid wall,
/// a 1-micro-thick wall (the R8 case), a 3-micro floor, a subcube post, scattered microcubes.
PackedOccupancyPool buildWorld() {
    const glm::ivec3 originA{0, 0, 0};
    const glm::ivec3 originB{-32, 0, 0};
    VoxelOccupancyGrid a, b;
    a.setChunkOrigin(originA);
    b.setChunkOrigin(originB);
    for (int x = 0; x < 32; ++x) for (int z = 0; z < 32; ++z) { addSolidCube(a, {x, 2, z}); addSolidCube(b, {x, 2, z}); }
    for (int y = 3; y < 8; ++y) for (int z = 4; z < 12; ++z) addSolidCube(a, {10, y, z});           // solid wall
    for (int y = 3; y < 8; ++y) for (int z = 4; z < 12; ++z)                                            // 1-micro wall at x = 20 (+0 micro)
        for (int sy = 0; sy < 3; ++sy) for (int sz = 0; sz < 3; ++sz)
            for (int my = 0; my < 3; ++my) for (int mz = 0; mz < 3; ++mz)
                addMicrocube(a, {20, y, z}, {0, sy, sz}, {0, my, mz});
    for (int x = 12; x < 18; ++x) for (int z = 12; z < 18; ++z)                                         // 3-micro floor at y = 5 (world y 5.0..5.33)
        for (int sx = 0; sx < 3; ++sx) for (int sz = 0; sz < 3; ++sz)
            for (int mx = 0; mx < 3; ++mx) for (int my = 0; my < 3; ++my) for (int mz = 0; mz < 3; ++mz)
                addMicrocube(a, {x, 5, z}, {sx, 0, sz}, {mx, my, mz});
    for (int y = 3; y < 6; ++y) addSubcube(b, {8, y, 8}, {1, 1, 1});                                   // subcube post
    std::mt19937 rng(7);
    for (int i = 0; i < 300; ++i) {
        const glm::ivec3 lp{int(rng() % 32), 3 + int(rng() % 6), int(rng() % 32)};
        addMicrocube(b, lp, {int(rng() % 3), int(rng() % 3), int(rng() % 3)}, {int(rng() % 3), int(rng() % 3), int(rng() % 3)});
    }
    const glm::ivec3 box = PackedOccupancyPool::boxMinChunkFor(glm::vec3(0.0f, 8.0f, 8.0f));
    return packOccupancyPool({{originA, buildLightOccupancy(a)}, {originB, buildLightOccupancy(b)}}, box);
}

}  // namespace

TEST(OccupancyTraversal, TwoLevelSegmentTestEqualsTheMicroMarchOnRandomSegments) {
    const PackedOccupancyPool packed = buildWorld();
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> px(-31.5f, 31.5f), py(2.2f, 12.0f), pz(0.5f, 31.5f);
    std::uniform_real_distribution<float> lenD(0.2f, 20.0f);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    int blocked = 0, clear = 0, disagreements = 0;
    for (int i = 0; i < 20000; ++i) {
        const glm::vec3 from{px(rng), py(rng), pz(rng)};
        glm::vec3 dir{nd(rng), nd(rng), nd(rng)};
        if (glm::length(dir) < 1e-3f) dir = glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 to = from + glm::normalize(dir) * lenD(rng);
        const bool micro = packedPoolSegmentHitsSolid(packed, from, to);
        const bool two = packedPoolSegmentBlocked(packed, from, to);
        if (micro != two) {
            ++disagreements;
            if (disagreements <= 5)
                ADD_FAILURE() << "segment (" << from.x << "," << from.y << "," << from.z << ") -> ("
                              << to.x << "," << to.y << "," << to.z << "): micro " << micro << " two-level " << two;
        }
        (micro ? blocked : clear) += 1;
    }
    EXPECT_EQ(disagreements, 0);
    // The rig must exercise BOTH answers, or equality proves nothing.
    EXPECT_GT(blocked, 2000);
    EXPECT_GT(clear, 2000);
}

TEST(OccupancyTraversal, AOneMicroWallBlocksTheTwoLevelTestExactlyAsACubeDoes) {
    const PackedOccupancyPool packed = buildWorld();
    // Straight through the 1-micro wall at x = 20 (its slab is micro x = 0 of the cube), y 3..8.
    EXPECT_TRUE(packedPoolSegmentBlocked(packed, {19.5f, 5.5f, 7.5f}, {21.5f, 5.5f, 7.5f}));
    // Straight through the solid wall at x = 10.
    EXPECT_TRUE(packedPoolSegmentBlocked(packed, {9.5f, 5.5f, 7.5f}, {11.5f, 5.5f, 7.5f}));
    // Parallel to both walls, in the air between them: clear.
    EXPECT_FALSE(packedPoolSegmentBlocked(packed, {15.0f, 6.5f, 4.5f}, {15.0f, 6.5f, 11.5f}));
    // Over the top of the 1-micro wall (y 8+): clear.
    EXPECT_FALSE(packedPoolSegmentBlocked(packed, {19.5f, 8.5f, 7.5f}, {21.5f, 8.5f, 7.5f}));
    // Down THROUGH the 3-micro floor slab at y = 5: blocked; the air above it, sideways: clear.
    EXPECT_TRUE(packedPoolSegmentBlocked(packed, {15.5f, 7.0f, 15.5f}, {15.5f, 4.5f, 15.5f}));
    EXPECT_FALSE(packedPoolSegmentBlocked(packed, {12.5f, 5.6f, 15.5f}, {17.5f, 5.6f, 15.5f}));
    // The end-cell rule, made explicit: a segment that ENDS inside the slab's first micro layer has
    // that one cell excluded, in both traversals alike.
    EXPECT_EQ(packedPoolSegmentBlocked(packed, {15.5f, 7.0f, 15.5f}, {15.5f, 5.30f, 15.5f}),
              packedPoolSegmentHitsSolid(packed, {15.5f, 7.0f, 15.5f}, {15.5f, 5.30f, 15.5f}));
    // (Coming from above, the slab's TOP layer is the first one met; ending inside it means the only
    //  solid cell on the path is the excluded end cell -> both say "not blocked". Pinned above.)
    EXPECT_FALSE(packedPoolSegmentBlocked(packed, {15.5f, 7.0f, 15.5f}, {15.5f, 5.30f, 15.5f}));
    // A segment that STARTS inside solid tests its start cell: blocked in both.
    EXPECT_EQ(packedPoolSegmentBlocked(packed, {15.5f, 5.25f, 15.5f}, {15.5f, 8.0f, 15.5f}),
              packedPoolSegmentHitsSolid(packed, {15.5f, 5.25f, 15.5f}, {15.5f, 8.0f, 15.5f}));
    EXPECT_TRUE(packedPoolSegmentBlocked(packed, {15.5f, 5.25f, 15.5f}, {15.5f, 8.0f, 15.5f}));
    // The 1-micro wall really is 1 micro: the micro cell beside it is air.
    EXPECT_TRUE(packedPoolSolidAt(packed, {20 * 9 + 0, 5 * 9 + 4, 7 * 9 + 4}));
    EXPECT_FALSE(packedPoolSolidAt(packed, {20 * 9 + 1, 5 * 9 + 4, 7 * 9 + 4}));
}
