// OccupancyTraversalTest.cpp — the two-level segment test (cube cells first, micro only inside
// mixed cubes) must answer EXACTLY what the micro march answers, on every kind of geometry the
// engine produces. It exists because the probe field's per-fragment leak guard (G-141) needed a
// segment test that costs a tenth of the micro march without giving up sub-voxel truth: a 1-micro
// wall must block a segment exactly as a full cube does (LightingPipeline.md rule R8).
//
// No Vulkan: packedPoolSegmentBlocked is the CPU mirror of phxSegmentBlocked, line for line, and
// packedPoolSegmentHitsSolid is the micro march the shaders have used since M2.
#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
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

// GI-2 (docs/PerfProgram2026-09.md section 15): the probe pass's primary ray must report the micro
// march's hit -- hit or miss, the entry axis (the face normal the bounce is lit with) and the micro
// cell (the bounce reads the probe field at its centre) -- while walking cube cells. Rays are the
// probe pass's shape: 16 u reach, any direction, starting in air or inside geometry.
//
// Hit/miss must be EXACT (the same slicing is what makes phxSegmentBlocked exact). The cell may
// differ only on a genuine float TIE, by one micro cell (0.11 u of bounce position, against a 2 u
// probe lattice). History: taking a solid cube's entry cell 1e-4 u past the face reported the
// neighbour whenever another micro boundary lay within that 1e-4 (35 of 77,643 hits); locating it
// AT the crossing leaves 1 of 77,643. Forbidden: any hit/miss difference, any difference beyond one
// micro cell, or more than 0.01% of hits differing. The naive port (every slice seeded with axis 1)
// gets the NORMAL wrong on ~1.8% of rays.
TEST(OccupancyTraversal, TwoLevelTraceReportsTheMicroMarchHitOnRandomRays) {
    using Phyxel::Graphics::packedPoolTraceMicro;
    using Phyxel::Graphics::packedPoolTraceTwoLevel;
    const PackedOccupancyPool packed = buildWorld();
    std::mt19937 rng(424242);
    std::uniform_real_distribution<float> px(-31.5f, 31.5f), py(2.2f, 12.0f), pz(0.5f, 31.5f);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    int hits = 0, misses = 0, hitsInMixed = 0, hitMissDiffer = 0, axisDiffer = 0, cellDiffer = 0, maxCellDist = 0;
    int probeDiffer = 0;
    for (int i = 0; i < 200000; ++i) {
        const glm::vec3 from{px(rng), py(rng), pz(rng)};
        glm::vec3 dir{nd(rng), nd(rng), nd(rng)};
        if (glm::length(dir) < 1e-3f) dir = glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 to = from + glm::normalize(dir) * 16.0f;
        glm::ivec3 mCell{0}, tCell{0};
        int mAxis = -1, tAxis = -1;
        const bool m = packedPoolTraceMicro(packed, from, to, 288, mCell, mAxis);
        const bool t = packedPoolTraceTwoLevel(packed, from, to, tCell, tAxis);
        glm::ivec3 pCell{0};
        int pAxis = -1;
        const bool pr = Phyxel::Graphics::packedPoolTraceProbe(packed, from, to, pCell, pAxis);
        if (pr != m || (m && (pCell != mCell || pAxis != mAxis))) ++probeDiffer;
        if (m != t) {
            ++hitMissDiffer;
        } else if (m) {
            if (mAxis != tAxis) ++axisDiffer;
            if (mCell != tCell) ++cellDiffer;
            const glm::ivec3 dc = glm::abs(mCell - tCell);
            maxCellDist = std::max(maxCellDist, std::max(dc.x, std::max(dc.y, dc.z)));
        }
        if ((m != t || (m && (mCell != tCell || mAxis != tAxis))) && hitMissDiffer + axisDiffer + cellDiffer <= 5)
            std::cout << "  tie/diff: ray (" << from.x << "," << from.y << "," << from.z << ") dir ("
                      << dir.x << "," << dir.y << "," << dir.z << "): micro " << m << " cell ("
                      << mCell.x << "," << mCell.y << "," << mCell.z << ") axis " << mAxis
                      << " | two-level " << t << " cell (" << tCell.x << "," << tCell.y << ","
                      << tCell.z << ") axis " << tAxis << "\n";
        if (m) {
            ++hits;
            const glm::ivec3 cube{static_cast<int>(std::floor(mCell.x / 9.0f)), static_cast<int>(std::floor(mCell.y / 9.0f)),
                                  static_cast<int>(std::floor(mCell.z / 9.0f))};
            if (Phyxel::Graphics::packedPoolCubeOccupancy(packed, cube) == Phyxel::Graphics::CubeOccupancy::Mixed)
                ++hitsInMixed;
        } else {
            ++misses;
        }
    }
    std::cout << "  rays 200000: hits " << hits << " (in mixed cubes " << hitsInMixed << "), misses " << misses
              << "; differ: hit/miss " << hitMissDiffer << ", axis " << axisDiffer << ", cell " << cellDiffer
              << " (max " << maxCellDist << " micro); probe trace differs " << probeDiffer << "\n";
    EXPECT_EQ(hitMissDiffer, 0);
    EXPECT_LE(maxCellDist, 1);
    EXPECT_LE(axisDiffer * 10000, hits);   // <= 0.01% of hits (measured: 0)
    EXPECT_LE(cellDiffer * 10000, hits);   // measured: 1 of 77,643
    EXPECT_LE(probeDiffer * 10000, hits);
    // The rig must exercise hits, misses AND hits resolved inside mixed cubes, or equality proves nothing.
    EXPECT_GT(hits, 20000);
    EXPECT_GT(misses, 20000);
    EXPECT_GT(hitsInMixed, 3000);   // 20,000-ray run: 404, counted from the micro march
}

// PROBE-SHAPED rays: the probe pass starts every ray ON the 2 u lattice, i.e. exactly on a cube corner,
// where the micro march resolves zero-length steps in its fixed tie order. Random float starts (the
// test above) never produce that, which is how the plain cube walk shipped a first version that
// differed from the micro march on 49 of these rays while passing the random-start test. The probe
// trace (micro march for the first unit, cube walk beyond) must match the micro march here; the plain
// cube walk's disagreement is asserted non-zero so this test keeps proving what random starts miss.
TEST(OccupancyTraversal, ProbeShapedRaysProbeTraceMatchesPlainCubeWalkDoesNot) {
    using Phyxel::Graphics::packedPoolTraceMicro;
    using Phyxel::Graphics::packedPoolTraceProbe;
    using Phyxel::Graphics::packedPoolTraceTwoLevel;
    const PackedOccupancyPool packed = buildWorld();
    const glm::vec3 dirs[18] = {
        {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},
        {0.707f, 0.707f, 0}, {-0.707f, 0.707f, 0}, {0.707f, -0.707f, 0}, {-0.707f, -0.707f, 0},
        {0, 0.707f, 0.707f}, {0, 0.707f, -0.707f}, {0, -0.707f, 0.707f}, {0, -0.707f, -0.707f},
        {0.707f, 0, 0.707f}, {-0.707f, 0, 0.707f}, {0.707f, 0, -0.707f}, {-0.707f, 0, -0.707f}};
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    int rays = 0, hits = 0, skipDiffer = 0, walkDiffer = 0, probeHitMiss = 0;
    for (int x = -30; x <= 30; x += 2)
        for (int y = 4; y <= 12; y += 2)
            for (int z = 2; z <= 30; z += 2) {
                // One rotation per probe, as gi_probe.comp: uniform spin about Y, uniform axis tilt about X.
                const float yaw = u01(rng) * 6.2831853f, tilt = std::acos(1.0f - 2.0f * u01(rng));
                const glm::mat3 spin(std::cos(yaw), 0, -std::sin(yaw), 0, 1, 0, std::sin(yaw), 0, std::cos(yaw));
                const glm::mat3 tip(1, 0, 0, 0, std::cos(tilt), std::sin(tilt), 0, -std::sin(tilt), std::cos(tilt));
                const glm::vec3 from{float(x), float(y), float(z)};
                for (const glm::vec3& d0 : dirs) {
                    const glm::vec3 to = from + (spin * tip * d0) * 16.0f;
                    glm::ivec3 mc{0}, sc{0}, wc{0};
                    int ma = -1, sa = -1, wa = -1;
                    const bool m = packedPoolTraceMicro(packed, from, to, 288, mc, ma);
                    const bool s = packedPoolTraceProbe(packed, from, to, sc, sa);
                    const bool w = packedPoolTraceTwoLevel(packed, from, to, wc, wa);
                    ++rays;
                    hits += m ? 1 : 0;
                    if (m != s) ++probeHitMiss;
                    if (m != s || (m && (mc != sc || ma != sa))) ++skipDiffer;
                    if (m != w || (m && (mc != wc || ma != wa))) ++walkDiffer;
                }
            }
    std::cout << "  probe-shaped rays " << rays << ": hits " << hits << "; probe trace differs " << skipDiffer
              << " (hit/miss " << probeHitMiss << "), plain cube walk differs " << walkDiffer << "\n";
    EXPECT_EQ(probeHitMiss, 0);
    EXPECT_LE(skipDiffer * 10000, rays);   // float ties only (<= 0.01%), as on random rays
    EXPECT_GT(walkDiffer, 0);
    EXPECT_GT(hits, rays / 10);
}

// L3a (docs/PerfProgram2026-09.md section 16.11): point-light visibility now walks two-level on the GPU.
// It must answer exactly what the micro-march REFERENCE (packedPoolLightVisibility, which every lighting
// test asserts against) answers: same start offset, same measured emitter run, same target. Random
// surface points with axis normals, lights within 20 u (light radii are at most ~20 u), some lights
// placed INSIDE solid cells (emissive voxels) so the emitter-run path is exercised.
TEST(OccupancyTraversal, LightVisibilityTwoLevelEqualsTheMicroMarch) {
    using Phyxel::Graphics::packedPoolLightVisibility;
    using Phyxel::Graphics::packedPoolLightVisibleTwoLevel;
    const PackedOccupancyPool packed = buildWorld();
    std::mt19937 rng(777);
    std::uniform_real_distribution<float> px(-31.5f, 31.5f), py(3.1f, 12.0f), pz(0.5f, 31.5f), off(-14.0f, 14.0f);
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int visible = 0, blocked = 0, differ = 0, inSolidLights = 0;
    for (int i = 0; i < 20000; ++i) {
        const glm::vec3 surf{px(rng), py(rng), pz(rng)};
        const glm::vec3 n = normals[rng() % 6];
        glm::vec3 light = surf + glm::vec3(off(rng), off(rng) * 0.5f, off(rng));
        if (i % 5 == 0) light = glm::floor(light) + 0.5f;         // at a cube centre, as emissive voxels are
        if (packedPoolSolidAt(packed, glm::ivec3(glm::floor(light * 9.0f)))) ++inSolidLights;
        const bool ref = packedPoolLightVisibility(packed, surf, n, light, /*maxSteps, never binding*/ 4096).visible;
        const bool two = packedPoolLightVisibleTwoLevel(packed, surf, n, light);
        if (ref != two) {
            ++differ;
            if (differ <= 5)
                ADD_FAILURE() << "surface (" << surf.x << "," << surf.y << "," << surf.z << ") n (" << n.x << ","
                              << n.y << "," << n.z << ") light (" << light.x << "," << light.y << "," << light.z
                              << "): micro " << ref << " two-level " << two;
        }
        (ref ? visible : blocked) += 1;
    }
    std::cout << "  light visibility: visible " << visible << ", blocked " << blocked << ", lights in solid "
              << inSolidLights << ", differ " << differ << "\n";
    EXPECT_EQ(differ, 0);
    EXPECT_GT(visible, 2000);
    EXPECT_GT(blocked, 2000);
    EXPECT_GT(inSolidLights, 100);
}
