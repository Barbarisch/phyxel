// VoxFoundStorageLabTest — docs/VoxelFoundationPlan.md §3 + §8 storage lab (F0 skeleton).
//
// Areas: the engine's terrain generator (WorldGenerator Perlin / Mountains, fixed seeds) sampled at 1/8 m by
// buildTerrainArea — engine-generated, not hand-placed; flora and structures are not in it yet (they are on
// thirds today and arrive with the content conversion, FC). Each area is one chunk footprint (32 x 32 m).
//
// Green now: the area is built as specified, and its block statistics are MEASURED (the §3.5a memory
// estimate was a guess; this replaces it with data for bare terrain). Prediction written in §3.5a before
// measuring: ~1-3 detailed blocks per block column on terrain.
// Red now (until F1): the BlockStore stub equals the dense reference, collapses uniform blocks like the
// reference says it should, and stays within the memory estimate.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "core/WorldGenerator.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;

namespace {

// An area covering the terrain surface of the 32 x 32 m footprint at (x0, z0), 8 m below the lowest ground to
// 8 m above the highest.
VoxelArea surfaceArea(WorldGenerator& gen, int x0, int z0) {
    float lo = std::numeric_limits<float>::max(), hi = -lo;
    for (int x = x0 - 1; x <= x0 + 32; ++x)
        for (int z = z0 - 1; z <= z0 + 32; ++z) {
            const float h = gen.sampleSurface(x, z).bedHeight;
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    const int y0 = int(std::floor(lo)) - 8;
    const int y1 = int(std::ceil(hi)) + 8;
    return buildTerrainArea(gen, {x0, y0, z0}, {32, y1 - y0, 32});
}

struct Named {
    const char* name;
    WorldGenerator::GenerationType type;
    uint32_t seed;
    int x, z;
};
const Named kAreas[] = {
    {"perlin s7 (0,0)", WorldGenerator::GenerationType::Perlin, 7u, 0, 0},
    {"perlin s7 (512,-256)", WorldGenerator::GenerationType::Perlin, 7u, 512, -256},
    {"perlin s12345 (-1024,768)", WorldGenerator::GenerationType::Perlin, 12345u, -1024, 768},
    {"mountains s7 (0,0)", WorldGenerator::GenerationType::Mountains, 7u, 0, 0},
    {"mountains s7 (2048,1024)", WorldGenerator::GenerationType::Mountains, 7u, 2048, 1024},
};

}  // namespace

TEST(VoxFoundStorageLab, TerrainAreaFollowsTheEngineGround) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    ASSERT_GT(a.grid.solidCount(), 0u);
    ASSERT_GE(a.palette.size(), 2u);
    // at every metre column centre the 1/8 m ground top is within half a metre (+ one voxel) of today's cube top
    const glm::ivec3 o = a.originVoxel / kVoxelsPerM;
    double worst = 0;
    for (int mx = 0; mx < 32; ++mx)
        for (int mz = 0; mz < 32; ++mz) {
            const auto c = gen.sampleSurface(o.x + mx, o.z + mz);
            const int vx = mx * 8 + 4, vz = mz * 8 + 4;
            int top = -1;
            for (int vy = 0; vy < a.grid.dim.y; ++vy)
                if (a.grid.mat[a.grid.idx({vx, vy, vz})] != 0) top = vy;
            ASSERT_GE(top, 0);
            const double ourTop = double(o.y) + double(top + 1) / kVoxelsPerM;
            const double todayTop = double(c.surfaceY + 1);
            worst = std::max(worst, std::abs(ourTop - todayTop));
        }
    std::printf("[storage lab] worst |1/8 m ground top - today's cube top| at column centres: %.3f m\n", worst);
    EXPECT_LE(worst, 0.5 + 1.0 / kVoxelsPerM + 1e-6);
}

TEST(VoxFoundStorageLab, MeasuredBlockStatisticsOnGeneratedTerrain) {
    for (const Named& n : kAreas) {
        WorldGenerator gen(n.type, n.seed);
        const VoxelArea a = surfaceArea(gen, n.x, n.z);
        const BlockStats s = blockStats(a.grid);
        const double perCol = double(s.detailed) / double(s.blockColumns);
        const double dense = double(a.grid.size());
        std::printf("[storage lab] %-26s %3d m tall: %6zu blocks, %5zu detailed (%.2f per block column), %6zu air, "
                    "%6zu solid; est %.2f MB vs dense %.2f MB (%.1fx); materials %zu\n",
                    n.name, a.grid.dim.y / kVoxelsPerM, s.blocks, s.detailed, perCol, s.uniformAir, s.uniformSolid,
                    s.estimatedBytes() / 1e6, dense / 1e6, dense / double(s.estimatedBytes()), a.palette.size() - 1);
        EXPECT_EQ(s.blocks, s.uniformAir + s.uniformSolid + s.detailed);
        EXPECT_GT(s.detailed, 0u);
    }
}

// The §3.5a prediction was "~1-3 detailed blocks per block column". First run (2026-11-10, this file):
// Perlin 0.86 / 0.98 / 1.06, Mountains 1.42 / 2.25 — the LOWER bound was wrong (bare terrain is cheaper than
// predicted); recorded in the plan. What the memory budget relies on is the UPPER bound, which held, so that is
// what stays asserted, on every area. Bare terrain only: flora, structures and water will raise it (FC/F1).
TEST(VoxFoundStorageLab, PredictionTerrainHasAtMostThreeDetailedBlocksPerColumn) {
    for (const Named& n : kAreas) {
        WorldGenerator gen(n.type, n.seed);
        const BlockStats s = blockStats(surfaceArea(gen, n.x, n.z).grid);
        const double perCol = double(s.detailed) / double(s.blockColumns);
        EXPECT_GT(perCol, 0.5) << n.name;
        EXPECT_LE(perCol, 3.0) << n.name;
    }
}

// ---- RED until F1 ----------------------------------------------------------------------------

TEST(VoxFoundStorageLab, BlockStoreEqualsTheDenseReference) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    BlockStore store(a.grid.dim);
    for (size_t i = 0; i < a.grid.size(); ++i) store.set(a.grid.pos(i), a.grid.mat[i]);
    std::string why;
    EXPECT_EQ(compareToReference(store, a.grid, &why), 0u) << why;
}

TEST(VoxFoundStorageLab, BlockStoreCollapsesUniformBlocks) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    const BlockStats s = blockStats(a.grid);
    BlockStore store(a.grid.dim);
    for (size_t i = 0; i < a.grid.size(); ++i) store.set(a.grid.pos(i), a.grid.mat[i]);
    EXPECT_EQ(store.uniformBlocks(), s.uniformAir + s.uniformSolid);
    EXPECT_EQ(store.detailedBlocks(), s.detailed);
}

TEST(VoxFoundStorageLab, BlockStoreMemoryWithinTheEstimate) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    const BlockStats s = blockStats(a.grid);
    BlockStore store(a.grid.dim);
    for (size_t i = 0; i < a.grid.size(); ++i) store.set(a.grid.pos(i), a.grid.mat[i]);
    EXPECT_GT(store.memoryBytes(), 0u);
    EXPECT_LE(double(store.memoryBytes()), 1.1 * double(s.estimatedBytes()));
}
