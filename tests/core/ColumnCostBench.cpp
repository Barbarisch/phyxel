#include <gtest/gtest.h>

#include "core/WorldGenerator.h"

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
// m1_meso_seam_and_column_cost_red.txt). DISABLED_ until M1b serves climate + the broad
// relief from the sheet; M1b's commit enables it (a DISABLED_ test is a parked red, never a
// skipped claim). Run it any time with --gtest_also_run_disabled_tests.
TEST(ColumnCostBench, DISABLED_FreshColumnCostsAtMost25Evals) {
    WorldGenerator perlin(WorldGenerator::GenerationType::Perlin, 4242u);
    (void)perlin.sampleSurface(0, 0);
    const double evals = measureEvalsPerColumn(perlin, 30000, -30000);
    EXPECT_LE(evals, 25.0) << "mean base-noise evals per fresh column = " << evals
                           << " (plan §9 gate: <= 25; the audit estimate was ~140)";
}
