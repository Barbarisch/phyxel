// VoxelLightOccupancyTest.cpp — M1 gate: the flattened occupancy must agree with the source grid
// CELL FOR CELL, including partially-filled cells.
//
// That last clause is the whole point. The lighting system this replaces stored ONE value per cube
// cell, so a cell that is 1/3 floor and 2/3 standing room had to be rounded to solid or empty —
// which is what produced the hard black band along every interior wall base (measured live: 240/255
// on the wall above the junction, 3.6/255 at it). If the flattening rounds mixed cells the same
// way, the replacement inherits the same defect, so mixed cells are tested explicitly and at the
// real thicknesses the generator produces (2-micro walls, 3-micro floors).
//
// No Vulkan, no rendering: this is the CPU mirror of the traversal the shader will perform, so the
// representation can be proven correct before any GPU plumbing exists to blame.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <tuple>
#include <vector>

#include "graphics/VoxelLightOccupancy.h"
#include "graphics/VoxelLightOccupancyGpu.h"
#include "physics/VoxelOccupancyGrid.h"

using Phyxel::Graphics::ChunkLightOccupancy;
using Phyxel::Graphics::buildLightOccupancy;
using Phyxel::Physics::VoxelOccupancyGrid;

namespace {

/// Mark one cube fully solid, the way ChunkPhysicsManager does for a plain cube.
void addSolidCube(VoxelOccupancyGrid& g, const glm::ivec3& lp) {
    g.setCube(lp, true);
}

/// Mark one subcube, the way ChunkPhysicsManager does: the cube bit is set AND flagged subdivided.
void addSubcube(VoxelOccupancyGrid& g, const glm::ivec3& lp, const glm::ivec3& sp) {
    g.setCube(lp, true);
    g.markSubdivided(lp, true);
    g.setSubcube(lp, sp, true);
}

/// Mark one microcube: cube subdivided, subcube set AND flagged subdivided, micro set.
void addMicrocube(VoxelOccupancyGrid& g, const glm::ivec3& lp, const glm::ivec3& sp,
                  const glm::ivec3& mp) {
    g.setCube(lp, true);
    g.markSubdivided(lp, true);
    g.setSubcube(lp, sp, true);
    g.markSubcubeSubdivided(lp, sp, true);
    g.setMicrocube(lp, sp, mp, true);
}

/// Every micro cell of one cube, asked of the SOURCE grid, mirroring queryAABB's rules.
bool gridSaysSolid(const VoxelOccupancyGrid& g, const glm::ivec3& lp, const glm::ivec3& inCube) {
    if (!g.isCubeFilled(lp)) return false;
    if (!g.isSubdivided(lp)) return true;
    const glm::ivec3 sp{inCube.x / 3, inCube.y / 3, inCube.z / 3};
    if (!g.isSubcubeFilled(lp, sp)) return false;
    if (!g.isSubcubeSubdivided(lp, sp)) return true;
    return g.isMicrocubeFilled(lp, sp, {inCube.x % 3, inCube.y % 3, inCube.z % 3});
}

/// Compare the flattened form against the grid for every micro cell of the listed cubes.
void expectAgreesOverCubes(const VoxelOccupancyGrid& g, const ChunkLightOccupancy& occ,
                           const std::vector<glm::ivec3>& cubes) {
    for (const auto& lp : cubes) {
        for (int mx = 0; mx < 9; ++mx)
        for (int my = 0; my < 9; ++my)
        for (int mz = 0; mz < 9; ++mz) {
            const glm::ivec3 inCube{mx, my, mz};
            const glm::ivec3 microLocal{lp.x * 9 + mx, lp.y * 9 + my, lp.z * 9 + mz};
            EXPECT_EQ(occ.solidAtMicro(microLocal), gridSaysSolid(g, lp, inCube))
                << "disagreement at cube (" << lp.x << "," << lp.y << "," << lp.z
                << ") micro (" << mx << "," << my << "," << mz << ")";
        }
    }
}

}  // namespace

TEST(VoxelLightOccupancy, EmptyGridIsEmptyEverywhere) {
    VoxelOccupancyGrid g;
    const auto occ = buildLightOccupancy(g);
    EXPECT_TRUE(occ.mixedCubeIdx.empty());
    EXPECT_TRUE(occ.microWords.empty());
    EXPECT_FALSE(occ.solidAtMicro({0, 0, 0}));
    EXPECT_FALSE(occ.solidAtMicro({100, 100, 100}));
}

TEST(VoxelLightOccupancy, AFullCubeIsSolidAtEveryMicroCellAndCarriesNoDetail) {
    VoxelOccupancyGrid g;
    addSolidCube(g, {4, 5, 6});
    const auto occ = buildLightOccupancy(g);

    EXPECT_TRUE(occ.cubeIsSolid(ChunkLightOccupancy::cubeIndex({4, 5, 6})));
    EXPECT_FALSE(occ.cubeIsMixed(ChunkLightOccupancy::cubeIndex({4, 5, 6})));
    EXPECT_TRUE(occ.mixedCubeIdx.empty()) << "a fully solid cube must cost no detail storage";

    expectAgreesOverCubes(g, occ, {{4, 5, 6}});
    EXPECT_FALSE(occ.solidAtMicro({3 * 9, 5 * 9, 6 * 9})) << "the neighbouring cube is empty";
}

TEST(VoxelLightOccupancy, OneSubcubeFillsExactlyItsTwentySevenMicroCells) {
    VoxelOccupancyGrid g;
    const glm::ivec3 lp{2, 2, 2}, sp{1, 0, 2};
    addSubcube(g, lp, sp);
    const auto occ = buildLightOccupancy(g);

    const int ci = ChunkLightOccupancy::cubeIndex(lp);
    EXPECT_FALSE(occ.cubeIsSolid(ci)) << "a subdivided cube is not fully solid";
    EXPECT_TRUE(occ.cubeIsMixed(ci));

    int solidCount = 0;
    for (int mx = 0; mx < 9; ++mx)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 9; ++mz)
        if (occ.solidAtMicro({lp.x * 9 + mx, lp.y * 9 + my, lp.z * 9 + mz})) ++solidCount;
    EXPECT_EQ(solidCount, 27) << "one subcube is 27 of the cube's 729 micro cells";

    expectAgreesOverCubes(g, occ, {lp});
}

TEST(VoxelLightOccupancy, OneMicrocubeFillsExactlyOneCell) {
    VoxelOccupancyGrid g;
    const glm::ivec3 lp{7, 1, 3}, sp{2, 1, 0}, mp{0, 2, 1};
    addMicrocube(g, lp, sp, mp);
    const auto occ = buildLightOccupancy(g);

    int solidCount = 0;
    for (int mx = 0; mx < 9; ++mx)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 9; ++mz)
        if (occ.solidAtMicro({lp.x * 9 + mx, lp.y * 9 + my, lp.z * 9 + mz})) ++solidCount;
    EXPECT_EQ(solidCount, 1);
    EXPECT_TRUE(occ.solidAtMicro({lp.x * 9 + sp.x * 3 + mp.x,
                                  lp.y * 9 + sp.y * 3 + mp.y,
                                  lp.z * 9 + sp.z * 3 + mp.z}));
    expectAgreesOverCubes(g, occ, {lp});
}

// THE case the old system could not represent: a cell that is part wall and part room.
TEST(VoxelLightOccupancy, PartiallyFilledCellsAreRepresentedNotRounded) {
    VoxelOccupancyGrid g;
    const glm::ivec3 lp{10, 17, 9};

    // A 3-micro floor slab occupying the bottom third of the cell — the exact geometry that made
    // the old field mark the whole cell opaque with zero light, producing the wall-base band.
    for (int mx = 0; mx < 9; ++mx)
    for (int mz = 0; mz < 9; ++mz)
    for (int my = 0; my < 3; ++my)
        addMicrocube(g, lp, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});

    const auto occ = buildLightOccupancy(g);
    const int ci = ChunkLightOccupancy::cubeIndex(lp);

    EXPECT_FALSE(occ.cubeIsSolid(ci))
        << "a cell that is 1/3 floor and 2/3 room must NOT read as fully solid — rounding it that "
           "way is exactly the defect this replaces";
    EXPECT_TRUE(occ.cubeIsMixed(ci));

    for (int mx = 0; mx < 9; ++mx)
    for (int mz = 0; mz < 9; ++mz) {
        for (int my = 0; my < 3; ++my)
            EXPECT_TRUE(occ.solidAtMicro({lp.x * 9 + mx, lp.y * 9 + my, lp.z * 9 + mz}))
                << "the floor slab itself must be solid";
        for (int my = 3; my < 9; ++my)
            EXPECT_FALSE(occ.solidAtMicro({lp.x * 9 + mx, lp.y * 9 + my, lp.z * 9 + mz}))
                << "the standing room above the slab must be OPEN";
    }
    expectAgreesOverCubes(g, occ, {lp});
}

// A 2-micro wall — timber_cottage's exterior_wall default, and the thickness that leaked 12 of 15
// light levels through the old field.
TEST(VoxelLightOccupancy, ATwoMicroWallIsSolidWhereItIsAndOpenWhereItIsNot) {
    VoxelOccupancyGrid g;
    const glm::ivec3 lp{5, 5, 5};
    for (int mx = 0; mx < 9; ++mx)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 2; ++mz)
        addMicrocube(g, lp, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});

    const auto occ = buildLightOccupancy(g);
    EXPECT_FALSE(occ.cubeIsSolid(ChunkLightOccupancy::cubeIndex(lp)));
    for (int mz = 0; mz < 2; ++mz)
        EXPECT_TRUE(occ.solidAtMicro({lp.x * 9 + 4, lp.y * 9 + 4, lp.z * 9 + mz}));
    for (int mz = 2; mz < 9; ++mz)
        EXPECT_FALSE(occ.solidAtMicro({lp.x * 9 + 4, lp.y * 9 + 4, lp.z * 9 + mz}));
    expectAgreesOverCubes(g, occ, {lp});
}

// The shader binary-searches mixedCubeIdx, so it must be sorted. Asserted, not assumed.
TEST(VoxelLightOccupancy, MixedCubeIndicesAreSortedAndSlotsResolve) {
    VoxelOccupancyGrid g;
    const std::vector<glm::ivec3> cubes = {{9, 2, 1}, {0, 0, 5}, {31, 31, 31}, {3, 20, 7}};
    for (const auto& lp : cubes) addSubcube(g, lp, {1, 1, 1});
    const auto occ = buildLightOccupancy(g);

    ASSERT_EQ(occ.mixedCubeIdx.size(), cubes.size());
    EXPECT_TRUE(std::is_sorted(occ.mixedCubeIdx.begin(), occ.mixedCubeIdx.end()))
        << "the shader binary-searches this array; unsorted means silently wrong occupancy";
    EXPECT_EQ(occ.microWords.size(),
              occ.mixedCubeIdx.size() * ChunkLightOccupancy::kMicroWordsPerCube);

    for (const auto& lp : cubes) {
        const int slot = occ.mixedSlot(ChunkLightOccupancy::cubeIndex(lp));
        EXPECT_GE(slot, 0) << "no detail slot for a cube that has detail";
    }
    EXPECT_EQ(occ.mixedSlot(ChunkLightOccupancy::cubeIndex({2, 2, 2})), -1)
        << "a cube with no detail must not resolve to a slot";
}

// ---------------------------------------------------------------------------------------------
// The PACKED POOL — the exact bytes and addressing that reach the GPU.
//
// Tested on the CPU because addressing is the part that silently goes wrong, and a wrong offset
// inside a shader produces a picture nobody can debug. packedPoolSolidAt() is the mirror of the
// GLSL that M2 will write; if these two ever diverge, this suite is what catches it.
// ---------------------------------------------------------------------------------------------

using Phyxel::Graphics::PackedOccupancyPool;
using Phyxel::Graphics::packOccupancyPool;
using Phyxel::Graphics::packedPoolSolidAt;

// The box centred on the world origin — what a viewer standing at (0,0,0) gets. Most tests use
// this so they read exactly as they did before the box became viewer-relative.
static const glm::ivec3 kOriginBox = PackedOccupancyPool::boxMinChunkFor(glm::vec3(0.0f));

TEST(VoxelLightOccupancy, PackedPoolAgreesWithTheGridAtWorldCoordinates) {
    // Two chunks at different origins, INCLUDING a negative one — world coordinates go negative
    // and truncating division would fold -1 and 0 into the same chunk.
    const glm::ivec3 originA{0, 0, 0};
    const glm::ivec3 originB{-32, 0, 64};

    VoxelOccupancyGrid a, b;
    a.setChunkOrigin(originA);
    b.setChunkOrigin(originB);

    addSolidCube(a, {1, 2, 3});
    // A 3-micro floor slab: the mixed case, in chunk A.
    for (int mx = 0; mx < 9; ++mx)
    for (int mz = 0; mz < 9; ++mz)
    for (int my = 0; my < 3; ++my)
        addMicrocube(a, {5, 5, 5}, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});
    addSubcube(b, {0, 0, 0}, {2, 2, 2});
    addSolidCube(b, {31, 7, 30});

    const auto blobA = buildLightOccupancy(a);
    const auto blobB = buildLightOccupancy(b);
    const auto packed = packOccupancyPool({{originA, blobA}, {originB, blobB}}, kOriginBox);

    auto checkCube = [&](const glm::ivec3& origin, const VoxelOccupancyGrid& g,
                         const glm::ivec3& lp) {
        for (int mx = 0; mx < 9; ++mx)
        for (int my = 0; my < 9; ++my)
        for (int mz = 0; mz < 9; ++mz) {
            const glm::ivec3 worldMicro{(origin.x + lp.x) * 9 + mx,
                                        (origin.y + lp.y) * 9 + my,
                                        (origin.z + lp.z) * 9 + mz};
            EXPECT_EQ(packedPoolSolidAt(packed, worldMicro), gridSaysSolid(g, lp, {mx, my, mz}))
                << "packed pool disagrees at world micro (" << worldMicro.x << ","
                << worldMicro.y << "," << worldMicro.z << ")";
        }
    };

    checkCube(originA, a, {1, 2, 3});
    checkCube(originA, a, {5, 5, 5});     // the mixed floor-slab cell
    checkCube(originB, b, {0, 0, 0});     // negative-origin chunk
    checkCube(originB, b, {31, 7, 30});   // far corner of a negative-origin chunk
}

TEST(VoxelLightOccupancy, PackedPoolReportsNotSolidOutsideItsBoxRatherThanReadingGarbage) {
    VoxelOccupancyGrid g;
    addSolidCube(g, {0, 0, 0});
    const auto packed = packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}},
                                          kOriginBox);

    // Well outside the covered world box in every direction. Degrading to "no occlusion" is the
    // safe failure: a wrong TRUE would carve phantom shadows out of open sky.
    EXPECT_FALSE(packedPoolSolidAt(packed, {-9999, 0, 0}));
    EXPECT_FALSE(packedPoolSolidAt(packed, {9999, 0, 0}));
    EXPECT_FALSE(packedPoolSolidAt(packed, {0, -9999, 0}));
    EXPECT_FALSE(packedPoolSolidAt(packed, {0, 9999, 0}));
    EXPECT_FALSE(packedPoolSolidAt(packed, {0, 0, 99999}));
    // An in-box chunk that was never uploaded reads empty, not stale.
    EXPECT_FALSE(packedPoolSolidAt(packed, {100 * 9, 0, 100 * 9}));
}

TEST(VoxelLightOccupancy, DirectoryIndexingHandlesNegativeChunkOrigins) {
    // The bug this guards: truncating division folds chunk -32 and chunk 0 onto the same slot,
    // which would make one chunk silently overwrite the other's directory entry.
    const int a = PackedOccupancyPool::directoryIndex({0, 0, 0}, kOriginBox);
    const int b = PackedOccupancyPool::directoryIndex({-32, 0, 0}, kOriginBox);
    const int c = PackedOccupancyPool::directoryIndex({-64, 0, 0}, kOriginBox);
    EXPECT_GE(a, 0);
    EXPECT_GE(b, 0);
    EXPECT_GE(c, 0);
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_EQ(PackedOccupancyPool::directoryIndex({-4096, 0, 0}, kOriginBox), -1)
        << "outside the box";

    // A micro position inside a negative chunk must resolve to that chunk, not its neighbour.
    EXPECT_EQ(PackedOccupancyPool::directoryIndexForMicro({-1, 0, 0}, kOriginBox), b)
        << "world micro -1 belongs to the chunk at origin -32, not the one at 0";
}

// ---------------------------------------------------------------------------------------------
// The box follows the viewer. This is what makes the system usable in a real world at all: fixed
// at the origin, the box covered x,z in [-256,256), while this repo's own default.db has placed
// objects at x ~ 611. Those chunks would have occluded nothing, and their interiors would have lit
// as though the buildings had no walls.
// ---------------------------------------------------------------------------------------------
TEST(VoxelLightOccupancy, RecentringTheBoxCoversGeometryFarFromTheWorldOrigin) {
    // A chunk at x = 608 — where this repo's default.db actually has content, and far outside the
    // box centred on the origin.
    const glm::ivec3 farOrigin{608, 0, 0};
    VoxelOccupancyGrid g;
    g.setChunkOrigin(farOrigin);
    addSolidCube(g, {4, 4, 4});
    const auto blob = buildLightOccupancy(g);

    // Centred on the world origin, that chunk is unreachable — and must degrade to "not solid".
    const auto atOrigin = packOccupancyPool({{farOrigin, blob}}, kOriginBox);
    EXPECT_EQ(PackedOccupancyPool::directoryIndex(farOrigin, kOriginBox), -1);
    EXPECT_FALSE(packedPoolSolidAt(atOrigin, {(608 + 4) * 9, 4 * 9, 4 * 9}))
        << "a chunk outside the box must read as no-occlusion, never as wrong geometry";

    // Recentre a viewer on it, and the SAME chunk becomes addressable and reads solid.
    const glm::ivec3 farBox = PackedOccupancyPool::boxMinChunkFor(glm::vec3(620.0f, 20.0f, 10.0f));
    const auto atFar = packOccupancyPool({{farOrigin, blob}}, farBox);
    EXPECT_GE(PackedOccupancyPool::directoryIndex(farOrigin, farBox), 0);
    EXPECT_TRUE(packedPoolSolidAt(atFar, {(608 + 4) * 9, 4 * 9, 4 * 9}))
        << "recentring the box must make far-from-origin geometry occlude";

    // Positive control at the other end: with the box moved out to x=620, the world origin is now
    // the one outside coverage. The box trades one region for another; it does not cover both.
    VoxelOccupancyGrid h;
    addSolidCube(h, {4, 4, 4});
    const auto atFarWithOriginChunk =
        packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(h)}}, farBox);
    EXPECT_FALSE(packedPoolSolidAt(atFarWithOriginChunk, {4 * 9, 4 * 9, 4 * 9}))
        << "the box is finite: moving it must actually drop what it left behind";
}

// ---------------------------------------------------------------------------------------------
// CHUNKS MUST NOT BE VISIBLE (CLAUDE.md standing rule, FloraMarginTest style).
//
// Occupancy is built per chunk and packed per chunk, so every chunk boundary is a place where an
// addressing slip could change the answer. It must not: whether a wall is solid is a pure function
// of WORLD position, never of which chunk happens to own it. If this ever failed, the visible
// symptom would be light leaking in a seam-shaped line through solid geometry — a chunk-shaped
// artifact, which is exactly the class of bug the rule exists to forbid.
//
// The reference is a pure world-position predicate, so "whole region" is not a second data
// structure that could share a bug with the first — it is arithmetic on world coordinates.
// ---------------------------------------------------------------------------------------------
namespace {

/// A deterministic, pure function of WORLD micro position. Deliberately asymmetric in x/y/z and
/// finer than a cube, so any swapped axis, truncating divide or off-by-one at a seam changes the
/// answer instead of cancelling out. A symmetric or cube-aligned shape would pass while broken.
bool referenceSolid(const glm::ivec3& worldMicro) {
    const int h = worldMicro.x * 7 + worldMicro.y * 13 + worldMicro.z * 29;
    return ((h % 11) + 11) % 11 < 4;      // positive modulo: world coords go negative
}

/// Populate one chunk's grid from `referenceSolid`, using the same three-level authoring path
/// ChunkPhysicsManager uses: fully-solid cubes stay cubes, uniform subcubes stay subcubes, and
/// only genuinely mixed subcubes descend to microcubes.
/// `regionMin`/`regionMax` bound the WORLD cubes worth authoring (half-open). Filling all 32768
/// cells of eight chunks at micro resolution is ~191M predicate evaluations and took 146 s in
/// Debug; bounding it to the band the test actually queries keeps the same property at a fraction
/// of the cost. Cubes outside the region are simply absent, which the test never asks about.
void fillGridFromReference(VoxelOccupancyGrid& g, const glm::ivec3& chunkOrigin,
                           const glm::ivec3& regionMin, const glm::ivec3& regionMax) {
    g.setChunkOrigin(chunkOrigin);
    for (int cx = 0; cx < 32; ++cx)
    for (int cy = 0; cy < 32; ++cy)
    for (int cz = 0; cz < 32; ++cz) {
        const glm::ivec3 lp{cx, cy, cz};
        const glm::ivec3 cubeWorld = chunkOrigin + lp;
        if (cubeWorld.x < regionMin.x || cubeWorld.x >= regionMax.x ||
            cubeWorld.y < regionMin.y || cubeWorld.y >= regionMax.y ||
            cubeWorld.z < regionMin.z || cubeWorld.z >= regionMax.z) continue;

        bool anyMicro = false, allMicro = true;
        for (int m = 0; m < 729 && (!anyMicro || allMicro); ++m) {
            const glm::ivec3 in{m % 9, (m / 9) % 9, m / 81};
            const bool s = referenceSolid(cubeWorld * 9 + in);
            anyMicro |= s;
            allMicro &= s;
        }
        if (!anyMicro) continue;
        if (allMicro) { g.setCube(lp, true); continue; }

        g.setCube(lp, true);
        g.markSubdivided(lp, true);
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz) {
            const glm::ivec3 sp{sx, sy, sz};
            bool anySub = false, allSub = true;
            for (int mx = 0; mx < 3; ++mx)
            for (int my = 0; my < 3; ++my)
            for (int mz = 0; mz < 3; ++mz) {
                const glm::ivec3 in{sx * 3 + mx, sy * 3 + my, sz * 3 + mz};
                const bool s = referenceSolid(cubeWorld * 9 + in);
                anySub |= s;
                allSub &= s;
            }
            if (!anySub) continue;
            g.setSubcube(lp, sp, true);
            if (allSub) continue;                 // uniform subcube — no micro detail needed
            g.markSubcubeSubdivided(lp, sp, true);
            for (int mx = 0; mx < 3; ++mx)
            for (int my = 0; my < 3; ++my)
            for (int mz = 0; mz < 3; ++mz) {
                const glm::ivec3 in{sx * 3 + mx, sy * 3 + my, sz * 3 + mz};
                if (referenceSolid(cubeWorld * 9 + in)) g.setMicrocube(lp, sp, {mx, my, mz}, true);
            }
        }
    }
}

}  // namespace

TEST(VoxelLightOccupancy, ChunkedOccupancyEqualsTheWholeRegionAcrossEverySeam) {
    // 2x2x2 chunks meeting at the world origin, so the tested region straddles the x, y AND z
    // seams at once, and half of it sits at NEGATIVE world coordinates — where truncating division
    // would fold chunk -32 and chunk 0 together.
    const glm::ivec3 regionMin{-14, -14, -2}, regionMax{14, 14, 2};

    std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> chunks;
    for (int ox = -1; ox <= 0; ++ox)
    for (int oy = -1; oy <= 0; ++oy)
    for (int oz = -1; oz <= 0; ++oz) {
        const glm::ivec3 origin{ox * 32, oy * 32, oz * 32};
        VoxelOccupancyGrid g;
        fillGridFromReference(g, origin, regionMin, regionMax);
        chunks.emplace_back(origin, buildLightOccupancy(g));
    }
    const auto packed = packOccupancyPool(chunks, kOriginBox);

    // Walk a band of micro cells straddling each seam. Every one must match the world-position
    // reference regardless of which chunk owns it.
    int checked = 0, mismatches = 0, solidSeen = 0;
    glm::ivec3 firstBad{0};
    for (int wx = regionMin.x; wx < regionMax.x; ++wx)
    for (int wy = regionMin.y; wy < regionMax.y; ++wy)
    for (int wz = regionMin.z; wz < regionMax.z; ++wz)
    for (int m = 0; m < 729; ++m) {
        const glm::ivec3 in{m % 9, (m / 9) % 9, m / 81};
        const glm::ivec3 wm = glm::ivec3{wx, wy, wz} * 9 + in;
        ++checked;
        const bool got = packedPoolSolidAt(packed, wm);
        if (got) ++solidSeen;
        if (got != referenceSolid(wm)) {
            if (mismatches == 0) firstBad = wm;
            ++mismatches;
        }
    }
    // Controls: an all-empty or all-solid region would agree trivially and prove nothing.
    EXPECT_GT(solidSeen, 0) << "nothing read solid; the comparison is vacuous";
    EXPECT_LT(solidSeen, checked) << "everything read solid; micro addressing is untested";
    EXPECT_EQ(mismatches, 0)
        << mismatches << " of " << checked << " micro cells disagree with the world-position "
        << "reference; first at world micro (" << firstBad.x << "," << firstBad.y << ","
        << firstBad.z << "). Occupancy has become chunk-dependent.";
}

TEST(VoxelLightOccupancy, TheSameShapeReadsTheSameWhereverTheChunkSeamsFall) {
    // The other half of the rule: it is not enough that chunked == reference for ONE alignment.
    // The same shape, shifted so the seams cut it in a different place, must read identically
    // relative to its own origin. This is what FloraMarginTest pins for flora.
    auto buildAt = [](const glm::ivec3& shapeOrigin) {
        // A 2-micro wall and a 3-micro floor — the real generator thicknesses — placed relative to
        // shapeOrigin and deliberately long enough to cross a chunk boundary.
        std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> out;
        std::map<glm::ivec3, VoxelOccupancyGrid, bool(*)(const glm::ivec3&, const glm::ivec3&)> grids(
            [](const glm::ivec3& a, const glm::ivec3& b) {
                return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
            });

        auto floorDiv32 = [](int v) { return (v >= 0) ? v / 32 : -(((-v) + 31) / 32); };
        auto add = [&](int wcx, int wcy, int wcz, const glm::ivec3& sp, const glm::ivec3& mp) {
            const glm::ivec3 origin{floorDiv32(wcx) * 32, floorDiv32(wcy) * 32, floorDiv32(wcz) * 32};
            auto it = grids.find(origin);
            if (it == grids.end()) {
                VoxelOccupancyGrid g;
                g.setChunkOrigin(origin);
                it = grids.emplace(origin, std::move(g)).first;
            }
            addMicrocube(it->second, {wcx - origin.x, wcy - origin.y, wcz - origin.z}, sp, mp);
        };

        for (int i = 0; i < 40; ++i) {                       // 40 cubes long: crosses a seam
            const int wx = shapeOrigin.x + i;
            // 3-micro floor at the bottom of the cell.
            for (int mx = 0; mx < 9; ++mx)
            for (int mz = 0; mz < 9; ++mz)
            for (int my = 0; my < 3; ++my)
                add(wx, shapeOrigin.y, shapeOrigin.z,
                    {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});
            // 2-micro wall on the cell's -z face, one cube above the floor.
            for (int mx = 0; mx < 9; ++mx)
            for (int my = 0; my < 9; ++my)
            for (int mz = 0; mz < 2; ++mz)
                add(wx, shapeOrigin.y + 1, shapeOrigin.z,
                    {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});
        }
        for (auto& [origin, g] : grids) out.emplace_back(origin, buildLightOccupancy(g));
        return out;
    };

    // Two alignments: one whose shape starts on a chunk boundary, one deliberately offset so every
    // seam cuts the wall and floor at a different micro column.
    const glm::ivec3 alignedOrigin{0, 8, 8};
    const glm::ivec3 offsetOrigin{17, 8 + 13, 8 + 5};

    const auto packedA = packOccupancyPool(buildAt(alignedOrigin), kOriginBox);
    const auto packedB = packOccupancyPool(buildAt(offsetOrigin), kOriginBox);

    int checked = 0, mismatches = 0, solidSeen = 0;
    glm::ivec3 firstBad{0};
    for (int i = 0; i < 40; ++i)
    for (int dy = 0; dy < 2; ++dy)
    for (int m = 0; m < 729; ++m) {
        const glm::ivec3 in{m % 9, (m / 9) % 9, m / 81};
        const glm::ivec3 rel = glm::ivec3{i, dy, 0} * 9 + in;
        const bool a = packedPoolSolidAt(packedA, alignedOrigin * 9 + rel);
        const bool b = packedPoolSolidAt(packedB, offsetOrigin * 9 + rel);
        ++checked;
        if (a) ++solidSeen;
        if (a != b) { if (mismatches == 0) firstBad = rel; ++mismatches; }
    }

    // Control: a test that read nothing solid would "pass" trivially. The shape must actually be
    // there, and it must be PARTIAL (a fully solid region would not exercise micro addressing).
    EXPECT_GT(solidSeen, 0) << "the shape is not present at all; the comparison proves nothing";
    EXPECT_LT(solidSeen, checked) << "the region is fully solid; micro addressing is untested";
    EXPECT_EQ(mismatches, 0)
        << mismatches << " of " << checked << " cells differ between the two chunk alignments; "
        << "first at shape-relative micro (" << firstBad.x << "," << firstBad.y << ","
        << firstBad.z << "). The seams are visible in the occupancy.";
}

// ---------------------------------------------------------------------------------------------
// OVERFLOW AND EVICTION. Both were previously unexercised: the pool held one chunk in every live
// run, so "drops rather than truncates" and "forgets what the box left behind" were claims, not
// results. Neither needs a Vulkan device — the overflow policy is a pure function, and residency
// bookkeeping touches no Vulkan at all.
// ---------------------------------------------------------------------------------------------

TEST(VoxelLightOccupancy, PoolOverflowDropsWholeChunksAndNeverTruncatesOne) {
    using Phyxel::Graphics::blobWords;
    using Phyxel::Graphics::selectChunksThatFit;

    // Three chunks with real content, so their blobs differ in size.
    std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> chunks;
    for (int i = 0; i < 3; ++i) {
        VoxelOccupancyGrid g;
        const glm::ivec3 origin{i * 32, 0, 0};
        g.setChunkOrigin(origin);
        for (int k = 0; k <= i; ++k)                       // later chunks carry more detail
            addMicrocube(g, {k, 0, 0}, {0, 0, 0}, {0, 0, 0});
        chunks.emplace_back(origin, buildLightOccupancy(g));
    }

    const size_t w0 = blobWords(chunks[0].second);
    const size_t w1 = blobWords(chunks[1].second);

    // Capacity for the first two only.
    size_t dropped = 0;
    const auto kept = selectChunksThatFit(chunks, w0 + w1, dropped);
    EXPECT_EQ(kept.size(), 2u);
    EXPECT_EQ(dropped, 1u);

    // THE invariant: what survived must be packed WHOLE. Pack the kept set and confirm every kept
    // chunk still answers correctly — a truncated blob would read as garbage geometry instead.
    const auto packed = packOccupancyPool(kept, kOriginBox);
    EXPECT_LE(packed.pool.size(), w0 + w1) << "the pack exceeded the capacity it was fitted to";
    for (const auto& [origin, blob] : kept) {
        // The microcube each of these chunks carries at local cube (0,0,0), micro (0,0,0).
        EXPECT_TRUE(packedPoolSolidAt(packed, origin * 9))
            << "a kept chunk lost its geometry — it was truncated, not kept whole";
        // And a cell that should be empty must still read empty, not as leftover words.
        EXPECT_FALSE(packedPoolSolidAt(packed, origin * 9 + glm::ivec3{4, 4, 4}));
    }

    // Zero capacity drops everything rather than emitting a partial blob.
    size_t droppedAll = 0;
    EXPECT_TRUE(selectChunksThatFit(chunks, 0, droppedAll).empty());
    EXPECT_EQ(droppedAll, 3u);

    // Ample capacity drops nothing — the positive control. Without it, a selector that always
    // returned {} would pass every assertion above.
    size_t droppedNone = 1234;
    EXPECT_EQ(selectChunksThatFit(chunks, 1u << 24, droppedNone).size(), 3u);
    EXPECT_EQ(droppedNone, 0u);
}

TEST(VoxelLightOccupancy, MovingTheBoxForgetsChunksItNoLongerCovers) {
    // No initialize(): setChunk/removeChunk/setViewCentre touch no Vulkan, and flushIfDirty
    // early-returns without a mapped pool. This tests the bookkeeping, not the upload.
    Phyxel::Graphics::VoxelLightOccupancyGpu occ;

    VoxelOccupancyGrid g;
    addSolidCube(g, {1, 1, 1});
    const auto blob = buildLightOccupancy(g);

    occ.setViewCentre(glm::vec3(16.0f, 16.0f, 16.0f));      // box around the origin
    EXPECT_TRUE(occ.setChunk({0, 0, 0}, blob));
    EXPECT_TRUE(occ.setChunk({64, 0, 0}, blob));
    EXPECT_EQ(occ.residentChunks(), 2u);

    // A chunk far outside the box is refused outright — not stored to be silently unaddressable.
    EXPECT_FALSE(occ.setChunk({100000, 0, 0}, blob));
    EXPECT_EQ(occ.residentChunks(), 2u);

    // Move the viewer far away: both chunks fall outside and must be forgotten, or they would keep
    // occupying pool space the shader can no longer address.
    occ.setViewCentre(glm::vec3(100000.0f, 16.0f, 16.0f));
    EXPECT_EQ(occ.residentChunks(), 0u);

    // Coming back does NOT resurrect them — residency is re-offered by the caller's scan. Anything
    // else would serve geometry from before whatever happened while the viewer was away.
    occ.setViewCentre(glm::vec3(16.0f, 16.0f, 16.0f));
    EXPECT_EQ(occ.residentChunks(), 0u);
    EXPECT_TRUE(occ.setChunk({0, 0, 0}, blob));
    EXPECT_EQ(occ.residentChunks(), 1u);

    occ.removeChunk({0, 0, 0});
    EXPECT_EQ(occ.residentChunks(), 0u);
    occ.removeChunk({0, 0, 0});                              // idempotent, not a crash
    EXPECT_EQ(occ.residentChunks(), 0u);
}

// ---------------------------------------------------------------------------------------------
// M2 — LIGHT VISIBILITY. The term whose absence made a lantern inside a sealed building light the
// ground outside it. Tested here rather than argued from screenshots: a screen A/B on a real world
// mixes wall shadow with terrain relief, and cannot separate them.
// ---------------------------------------------------------------------------------------------
using Phyxel::Graphics::packedPoolLightVisibility;

namespace {
/// One chunk holding a solid wall plane at world cube x == wallX, spanning the given y/z ranges.
PackedOccupancyPool wallPool(int wallX, int y0, int y1, int z0, int z1) {
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int y = y0; y <= y1; ++y)
    for (int z = z0; z <= z1; ++z) addSolidCube(g, {wallX, y, z});
    return packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);
}
}  // namespace

TEST(VoxelLightOccupancy, AWallBlocksLightAndOpenAirDoesNot) {
    // Wall occupying world cube x = 10, i.e. the slab 10.0 <= x < 11.0.
    const auto pool = wallPool(10, 4, 12, 4, 12);
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 light{5.5f, 8.0f, 8.5f};      // light on the -x side of the wall

    // Same side as the light, nothing between: VISIBLE. This is the control — without it, a
    // march that always returned "blocked" would pass the blocking assertion below.
    const auto near = packedPoolLightVisibility(pool, {7.5f, 6.0f, 8.5f}, up, light);
    EXPECT_TRUE(near.visible) << "open air between surface and light must not block";

    // Opposite side of the wall: BLOCKED, and the first hit must be inside the wall slab.
    const auto far = packedPoolLightVisibility(pool, {14.5f, 6.0f, 8.5f}, up, light);
    EXPECT_FALSE(far.visible) << "a solid wall between surface and light must block it";
    EXPECT_EQ(far.firstHitMicro.x / 9, 10)
        << "blocked, but not by the wall — first hit at micro x=" << far.firstHitMicro.x;
}

TEST(VoxelLightOccupancy, TheThinnestPossibleVoxelStillOccludes) {
    // FOUND BY THE GROUNDED WALL RIG, 2026-08-30: a 1-micro slab is exactly 1/9 thick, and the
    // march originally stepped 1/9 — so samples landed either side of it and it occluded NOTHING.
    // A sealed room under a 1-micro roof read 0.536 sky instead of 0. The sampling interval must be
    // finer than the thinnest thing the world can contain; hence the half-micro step.
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    // A single-micro-thick horizontal sheet at the BOTTOM of cube y = 10, spanning x/z.
    for (int x = 4; x <= 16; ++x)
    for (int z = 4; z <= 16; ++z)
    for (int mx = 0; mx < 9; ++mx)
    for (int mz = 0; mz < 9; ++mz)
        addMicrocube(g, {x, 10, z}, {mx / 3, 0, mz / 3}, {mx % 3, 0, mz % 3});
    const auto pool = packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);

    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    // Straight up through the sheet — the worst case for a stepped march, because the ray crosses
    // the thinnest possible extent perpendicular.
    const auto v = packedPoolLightVisibility(pool, {10.5f, 8.0f, 10.5f}, up, {10.5f, 14.0f, 10.5f});
    EXPECT_FALSE(v.visible) << "a 1-micro sheet did not occlude — the march stepped over the "
                               "finest geometry the engine can represent";

    // Sky, same sheet, same reason.
    EXPECT_EQ(packedPoolSkyVisibility(pool, {10.5f, 8.0f, 10.5f}, up), 0.0f)
        << "a 1-micro roof did not block the sky";

    // Control: beside the sheet, both must be clear — otherwise this passes for the wrong reason.
    EXPECT_TRUE(packedPoolLightVisibility(pool, {30.5f, 8.0f, 30.5f}, up,
                                          {30.5f, 14.0f, 30.5f}).visible);
    EXPECT_GT(packedPoolSkyVisibility(pool, {30.5f, 8.0f, 30.5f}, up), 0.99f);
}

TEST(VoxelLightOccupancy, ASurfaceDoesNotShadowItself) {
    // The failure this guards is total: if the start offset does not clear the surface's own cell,
    // EVERY lit face reads black and the feature looks like "lights stopped working".
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int x = 2; x < 20; ++x)
    for (int z = 2; z < 20; ++z) addSolidCube(g, {x, 5, z});      // floor slab, top at y = 6.0
    const auto pool = packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);

    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 light{10.5f, 9.0f, 10.5f};                    // 3 u above the floor

    // Directly beneath the light, and at a grazing distance across the same flat floor.
    EXPECT_TRUE(packedPoolLightVisibility(pool, {10.5f, 6.0f, 10.5f}, up, light).visible);
    EXPECT_TRUE(packedPoolLightVisibility(pool, {3.5f, 6.0f, 3.5f}, up, light).visible)
        << "a flat floor shadowed itself at a grazing angle — the classic acne failure";
}

TEST(VoxelLightOccupancy, SubVoxelGeometryOccludesLikeSolidGeometry) {
    // The whole point of sub-voxel occupancy: a 2-micro wall is 2/9 of a voxel thick and would be
    // INVISIBLE to any cube-resolution occlusion test, yet it is what the generator actually
    // builds. It must block light.
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int y = 4; y <= 10; ++y)
    for (int z = 4; z <= 10; ++z)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 9; ++mz)
    for (int mx = 0; mx < 2; ++mx)          // 2 micro cells thick, at the cell's -x edge
        addMicrocube(g, {10, y, z}, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});
    const auto pool = packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);

    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 light{5.5f, 7.0f, 7.5f};

    // Straight through the thin wall: blocked.
    const auto through = packedPoolLightVisibility(pool, {14.5f, 7.0f, 7.5f}, up, light);
    EXPECT_FALSE(through.visible) << "a 2-micro wall must occlude — this is why M1 stores sub-voxel "
                                     "occupancy rather than one bit per cube";

    // Control: past the END of the wall, the same distance out, must be visible. Without this a
    // march that blocked on everything would look like a pass.
    const auto around = packedPoolLightVisibility(pool, {14.5f, 7.0f, 20.5f}, up, {5.5f, 7.0f, 20.5f});
    EXPECT_TRUE(around.visible) << "open air beside the wall must not block";
}

TEST(VoxelLightOccupancy, ADistantLightStillGetsOccludedRatherThanRunningOutOfSteps) {
    // The bug this pins, found live: with a fixed 1/9 step and a fixed cap, a march that ran out
    // of steps returned "visible". A radius-22 light therefore lit every surface beyond ~10.7 u
    // straight THROUGH walls — the exact defect M2 exists to remove, reintroduced at range.
    const auto pool = wallPool(10, 0, 30, 0, 30);
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    // Surface 30 u from its light, wall squarely between them.
    const auto far = packedPoolLightVisibility(pool, {14.5f, 6.0f, 8.5f}, up, {-16.0f, 6.0f, 8.5f});
    EXPECT_FALSE(far.visible) << "a distant light shone through a wall — the traversal stopped short";
    EXPECT_EQ(far.firstHitMicro.x / 9, 10) << "blocked by something other than the wall";
    // Under the DDA the wall is reached long before the cell budget runs out, so this ray is NOT
    // capped. (The old fixed-step march reported capped here because it coarsened at range — the
    // very behaviour that made 1-micro geometry invisible.)
    EXPECT_FALSE(far.cappedOut) << "the wall is well inside the cell budget; nothing should cap";

    // Control at the same range with no wall between: still visible, i.e. the fix did not simply
    // make everything distant read as blocked.
    const auto clear = packedPoolLightVisibility(pool, {14.5f, 6.0f, 8.5f}, up, {44.0f, 6.0f, 8.5f});
    EXPECT_TRUE(clear.visible) << "open air at range must stay lit";

    // A nearby light crosses few cells and must never cap.
    const auto near = packedPoolLightVisibility(pool, {14.5f, 6.0f, 8.5f}, up, {13.0f, 8.0f, 8.5f});
    EXPECT_FALSE(near.cappedOut) << "a nearby light exhausted the cell budget";
    EXPECT_GT(near.steps, 0) << "the traversal visited no cells at all";
}

TEST(VoxelLightOccupancy, AFlameInACavityIsBlockedByTheMasonryAroundIt) {
    // Found live, in the engine's OWN generated hall_house: the hearth's firelight was landing on
    // the lawn outside the building. Every sealed-box gate passed, because they all place the light
    // in open interior air — none of them puts it in a cavity with masonry a fraction of a voxel
    // away, which is exactly what a firebox is.
    //
    // The mechanism was the self-occlusion guard. An emissive voxel is solid with its light at the
    // cell centre, so it occludes its own light; the guard stopped the shadow ray a flat HALF VOXEL
    // short of the light. That blind spot is thicker than the wall it needs to see, so the wall was
    // never tested at all. The rig below reproduces that geometry to scale.
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int y = 4; y <= 8; ++y)
    for (int z = 6; z <= 10; ++z)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 9; ++mz)
    for (int mx = 0; mx < 3; ++mx)        // 3 micro (1/3 voxel) of masonry at the cell's -x edge
        addMicrocube(g, {10, y, z}, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});
    const auto pool = packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);

    // The flame sits in air in the cavity, 0.389 u from the wall's outer face — INSIDE the old
    // half-voxel blind spot, which is the whole point of the rig.
    const glm::vec3 flame{10.0f + 3.5f / 9.0f, 6.5f, 8.5f};
    const glm::vec3 toLight{1.0f, 0.0f, 0.0f};

    EXPECT_FALSE(packedPoolLightVisibility(pool, {7.0f, 6.5f, 8.5f}, toLight, flame).visible)
        << "firelight reached the lawn through 3 micro of masonry — the self-occlusion guard is "
           "blanking more than the emitter itself";

    // Control, and the behaviour that must be PRESERVED: the room side of the same hearth stays
    // lit. A fix that simply blocks the hearth everywhere would pass the assertion above.
    EXPECT_TRUE(packedPoolLightVisibility(pool, {14.0f, 6.5f, 8.5f}, {-1.0f, 0.0f, 0.0f},
                                          flame).visible)
        << "the hearth stopped lighting its own room";
}

TEST(VoxelLightOccupancy, AnEmissiveVoxelStillLightsThroughItsOwnBody) {
    // The other half of the same guard, and the reason it exists at all (U3.2/U3.3): an emissive
    // voxel IS a light, and it IS solid, with the light at its centre. Without excluding the
    // emitter's own body every such light self-occludes completely — measured live as a glow block
    // in a night meadow lighting nothing, with the blades around it rendered as silhouettes.
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    addSolidCube(g, {20, 6, 8});
    const auto pool = packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);

    const glm::vec3 glow{20.5f, 6.5f, 8.5f};   // dead centre of the solid cube
    EXPECT_TRUE(packedPoolLightVisibility(pool, {24.0f, 6.5f, 8.5f}, {-1.0f, 0.0f, 0.0f},
                                          glow).visible)
        << "a glow block occluded its own light";
    EXPECT_TRUE(packedPoolLightVisibility(pool, {20.5f, 10.0f, 8.5f}, {0.0f, -1.0f, 0.0f},
                                          glow).visible)
        << "a glow block occluded its own light from above";
}

// ---------------------------------------------------------------------------------------------
// M3 — SKY AS AN EMITTER. The replacement for the deleted per-cell skylight flood.
//
// The flood's defining failure was that it was not light transport: it decayed 1 per CUBE CELL from
// the nearest opening, so a room read 14,13,12,11,10,9,8,7 along a row from one doorway — 47% of
// full daylight eight cells in — and a sealed room could still be bright if the flood leaked. These
// tests assert the ORDERING that any real visibility term must produce, and that a sealed room is
// actually zero rather than merely dim.
// ---------------------------------------------------------------------------------------------
using Phyxel::Graphics::packedPoolSkyVisibility;

namespace {
/// A hollow box of solid cubes: shell over [lo,hi] inclusive, interior hollow.
/// `windowFace` > 0 removes one wall cube at the middle of the +x face (a window).
VoxelOccupancyGrid hollowBox(const glm::ivec3& lo, const glm::ivec3& hi, int windowSize) {
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int x = lo.x; x <= hi.x; ++x)
    for (int y = lo.y; y <= hi.y; ++y)
    for (int z = lo.z; z <= hi.z; ++z) {
        const bool shell = (x == lo.x || x == hi.x || y == lo.y || y == hi.y ||
                            z == lo.z || z == hi.z);
        if (shell) addSolidCube(g, {x, y, z});
    }
    // Punch a DOORWAY out of the +x wall: it reaches the floor, so a floor-level probe can
    // actually see through it. (A high window cannot be seen from the floor by a hemisphere of
    // rays tilted at most 60 degrees off vertical — my first version of this test asserted it
    // could, and was simply wrong about the geometry.)
    const int cz = (lo.z + hi.z) / 2;
    for (int y = lo.y + 1; y < lo.y + 1 + windowSize; ++y)
    for (int z = cz; z < cz + windowSize; ++z)
        g.setCube({hi.x, y, z}, false);
    return g;
}
PackedOccupancyPool poolOf(const VoxelOccupancyGrid& g) {
    return packOccupancyPool({{glm::ivec3{0, 0, 0}, buildLightOccupancy(g)}}, kOriginBox);
}
}  // namespace

TEST(VoxelLightOccupancy, SkyVisibilityIsZeroSealedOneOpenAndBetweenNearAWindow) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    // 1. OPEN GROUND — a bare floor slab with nothing above it. Must read fully sky-lit. This is
    //    the positive control: without it, a function returning 0 everywhere would "pass" the
    //    sealed-room assertion.
    VoxelOccupancyGrid open;
    open.setChunkOrigin({0, 0, 0});
    for (int x = 2; x < 26; ++x)
    for (int z = 2; z < 26; ++z) addSolidCube(open, {x, 5, z});
    const float vOpen = packedPoolSkyVisibility(poolOf(open), {12.5f, 6.0f, 12.5f}, up);
    EXPECT_GT(vOpen, 0.99f) << "open ground must see the whole sky";

    // 2. SEALED ROOM — interior floor of a closed box. Must read ZERO, not merely dim. The old
    //    flood could not do this; that is the entire point of the rebuild.
    const auto sealed = hollowBox({4, 4, 4}, {14, 12, 14}, 0);
    const float vSealed = packedPoolSkyVisibility(poolOf(sealed), {9.5f, 5.0f, 9.5f}, up);
    EXPECT_EQ(vSealed, 0.0f) << "a sealed room must be BLACK, not dim (flood behaviour)";

    // 3. WINDOW ROOM — identical box with a 3x3 window. Must be > sealed and < open: some sky
    //    reaches the floor, but not all of it.
    const auto windowed = hollowBox({4, 4, 4}, {14, 12, 14}, 3);
    const auto wPool = poolOf(windowed);
    const float vNearWindow = packedPoolSkyVisibility(wPool, {13.0f, 5.0f, 9.5f}, up);
    const float vFarCorner  = packedPoolSkyVisibility(wPool, {5.5f,  5.0f, 5.5f}, up);
    std::cout << "\n  sky visibility: open " << vOpen << "  doorway-adjacent " << vNearWindow
              << "  far corner " << vFarCorner << "  sealed " << vSealed << "\n\n";

    EXPECT_GT(vNearWindow, 0.0f)   << "an opening must admit some sky";
    EXPECT_LT(vNearWindow, vOpen)  << "an opening must not admit as much sky as open ground";
    EXPECT_GT(vNearWindow, vFarCorner)
        << "sky must fall off with distance from the opening (near " << vNearWindow
        << " vs far corner " << vFarCorner << ")";

    // The ORDERING the plan names as the M3 gate.
    EXPECT_LT(vSealed, vNearWindow);
    EXPECT_LT(vNearWindow, vOpen);
}

TEST(VoxelLightOccupancy, SkyVisibilityIsBlockedBySubVoxelCeilingsNotJustFullCubes) {
    // A 2-micro ceiling is 2/9 of a voxel thick — invisible to any cube-resolution test, and
    // exactly what the generator builds. If it does not block sky, generated interiors light as
    // though they had no roof.
    VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (int x = 6; x <= 14; ++x)
    for (int z = 6; z <= 14; ++z) {
        addSolidCube(g, {x, 5, z});                       // floor
        for (int mx = 0; mx < 9; ++mx)                    // 2-micro ceiling at cube y = 9
        for (int mz = 0; mz < 9; ++mz)
        for (int my = 0; my < 2; ++my)
            addMicrocube(g, {x, 9, z}, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});
    }
    // Walls, so sky cannot arrive from the side and mask a leaking ceiling.
    for (int y = 6; y <= 9; ++y)
    for (int t = 6; t <= 14; ++t) {
        addSolidCube(g, {6, y, t});  addSolidCube(g, {14, y, t});
        addSolidCube(g, {t, y, 6});  addSolidCube(g, {t, y, 14});
    }
    const auto pool = poolOf(g);
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    EXPECT_EQ(packedPoolSkyVisibility(pool, {10.5f, 6.0f, 10.5f}, up), 0.0f)
        << "a 2-micro ceiling let the sky through — sub-voxel roofs would not darken interiors";

    // Control: the same floor with the ceiling cells removed must be sky-lit, proving the zero
    // above came from the ceiling and not from the walls or the reach limit.
    VoxelOccupancyGrid noRoof;
    noRoof.setChunkOrigin({0, 0, 0});
    for (int x = 6; x <= 14; ++x)
    for (int z = 6; z <= 14; ++z) addSolidCube(noRoof, {x, 5, z});
    EXPECT_GT(packedPoolSkyVisibility(poolOf(noRoof), {10.5f, 6.0f, 10.5f}, up), 0.99f);
}

// Chunk-quantised, so ordinary camera motion does not repack. A box that slid continuously would
// re-pack every resident chunk on every camera nudge.
TEST(VoxelLightOccupancy, BoxOnlyMovesInWholeChunkSteps) {
    const auto a = PackedOccupancyPool::boxMinChunkFor({100.0f, 20.0f, 100.0f});
    const auto b = PackedOccupancyPool::boxMinChunkFor({101.9f, 20.5f, 115.0f});   // same chunk
    EXPECT_EQ(a, b) << "moving within one chunk must not move the box";

    const auto c = PackedOccupancyPool::boxMinChunkFor({132.0f, 20.0f, 100.0f});   // next chunk
    EXPECT_EQ(c.x, a.x + 1);
    EXPECT_EQ(c.y, a.y);
    EXPECT_EQ(c.z, a.z);

    // Negative coordinates: floor, not truncate. -1 and 0 are different chunks.
    EXPECT_NE(PackedOccupancyPool::boxMinChunkFor({-1.0f, 0.0f, 0.0f}),
              PackedOccupancyPool::boxMinChunkFor({1.0f, 0.0f, 0.0f}));
}

// Storage is the reason detail is carried only for mixed cubes. Record it rather than assume it.
TEST(VoxelLightOccupancy, ReportStorageCost) {
    VoxelOccupancyGrid solidOnly, wallLike;
    for (int x = 0; x < 32; ++x)
    for (int y = 0; y < 8; ++y)
    for (int z = 0; z < 32; ++z) addSolidCube(solidOnly, {x, y, z});

    // A wall plane of 2-micro cells — the expensive shape for this layout.
    for (int x = 0; x < 32; ++x)
    for (int y = 0; y < 8; ++y)
    for (int mx = 0; mx < 9; ++mx)
    for (int my = 0; my < 9; ++my)
    for (int mz = 0; mz < 2; ++mz)
        addMicrocube(wallLike, {x, y, 4}, {mx / 3, my / 3, mz / 3}, {mx % 3, my % 3, mz % 3});

    const auto a = buildLightOccupancy(solidOnly);
    const auto b = buildLightOccupancy(wallLike);
    std::cout << "\n  storage per chunk:\n"
              << "    8192 solid cubes (terrain)      : " << a.bytes() << " B, "
              << a.mixedCubeIdx.size() << " mixed cubes\n"
              << "    256 two-micro wall cells        : " << b.bytes() << " B, "
              << b.mixedCubeIdx.size() << " mixed cubes\n"
              << "    (a dense micro bitfield would be " << (32 * 32 * 32 * 729 / 8) << " B)\n\n";
    EXPECT_EQ(a.mixedCubeIdx.size(), 0u) << "solid terrain must cost no detail storage at all";
    EXPECT_GT(b.mixedCubeIdx.size(), 0u);
}

// ---------------------------------------------------------------------------------------------
// The revision counter. RenderCoordinator::updateLightOccupancy re-flattens a chunk ONLY when this
// moves, so a mutation that forgets to bump it serves stale geometry to the light tracer — which
// looks like light passing through a wall, or a shadow cast by air. Every mutator is pinned here
// rather than trusted, because that failure is silent and its symptom points at the tracer.
// ---------------------------------------------------------------------------------------------
TEST(VoxelLightOccupancy, EveryGridMutationBumpsTheRevision) {
    VoxelOccupancyGrid g;
    uint32_t last = g.revision();
    auto moved = [&](const char* what) {
        const uint32_t now = g.revision();
        EXPECT_NE(now, last) << what << " did not bump revision(); the GPU mirror would keep "
                                        "serving the geometry from before this change";
        last = now;
    };

    g.setCube({1, 1, 1}, true);                              moved("setCube");
    g.markSubdivided({2, 2, 2}, true);                       moved("markSubdivided");
    g.setSubcube({2, 2, 2}, {0, 0, 0}, true);                moved("setSubcube");
    g.markSubcubeSubdivided({2, 2, 2}, {1, 1, 1}, true);     moved("markSubcubeSubdivided");
    g.setMicrocube({2, 2, 2}, {1, 1, 1}, {0, 0, 0}, true);   moved("setMicrocube");
    g.clear();                                               moved("clear");
}

// A pure read must NOT bump it — otherwise every frame's queries would look like a change and the
// pool would repack forever, turning an O(changed) mirror into an O(everything) one.
TEST(VoxelLightOccupancy, ReadsDoNotBumpTheRevision) {
    VoxelOccupancyGrid g;
    g.setCube({1, 1, 1}, true);
    g.markSubdivided({2, 2, 2}, true);
    g.setSubcube({2, 2, 2}, {0, 0, 0}, true);

    const uint32_t before = g.revision();
    (void)g.isCubeFilled({1, 1, 1});
    (void)g.isSubdivided({2, 2, 2});
    (void)g.isSubcubeFilled({2, 2, 2}, {0, 0, 0});
    (void)g.isSubcubeSubdivided({2, 2, 2}, {0, 0, 0});
    (void)g.isMicrocubeFilled({2, 2, 2}, {0, 0, 0}, {0, 0, 0});
    std::vector<Phyxel::Physics::OccupiedBox> boxes;
    g.queryAABB(glm::vec3(0.0f), glm::vec3(32.0f), boxes);

    EXPECT_EQ(g.revision(), before)
        << "a read changed revision(); the mirror would repack every frame";
}
