#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

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

// Wall-clock per fresh column and its per-phase split (M1e audit: measured, not inferred). Printed;
// Debug numbers are not the figure — run the Release test exe for the one the plan records.
// The first version sampled (30000, -30000): OUTSIDE the +-16384 hydrology box, no worldforge plan,
// microsecond timers that read 0 — it reported 4.4 us per column while the engine measured ~400 us.
// This one samples the gate pose (300, -1400) on the baseline recipe (seed 20260816, 8-site
// worldforge plan) so the channel, creek, road and bridge queries run, with nanosecond timers, and
// repeats on THREE threads over generator copies to expose the shared-pager lock contention the
// streaming workers see.
namespace {
WorldRecipe baselineRecipe(int version) {
    WorldRecipe r;
    r.version = version;
    r.type = "Perlin";
    r.seed = 20260816u;
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048;
    r.worldforge.minSpacing = 400;
    r.creekCarve = true;   // what makeRecipe() gives a NEW world (the engine's lab worlds have it)
    return r;
}

double phasesOf(const char* label, WorldGenerator& gen, int x0, int z0, int n, bool print) {
    uint64_t before[WorldGenerator::kColumnPhaseCount];
    for (int k = 0; k < WorldGenerator::kColumnPhaseCount; ++k) before[k] = WorldGenerator::columnPhaseNsThisThread(k);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) (void)gen.sampleSurface(x0 + (i % 32), z0 + (i / 32));
    const double totalUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    if (print) {
        std::printf("[ column phases ] %s: %.1f us per fresh column over %d columns\n", label, totalUs / n, n);
        for (int k = 0; k < WorldGenerator::kColumnPhaseCount; ++k) {
            const double us = static_cast<double>(WorldGenerator::columnPhaseNsThisThread(k) - before[k]) * 1e-3 / n;
            std::printf("    %-28s %8.1f us  (%4.1f %%)\n", WorldGenerator::kColumnPhaseNames[k], us, 100.0 * us * n / totalUs);
        }
    }
    return totalUs / n;
}
}  // namespace

TEST(ColumnCostBench, PhaseTimingPerFreshColumnAtTheGatePosePrinted) {
    WorldGenerator v1(WorldGenerator::GenerationType::Perlin, 20260816u);
    v1.applyRecipe(baselineRecipe(1));
    (void)v1.sampleSurface(300, -1400);
    phasesOf("v1 baseline, gate pose (300,-1400)", v1, 300, -1400, 1024, true);
    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 20260816u);
    v2.applyRecipe(baselineRecipe(2));
    ASSERT_TRUE(v2.sheetMode());
    (void)v2.sampleSurface(300, -1400);
    phasesOf("v2 sheet, gate pose (300,-1400)", v2, 300, -1400, 1024, true);
    // Three threads on generator COPIES (the streaming workers' arrangement), each its own fresh
    // 32x32 block, timed per thread: the v2 copies share one MesoPager mutex.
    for (int version : {1, 2}) {
        WorldGenerator base(WorldGenerator::GenerationType::Perlin, 20260816u);
        base.applyRecipe(baselineRecipe(version));
        (void)base.sampleSurface(300, -1400);
        double per[3] = {0, 0, 0};
        std::vector<std::thread> th;
        for (int t = 0; t < 3; ++t)
            th.emplace_back([&, t] {
                WorldGenerator copy = base;
                per[t] = phasesOf("", copy, 400 + 64 * t, -1300, 1024, false);
            });
        for (auto& x : th) x.join();
        std::printf("[ column phases ] v%d, 3 threads on copies: %.1f / %.1f / %.1f us per column per thread\n",
                    version, per[0], per[1], per[2]);
    }
    EXPECT_GT(WorldGenerator::columnPhaseNsThisThread(0), 0u);
}

// The engine's recorder attributes ~130 ms of column sampling per chunk while the single-block bench
// above costs ~2 ms: is it LOCATION (289 blocks around the pose, channel searches near rivers) or
// something systemic? Time every 32x32 block in the 17x17 streaming radius around the gate pose on
// the v2 sheet and print the per-block spread + the phase split of the total.
TEST(ColumnCostBench, PhaseTimingOverTheStreamingRadiusPrinted) {
    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 20260816u);
    v2.applyRecipe(baselineRecipe(2));
    ASSERT_TRUE(v2.sheetMode());
    (void)v2.sampleSurface(300, -1400);
    uint64_t before[WorldGenerator::kColumnPhaseCount];
    for (int k = 0; k < WorldGenerator::kColumnPhaseCount; ++k) before[k] = WorldGenerator::columnPhaseNsThisThread(k);
    const int cx0 = 300 / 32, cz0 = -1400 / 32;
    double minUs = 1e9, maxUs = 0.0, sumUs = 0.0; int maxCx = 0, maxCz = 0, blocks = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int dz = -8; dz <= 8; ++dz)
        for (int dx = -8; dx <= 8; ++dx) {
            const int bx = (cx0 + dx) * 32, bz = (cz0 + dz) * 32;
            const auto b0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 1024; ++i) (void)v2.sampleSurface(bx + (i % 32), bz + (i / 32));
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - b0).count() / 1024.0;
            minUs = std::min(minUs, us); sumUs += us; ++blocks;
            if (us > maxUs) { maxUs = us; maxCx = cx0 + dx; maxCz = cz0 + dz; }
        }
    const double totalUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[ column radius ] v2 sheet, %d blocks (17x17 chunk columns) around (300,-1400): per-column us min %.1f mean %.1f max %.1f (max at chunk column %d,%d); whole radius %.0f ms\n",
                blocks, minUs, sumUs / blocks, maxUs, maxCx, maxCz, totalUs * 1e-3);
    for (int k = 0; k < WorldGenerator::kColumnPhaseCount; ++k) {
        const double ms = static_cast<double>(WorldGenerator::columnPhaseNsThisThread(k) - before[k]) * 1e-6;
        std::printf("    %-28s %8.1f ms  (%4.1f %%)\n", WorldGenerator::kColumnPhaseNames[k], ms, 100.0 * ms * 1e3 / totalUs);
    }
    EXPECT_GT(blocks, 0);
}
