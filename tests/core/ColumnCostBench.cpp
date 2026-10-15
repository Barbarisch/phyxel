#include <gtest/gtest.h>

#include <cstdio>

#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;

// ============================================================================
// ColumnCostBench — docs/WorldForgeV2Plan.md §9 / M1 resolution 3.
// The plan's "~140 noise evals per column" was an audit ESTIMATE; this bench is the first
// real count. One eval = one base 3-D noise call (an fbm of N octaves counts N), counted on
// this thread by WorldGenerator::noiseEvalsThisThread(). M1's gate is <= 25 per column once
// the macro/meso sheet serves climate + the broad relief; today's analytic Layer-0 is RED.
// ============================================================================
namespace {
constexpr int kColumns = 64;   // distinct, never-sampled columns (the column cache must not help)

double measureEvalsPerColumn(WorldGenerator& gen, int x0, int z0) {
    const uint64_t before = WorldGenerator::noiseEvalsThisThread();
    for (int i = 0; i < kColumns; ++i) (void)gen.sampleSurface(x0 + i * 7, z0 + i * 13);
    const uint64_t after = WorldGenerator::noiseEvalsThisThread();
    return static_cast<double>(after - before) / kColumns;
}
}  // namespace

// Control: the instrument is live (nothing sampled = no evals; a column = more than zero), and a
// CHARACTERIZATION found by the first run: the public sampleSurface() path is UNCACHED. The
// column cache (WorldGenerator.cpp ~662) memoizes whole 32x32 chunk blocks for generateChunk
// only, so every streaming-band query (ChunkManager.cpp ~260) and every
// terrain_height?source=generator call re-evaluates the full column. Re-sampling the same 64
// columns measured 136.69 == 136.69 (m1_meso_seam_and_column_cost_red.txt). Pinned so a later
// cache shows up as a deliberate change here, not a silent one.
TEST(ColumnCostBench, InstrumentCountsAndPublicSamplerIsUncached) {
    WorldGenerator perlin(WorldGenerator::GenerationType::Perlin, 4242u);
    (void)perlin.sampleSurface(0, 0);   // one-time bakes (hydrology memo, plan) outside the measurement
    const uint64_t idle0 = WorldGenerator::noiseEvalsThisThread();
    const uint64_t idle1 = WorldGenerator::noiseEvalsThisThread();
    EXPECT_EQ(idle0, idle1) << "the counter moved with nothing sampled";
    const double fresh = measureEvalsPerColumn(perlin, 20000, 20000);
    const double again = measureEvalsPerColumn(perlin, 20000, 20000);
    EXPECT_GT(fresh, 0.0) << "the counter did not move: the instrument is not wired";
    EXPECT_DOUBLE_EQ(again, fresh) << "sampleSurface() gained a cache (or lost determinism): "
                                   << again << " vs " << fresh << "; update this pin deliberately";
}

// The gate. Prints the measured mean so the red run IS the first real number. MEASURED RED
// 2026-10-15: 136.625 evals per fresh column on the analytic Layer-0 (Perlin seed 4242;
// m1_meso_seam_and_column_cost_red.txt). M1b: a version-2 recipe serves climate, base and the
// broad relief from the sheet; the tile is warmed first so its one-time bake is not amortized
// into the column figure (the bake is the section 9 "meso tile" budget, recorded separately).
TEST(ColumnCostBench, FreshColumnCostsAtMost25EvalsOnTheSheet) {
    WorldGenerator perlin(WorldGenerator::GenerationType::Perlin, 4242u);
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = 4242u;
    perlin.applyRecipe(r);
    ASSERT_TRUE(perlin.sheetMode()) << "a version-2 recipe did not enable the sheet";
    (void)perlin.sampleSurface(30000, -30000);   // warms the tile the 64 columns live in
    const double evals = measureEvalsPerColumn(perlin, 30000, -30000);
    std::printf("[ M1 section 9 ] sheet column cost: %.3f base-noise evals per fresh column (gate <= 25)\n", evals);
    EXPECT_LE(evals, 25.0) << "mean base-noise evals per fresh column = " << evals
                           << " (plan section 9 gate: <= 25; v1 measured 136.6)";
}

// D10: the version-1 path is frozen. Its cost is the characterization pinned here; a change
// means the legacy terrain changed, which is exactly what D10 forbids.
TEST(ColumnCostBench, LegacyColumnCostIsFrozen) {
    WorldGenerator perlin(WorldGenerator::GenerationType::Perlin, 4242u);
    (void)perlin.sampleSurface(0, 0);
    const double evals = measureEvalsPerColumn(perlin, 30000, -30000);
    EXPECT_NEAR(evals, 136.6, 1.5) << "the v1 column cost moved: " << evals;
}
