#include <gtest/gtest.h>
#include "core/WaterSimulation.h"
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <algorithm>

using Phyxel::Core::WaterSimulation;

namespace {

// Build a box with a solid floor at y=0 across the whole XZ extent.
void addFloor(WaterSimulation& sim) {
    for (int z = 0; z < sim.sizeZ(); ++z)
        for (int x = 0; x < sim.sizeX(); ++x)
            sim.setSolid(x, 0, z, true);
}

} // namespace

// Mass is conserved exactly across many ticks, regardless of how chaotic the flow is.
TEST(WaterSimulation, ConservesMass) {
    WaterSimulation sim(8, 8, 8);
    addFloor(sim);
    sim.addWater(1, 6, 1, 1.0f);
    sim.addWater(4, 7, 5, 1.0f);
    sim.addWater(6, 5, 2, 0.5f);
    sim.addWater(3, 7, 3, 2.0f); // overfull cell — must still conserve as it drains

    const float before = sim.totalMass();
    for (int i = 0; i < 300; ++i) sim.step();
    const float after = sim.totalMass();

    EXPECT_NEAR(after, before, before * 1e-4f + 1e-4f);
    EXPECT_GE(sim.minMass(), -1e-5f); // never goes negative
}

// shift() translates the field (new[p] = old[p+delta]) so world content stays put as the grid
// window moves; content staying in-window is conserved, content shifted past an edge is dropped.
// (WaterSystemV2 Phase A: the primitive WaterManager::recenter uses to follow the player.)
TEST(WaterSimulation, ShiftTranslatesFieldAndConservesInWindowMass) {
    WaterSimulation sim(8, 8, 8);
    sim.addWater(4, 4, 4, 1.0f);
    sim.setChannel(4, 4, 4, true);
    const float before = sim.totalMass();
    ASSERT_FLOAT_EQ(before, 1.0f);

    // Move the window +1 in x: the cell that was at local (4,4,4) is now at (3,4,4).
    sim.shift(glm::ivec3(1, 0, 0));
    EXPECT_FLOAT_EQ(sim.massAt(3, 4, 4), 1.0f) << "content did not translate by -delta";
    EXPECT_FLOAT_EQ(sim.massAt(4, 4, 4), 0.0f) << "old cell not vacated";
    EXPECT_TRUE(sim.isChannel(3, 4, 4)) << "channel tag did not travel with the cell";
    EXPECT_FLOAT_EQ(sim.totalMass(), before) << "mass not conserved while content stays in-window";

    // Shift everything out of the window → mass is dropped at the frontier.
    sim.shift(glm::ivec3(100, 0, 0));
    EXPECT_FLOAT_EQ(sim.totalMass(), 0.0f) << "content shifted past the edge should be dropped";
}

// Once the field reaches rest, step() does NO work (returns immediately) until a disturbance wakes
// it — the SLEEP perf property. sweepsRun() (steps that ran the sweep) freezes while settled and
// resumes after a disturbance. Also prints a rough active-vs-settled per-step cost on a 64^3 box so
// the perf win is concrete (Debug build — Release is ~an order of magnitude faster).
TEST(WaterSimulation, SettledFieldSkipsWork) {
    WaterSimulation sim(8, 8, 8);
    addFloor(sim);
    sim.addWater(3, 5, 3, 1.0f);
    int guard = 0;
    while (!sim.settled() && guard++ < 2000) sim.step();
    ASSERT_TRUE(sim.settled()) << "water never reached rest";

    const unsigned long long sweepsAtRest = sim.sweepsRun();
    const float massAtRest = sim.totalMass();
    for (int i = 0; i < 100; ++i) sim.step();                 // all should be skipped no-ops
    EXPECT_EQ(sim.sweepsRun(), sweepsAtRest) << "settled field kept sweeping — no perf win";
    EXPECT_FLOAT_EQ(sim.totalMass(), massAtRest) << "settled field changed while skipping";

    sim.addWater(3, 6, 3, 0.5f);                              // disturbance
    EXPECT_FALSE(sim.settled()) << "disturbance did not wake the field";
    sim.step();
    EXPECT_GT(sim.sweepsRun(), sweepsAtRest) << "woken field did not resume sweeping";

    // Concrete cost datapoint: a 64x32x64 box (131,072 cells) — active step vs settled (skipped) step.
    WaterSimulation big(64, 32, 64);
    for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) big.setSolid(x, 0, z, true);
    for (int z = 20; z < 44; ++z) for (int x = 20; x < 44; ++x) big.addWater(x, 6, z, 1.0f);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) big.step();                  // active sweeps
    auto t1 = std::chrono::steady_clock::now();
    int g = 0; while (!big.settled() && g++ < 4000) big.step();
    ASSERT_TRUE(big.settled());
    auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) big.step();                // settled: should be ~free
    auto t3 = std::chrono::steady_clock::now();
    const double activeUs  = std::chrono::duration<double, std::micro>(t1 - t0).count() / 20.0;
    const double settledUs = std::chrono::duration<double, std::micro>(t3 - t2).count() / 1000.0;
    std::printf("[water-perf] 64x32x64 box (Debug): active step %.1f us, settled step %.3f us (%.0fx cheaper)\n",
                activeUs, settledUs, settledUs > 0 ? activeUs / settledUs : 0.0);
}

// wake() forces the next step() to run even when the field was settled. This is the contract the GPU
// stepper depends on: WaterManager::stepGpu() writes the mass field DIRECTLY (bypassing the tracked
// mutators), so WaterManager::update() calls m_sim.wake() after every GPU step — otherwise switching
// back to the CPU stepper would trust a stale "settled" flag and freeze the field mid-flow. (The
// actual GPU→CPU switch needs a Vulkan device and is exercised by runtime/integration, not this
// CPU-only unit test; here we prove the wake() primitive the fix relies on.)
TEST(WaterSimulation, WakeForcesTheNextStepToRun) {
    WaterSimulation sim(8, 8, 8);
    addFloor(sim);
    sim.addWater(3, 5, 3, 1.0f);
    int g = 0; while (!sim.settled() && g++ < 2000) sim.step();
    ASSERT_TRUE(sim.settled());

    const unsigned long long sweeps = sim.sweepsRun();
    sim.step();
    EXPECT_EQ(sim.sweepsRun(), sweeps) << "settled field should have skipped this step";
    sim.wake();                                    // e.g. after a GPU step wrote the field directly
    sim.step();
    EXPECT_GT(sim.sweepsRun(), sweeps) << "wake() did not force the next step to run — GPU resume would freeze";
}

// ─── Per-column ACTIVE SET (WaterSystemV2 A2c part 2) ─────────────────────────────────────────────

// The active set is a pure optimization: stepping with it must produce EXACTLY the field a full
// sweep produces, step for step. The reference sim is forced to sweep every column each step by
// wake() (which marks all columns dirty); the subject sim runs normally. The scenario exercises
// every rule that moves mass — gravity off a shelf, horizontal leveling, pressure/compression in a
// deep pool, a pinned source (spring), channels, and evaporation — so a divergence anywhere in the
// dirty-propagation logic (a missed neighbor mark, a stale snapshot column) shows up as a per-cell
// mismatch within a few steps.
TEST(WaterSimulation, ActiveSetMatchesFullSweepExactly) {
    auto build = [](WaterSimulation& sim) {
        // Floor + a solid shelf at y=5 the water falls off; a walled deep pit for compression.
        for (int z = 0; z < sim.sizeZ(); ++z)
            for (int x = 0; x < sim.sizeX(); ++x) sim.setSolid(x, 0, z, true);
        for (int z = 2; z <= 6; ++z)
            for (int x = 2; x <= 6; ++x) sim.setSolid(x, 5, z, true);
        for (int y = 1; y <= 4; ++y) {  // pit walls around x,z in [10,12]
            for (int i = 9; i <= 13; ++i) {
                sim.setSolid(i, y, 9, true); sim.setSolid(i, y, 13, true);
                sim.setSolid(9, y, i, true); sim.setSolid(13, y, i, true);
            }
        }
        sim.addWater(4, 8, 4, 2.0f);        // overfull blob on the shelf — spills off the edge
        sim.addWater(11, 8, 11, 3.0f);      // fills the pit → compression → pressure path
        sim.setSource(14, 6, 2, 1.0f);      // spring keeps injecting the whole run
        sim.setChannel(2, 1, 10, true);     // channel cell (evaporation-exempt)
        sim.addWater(2, 1, 10, 0.05f);      // thin water ON the channel — must persist
        sim.addWater(2, 1, 12, 0.05f);      // thin water off-channel — must evaporate
        sim.setEvaporation(true);
    };
    WaterSimulation active(16, 12, 16), full(16, 12, 16);
    build(active); build(full);

    for (int step = 0; step < 80; ++step) {
        active.step();
        full.wake();   // force a full sweep — the reference result
        full.step();
        for (int z = 0; z < 16; ++z)
        for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x) {
            const float a = active.massAt(x, y, z), f = full.massAt(x, y, z);
            ASSERT_FLOAT_EQ(a, f) << "active-set result diverged from the full sweep at step "
                                  << step << " cell (" << x << "," << y << "," << z << ")";
        }
    }
}

// A local disturbance in a big settled field sweeps only its own neighborhood, not the whole box —
// the property that makes a PARTIALLY-active region affordable (global settle-skip only helps when
// EVERYTHING is at rest). The first sweep after a one-cell disturbance must touch exactly
// 5 columns (the cell's column + its 4 neighbors); as the water spreads inside a small walled basin
// the set may grow, but must stay far below the 4096-column box.
TEST(WaterSimulation, LocalDisturbanceSweepsOnlyNearbyColumns) {
    WaterSimulation sim(64, 16, 64);
    for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) sim.setSolid(x, 0, z, true);
    for (int y = 1; y <= 4; ++y)      // walled 7x7 basin centred on (32,32)
        for (int i = 28; i <= 36; ++i) {
            sim.setSolid(i, y, 28, true); sim.setSolid(i, y, 36, true);
            sim.setSolid(28, y, i, true); sim.setSolid(36, y, i, true);
        }
    // First sweep ever is FULL (everything starts dirty — nothing is known-settled yet).
    sim.step();
    EXPECT_EQ(sim.columnsProcessedLastSweep(), sim.columnCount());
    int guard = 0;
    while (!sim.settled() && guard++ < 100) sim.step();
    ASSERT_TRUE(sim.settled()) << "empty field should settle immediately";

    sim.addWater(32, 8, 32, 1.0f);    // one-cell disturbance inside the basin
    sim.step();
    EXPECT_EQ(sim.columnsProcessedLastSweep(), 5)
        << "first sweep after a 1-cell disturbance should touch exactly its column + 4 neighbors";

    int maxProcessed = 0;
    int partialSweeps = 0;
    guard = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (!sim.settled() && guard++ < 300) {
        sim.step();
        maxProcessed = std::max(maxProcessed, sim.columnsProcessedLastSweep());
        ++partialSweeps;
    }
    const auto t1 = std::chrono::steady_clock::now();
    ASSERT_TRUE(sim.settled()) << "the drop never re-settled";
    EXPECT_FLOAT_EQ(sim.totalMass(), 1.0f) << "active-set sweep lost mass";
    EXPECT_LE(maxProcessed, 300)
        << "a basin-contained drop swept far outside its neighborhood";
    EXPECT_LT(maxProcessed, sim.columnCount() / 4)
        << "local disturbance degenerated into (near-)full sweeps";
    // Concrete datapoint for the partially-active case the global settle-skip can't help with.
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() /
                      std::max(partialSweeps, 1);
    std::printf("[water-perf] 64x16x64 box (Debug): partially-active step %.1f us "
                "(max %d of %d columns swept)\n", us, maxProcessed, sim.columnCount());
}

// A fully-pinned ocean must reach REST: every transfer between two source-pinned cells is
// meaningless (both ends are re-clamped to their pinned mass at the next step's re-pin), but the
// compression rule wants deep cells at slightly OVER full — so pinned column stacks re-donated
// ~0.01 downward every step, forever, and the live ocean never settled (found by the live frame
// profiler: ~6ms water step at 20Hz over open sea, at rest, with total mass exactly constant).
// Pinned→unpinned transfers still run — that is how a breach floods.
TEST(WaterSimulation, FullyPinnedOceanSettlesAndStopsSweeping) {
    WaterSimulation sim(16, 16, 16);
    addFloor(sim);
    const int pinned = sim.fillWaterTable([](int, int) { return 6; });  // ocean up to y=6 everywhere
    ASSERT_GT(pinned, 1000) << "ocean should have flooded the open box";

    int guard = 0;
    while (!sim.settled() && guard++ < 60) sim.step();
    ASSERT_TRUE(sim.settled())
        << "a fully-pinned ocean never reached rest — pinned<->pinned compression oscillation";
    const unsigned long long sweeps = sim.sweepsRun();
    const float mass = sim.totalMass();
    for (int i = 0; i < 50; ++i) sim.step();
    EXPECT_EQ(sim.sweepsRun(), sweeps) << "settled pinned ocean kept sweeping";
    EXPECT_FLOAT_EQ(sim.totalMass(), mass);

    // A breach still floods: drain one cell and remove its pin — an empty UNPINNED gap in the
    // ocean. Its pinned neighbors must still flow into it (pinned→unpinned is real physics).
    sim.clearSource(8, 1, 8);
    sim.addWater(8, 1, 8, -1.0f);
    ASSERT_LT(sim.massAt(8, 1, 8), 1e-4f);
    sim.step();
    EXPECT_GT(sim.massAt(8, 1, 8), 0.1f)
        << "pinned neighbors no longer flow into an unpinned breach — the skip is too broad";
}

// Turning evaporation ON must wake a settled field: thin cells that settled while evaporation was
// off only became sink-eligible by the toggle, and the settle-skip would otherwise bypass them
// forever (they'd never dry). This was a real latent hole: setEvaporation() didn't clear the
// settled flag.
TEST(WaterSimulation, EvaporationToggleWakesSettledThinFilm) {
    WaterSimulation sim(8, 8, 8);
    addFloor(sim);
    sim.addWater(4, 1, 4, 0.05f);     // a thin film (< EVAP_THRESHOLD), evaporation OFF
    int guard = 0;
    while (!sim.settled() && guard++ < 2000) sim.step();
    ASSERT_TRUE(sim.settled());
    ASSERT_GT(sim.totalMass(), 0.04f) << "film should persist while evaporation is off";

    sim.setEvaporation(true);          // the toggle must wake the field...
    for (int i = 0; i < 40; ++i) sim.step();
    EXPECT_LT(sim.totalMass(), 1e-4f)
        << "settled thin film never evaporated after the toggle — setEvaporation didn't wake the field";
}

// ─── Baked WATER TABLE (WaterSystemV2 Phase C: generation feeds water) ────────────────────────────

// fillWaterTable floods a basin to ITS OWN per-column level (a lake above sea level) and nothing
// else: wet up to the level, dry above it, dry outside the wet columns. This is the generalization
// that lets baked mountain lakes fill without a global sea level reaching them.
TEST(WaterSimulation, FillWaterTableFloodsLakeToItsBakedLevelOnly) {
    WaterSimulation sim(16, 12, 16);
    for (int y = 0; y <= 6; ++y)       // basin walls ringing interior x,z in [5,7], well above level 5
        for (int i = 4; i <= 8; ++i) {
            sim.setSolid(i, y, 4, true); sim.setSolid(i, y, 8, true);
            sim.setSolid(4, y, i, true); sim.setSolid(8, y, i, true);
        }
    const int pinned = sim.fillWaterTable([](int lx, int lz) {
        return (lx >= 5 && lx <= 7 && lz >= 5 && lz <= 7) ? 5 : INT_MIN;  // 3x3 lake, level y=5
    });
    EXPECT_EQ(pinned, 3 * 3 * 6) << "lake should pin its 3x3 columns from y=0 up to level y=5";
    for (int i = 0; i < 4; ++i) sim.step();

    EXPECT_NEAR(sim.totalMass(), 54.0f, 1.0f) << "lake did not fill to its level";
    // Pinned sources breathe ~0.01 into the cell below between re-pins → threshold, not exact-1.0.
    EXPECT_GT(sim.massAt(6, 5, 6), 0.9f) << "surface cell at the baked level should be full";
    EXPECT_LT(sim.massAt(6, 6, 6), 1e-3f) << "water ABOVE the baked level";
    EXPECT_LT(sim.massAt(2, 0, 2), 1e-3f) << "water in a DRY column outside the lake";
}

// Fine-scale connectivity-gating still holds under the table: a sealed cavity below the lake
// (enclosed by solids, unreachable from the lake surface) stays dry even though its column is wet.
TEST(WaterSimulation, FillWaterTableLeavesSealedPocketDry) {
    WaterSimulation sim(16, 12, 16);
    for (int y = 0; y <= 6; ++y)
        for (int i = 4; i <= 8; ++i) {
            sim.setSolid(i, y, 4, true); sim.setSolid(i, y, 8, true);
            sim.setSolid(4, y, i, true); sim.setSolid(8, y, i, true);
        }
    // Seal the single cell (6,1,6): solids on all six faces (inside the lake's wet volume).
    sim.setSolid(5, 1, 6, true); sim.setSolid(7, 1, 6, true);
    sim.setSolid(6, 0, 6, true); sim.setSolid(6, 2, 6, true);
    sim.setSolid(6, 1, 5, true); sim.setSolid(6, 1, 7, true);
    sim.fillWaterTable([](int lx, int lz) {
        return (lx >= 5 && lx <= 7 && lz >= 5 && lz <= 7) ? 5 : INT_MIN;
    });
    for (int i = 0; i < 4; ++i) sim.step();

    EXPECT_GT(sim.massAt(5, 5, 5), 0.5f) << "the open lake volume should be wet";
    EXPECT_FLOAT_EQ(sim.massAt(6, 1, 6), 0.0f)
        << "sealed pocket under the lake flooded — connectivity-gating broke in fillWaterTable";
}

// With a UNIFORM level the table degenerates to the ocean boundary condition: on terrain with no
// sealed pockets, fillWaterTable(L everywhere) and fillOcean(edge seeds at/below L) must produce
// the exact same field. Guards the generalization against quietly changing ocean behavior.
TEST(WaterSimulation, FillWaterTableUniformLevelMatchesOceanBoundaryFill) {
    auto build = [](WaterSimulation& sim) {
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) sim.setSolid(x, 0, z, true);       // seabed
        for (int z = 5; z <= 9; ++z)                                        // a plateau island
            for (int x = 5; x <= 9; ++x)
                for (int y = 1; y <= 6; ++y) sim.setSolid(x, y, z, true);
        for (int y = 1; y <= 3; ++y) sim.setSolid(12, y, 12, true);         // a pillar stub
    };
    WaterSimulation table(16, 8, 16), ocean(16, 8, 16);
    build(table); build(ocean);
    const int L = 4;

    table.fillWaterTable([&](int, int) { return L; });
    std::vector<glm::ivec3> edgeSeeds;                    // fillOcean's boundary-condition seeding
    for (int y = 0; y <= L; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (!ocean.isSolid(x, y, 0))  edgeSeeds.push_back({x, y, 0});
            if (!ocean.isSolid(x, y, 15)) edgeSeeds.push_back({x, y, 15});
        }
        for (int z = 1; z < 15; ++z) {
            if (!ocean.isSolid(0, y, z))  edgeSeeds.push_back({0, y, z});
            if (!ocean.isSolid(15, y, z)) edgeSeeds.push_back({15, y, z});
        }
    }
    ocean.fillOcean(edgeSeeds, L);

    for (int i = 0; i < 3; ++i) { table.step(); ocean.step(); }
    for (int z = 0; z < 16; ++z)
    for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x)
        ASSERT_FLOAT_EQ(table.massAt(x, y, z), ocean.massAt(x, y, z))
            << "uniform table diverged from the ocean fill at (" << x << "," << y << "," << z << ")";
}

// Water released high in a column ends up resting on the floor, top empties.
TEST(WaterSimulation, FallsToFloor) {
    WaterSimulation sim(1, 10, 1);
    sim.setSolid(0, 0, 0, true);   // floor
    sim.addWater(0, 9, 0, 1.0f);   // a full cell at the top

    for (int i = 0; i < 100; ++i) sim.step();

    EXPECT_NEAR(sim.massAt(0, 1, 0), 1.0f, 0.05f); // settled just above the floor
    EXPECT_LT(sim.massAt(0, 9, 0), 0.01f);         // top drained
    EXPECT_NEAR(sim.totalMass(), 1.0f, 1e-3f);
}

// A pile of water spreads and levels to an even depth across a flat basin.
TEST(WaterSimulation, LevelsAcrossBasin) {
    WaterSimulation sim(5, 3, 1);
    addFloor(sim);
    sim.addWater(2, 2, 0, 4.0f); // 4 units over a 5-wide, 2-tall (above floor) basin

    for (int i = 0; i < 1500; ++i) sim.step();

    // 4 units across 5 columns settles to ~0.8 in the bottom (y=1) layer; y=2 drains.
    float lo = 1e9f, hi = -1e9f;
    for (int x = 0; x < 5; ++x) {
        float m = sim.massAt(x, 1, 0);
        lo = std::min(lo, m);
        hi = std::max(hi, m);
        EXPECT_LT(sim.massAt(x, 2, 0), 0.05f); // upper layer drained
        EXPECT_FLOAT_EQ(sim.massAt(x, 0, 0), 0.0f); // solid floor holds no water
    }
    EXPECT_NEAR((lo + hi) * 0.5f, 0.8f, 0.1f); // average depth ~0.8
    EXPECT_LT(hi - lo, 0.1f);                  // surface is level
    EXPECT_NEAR(sim.totalMass(), 4.0f, 1e-3f);
    EXPECT_GE(sim.minMass(), -1e-5f);
}

// Draining one crater into another conserves volume and equalizes levels. Two basins
// share a floor-level gap (communicating vessels); all water starts in the left basin.
// With evaporation off (the default), no mass is lost — it just redistributes until the
// two surfaces sit at the same level. This is the behaviour the default is tuned for.
TEST(WaterSimulation, DrainsBetweenCratersConservingVolume) {
    WaterSimulation sim(7, 5, 1);
    addFloor(sim);
    // Boundary walls and a divider at x=3 that is solid above the floor layer but open
    // at y=1, so the two basins (x=1,2 and x=4,5) connect only through the bottom gap.
    for (int y = 1; y < 5; ++y) {
        sim.setSolid(0, y, 0, true); // left wall
        sim.setSolid(6, y, 0, true); // right wall
        if (y >= 2) sim.setSolid(3, y, 0, true); // divider, open at y=1
    }
    sim.addWater(1, 1, 0, 3.0f); // dump it all into the left crater

    const float before = sim.totalMass();
    for (int i = 0; i < 3000; ++i) sim.step();

    // Volume is conserved exactly — nothing evaporated or leaked.
    EXPECT_NEAR(sim.totalMass(), before, before * 1e-4f + 1e-4f);
    EXPECT_GE(sim.minMass(), -1e-5f);

    // Water drained from the left crater into the right until the two equalized.
    const float leftCol  = sim.massAt(1, 1, 0) + sim.massAt(1, 2, 0);
    const float rightCol = sim.massAt(4, 1, 0) + sim.massAt(4, 2, 0);
    EXPECT_GT(rightCol, 0.3f);                  // the empty crater actually filled
    EXPECT_NEAR(leftCol, rightCol, 0.15f);      // levels equalized across the gap
}

// Pressure / upward flow: more than one cell's worth of water in a bottom cell
// stacks up the column (rises) instead of staying crushed in place. This is the
// mechanism that lets connected water reach a common level.
TEST(WaterSimulation, RisesUnderPressure) {
    WaterSimulation sim(1, 6, 1);
    sim.setSolid(0, 0, 0, true);    // floor
    sim.addWater(0, 1, 0, 3.0f);    // three cells' worth dumped into the bottom cell

    for (int i = 0; i < 400; ++i) sim.step();

    // It climbed into the cells above rather than remaining a single crushed cell.
    EXPECT_GT(sim.massAt(0, 1, 0), 0.9f);
    EXPECT_GT(sim.massAt(0, 2, 0), 0.9f);
    EXPECT_GT(sim.massAt(0, 3, 0), 0.5f);
    EXPECT_NEAR(sim.totalMass(), 3.0f, 1e-3f);
    EXPECT_GE(sim.minMass(), -1e-5f);
}

// Q1: a sealed pit not connected to water stays dry; breaching the wall floods it.
TEST(WaterSimulation, DisconnectedPitStaysDryThenFloodsAfterBreach) {
    WaterSimulation sim(7, 5, 1);
    addFloor(sim);
    sim.setSolid(0, 0, 0, true);
    for (int y = 1; y < 5; ++y) {
        sim.setSolid(0, y, 0, true); // left boundary
        sim.setSolid(3, y, 0, true); // divider wall: left "sea" | right "pit"
        sim.setSolid(6, y, 0, true); // right boundary
    }
    // A sea on the left, held full.
    sim.setSource(1, 1, 0, WaterSimulation::MAX_MASS);
    sim.setSource(2, 1, 0, WaterSimulation::MAX_MASS);

    for (int i = 0; i < 1500; ++i) sim.step();

    // Left sea is full; the walled-off right pit is bone dry (water cannot reach it).
    EXPECT_GT(sim.massAt(2, 1, 0), 0.9f);
    EXPECT_LT(sim.massAt(4, 1, 0), 0.01f);
    EXPECT_LT(sim.massAt(5, 1, 0), 0.01f);

    // Breach the bottom of the divider — now water can reach the pit.
    sim.setSolid(3, 1, 0, false);
    for (int i = 0; i < 1500; ++i) sim.step();

    EXPECT_GT(sim.massAt(4, 1, 0), 0.3f); // the pit floods through the breach
    EXPECT_GE(sim.minMass(), -1e-5f);
}

// With evaporation on, a thin spill on flat ground dries up (bounds free spread).
TEST(WaterSimulation, ThinSpillEvaporates) {
    WaterSimulation sim(15, 4, 15);
    addFloor(sim);
    sim.setEvaporation(true);
    sim.addWater(7, 1, 7, 3.0f); // a puddle on an open flat floor — nothing contains it

    for (int i = 0; i < 800; ++i) sim.step();

    // It spread thin and evaporated away rather than dispersing into an endless film.
    EXPECT_LT(sim.totalMass(), 1.0f);
    EXPECT_GE(sim.minMass(), -1e-5f);
}

// Evaporation spares deep water, so a contained pond persists.
TEST(WaterSimulation, DeepPondPersistsUnderEvaporation) {
    WaterSimulation sim(4, 5, 4);
    addFloor(sim);
    for (int y = 1; y < 5; ++y) // walls around a 2x2 interior basin
        for (int x = 0; x < 4; ++x)
            for (int z = 0; z < 4; ++z)
                if (x == 0 || x == 3 || z == 0 || z == 3) sim.setSolid(x, y, z, true);
    sim.setEvaporation(true);
    sim.addWater(1, 3, 1, 6.0f); // fills the 2x2 basin a couple cells deep

    const float before = sim.totalMass();
    for (int i = 0; i < 800; ++i) sim.step();

    // Deep cells (>= threshold) are untouched; only a thin top sliver could go.
    EXPECT_GT(sim.totalMass(), before - 0.6f);
    EXPECT_GT(sim.massAt(1, 1, 1), 0.9f); // floor of the pond stays full
}

// A spring (continuous source) on flat ground, bounded by evaporation, settles to a
// persistent puddle — it neither dries up nor grows without bound.
TEST(WaterSimulation, SpringReachesBoundedSteadyState) {
    WaterSimulation sim(15, 4, 15);
    addFloor(sim);
    sim.setEvaporation(true);
    sim.setSource(7, 1, 7, WaterSimulation::MAX_MASS); // the spring

    for (int i = 0; i < 400; ++i) sim.step();
    float t1 = sim.totalMass();
    for (int i = 0; i < 400; ++i) sim.step();
    float t2 = sim.totalMass();

    EXPECT_GT(t2, 0.5f);                    // a persistent puddle exists
    EXPECT_LT(std::abs(t2 - t1), 1.0f);     // steady, not growing unbounded
    // The spring cell is re-pinned full each step but flows out during it, so it reads
    // partial after a step — what matters is the spring area stays wet (continuously fed).
    EXPECT_GT(sim.massAt(7, 1, 7), 0.3f);
}

// Channel cells are exempt from evaporation: thin water on a channel persists while the
// same thin water off-channel evaporates away. (Authored riverbeds carry flow.)
TEST(WaterSimulation, ChannelCellsResistEvaporation) {
    WaterSimulation sim(7, 3, 7);
    addFloor(sim);
    sim.setEvaporation(true);
    // Two isolated walled cells (so flow can't move the water — isolates evaporation).
    auto isolate = [&](int x, int z) {
        sim.setSolid(x - 1, 1, z, true); sim.setSolid(x + 1, 1, z, true);
        sim.setSolid(x, 1, z - 1, true); sim.setSolid(x, 1, z + 1, true);
    };
    isolate(2, 2); isolate(5, 5);
    sim.setChannel(2, 1, 2, true);  // channel cell (where the water sits)
    sim.addWater(2, 1, 2, 0.06f);   // thin water on the channel...
    sim.addWater(5, 1, 5, 0.06f);   // ...and the same off-channel

    for (int i = 0; i < 200; ++i) sim.step();

    EXPECT_GT(sim.massAt(2, 1, 2), 0.04f);  // channel retained its water
    EXPECT_LT(sim.massAt(5, 1, 5), 0.01f);  // off-channel evaporated away
}

// Ocean seam: a seeded basin fills to sea level and HOLDS it when dug deeper
// (infinite reservoir), unlike a self-contained pond which would drop.
TEST(WaterSimulation, OceanHoldsSeaLevelWhenDug) {
    WaterSimulation sim(5, 6, 1);
    addFloor(sim);
    for (int y = 1; y < 6; ++y) { sim.setSolid(0, y, 0, true); sim.setSolid(4, y, 0, true); }
    const int seaY = 3;

    sim.fillOcean({glm::ivec3(2, 1, 0)}, seaY);
    for (int i = 0; i < 60; ++i) sim.step();
    EXPECT_GT(sim.massAt(1, 3, 0), 0.9f); // filled to sea level
    EXPECT_GT(sim.massAt(3, 3, 0), 0.9f);
    EXPECT_LT(sim.massAt(2, 4, 0), 0.1f); // nothing above sea level

    // Dig the seabed deeper and re-flood — the surface must stay at sea level.
    sim.setSolid(2, 0, 0, false);
    sim.fillOcean({glm::ivec3(2, 1, 0)}, seaY);
    for (int i = 0; i < 60; ++i) sim.step();
    EXPECT_GT(sim.massAt(2, 0, 0), 0.9f); // new depth filled by the reservoir
    EXPECT_GT(sim.massAt(1, 3, 0), 0.9f); // surface still at sea level (did NOT drop)
    EXPECT_LT(sim.massAt(2, 4, 0), 0.1f);
}

// Ocean connectivity: a sealed sub-sea pocket not reachable from the seed stays dry.
TEST(WaterSimulation, OceanConnectivityLeavesSealedPocketDry) {
    WaterSimulation sim(7, 5, 1);
    addFloor(sim);
    for (int y = 1; y < 5; ++y) {
        sim.setSolid(0, y, 0, true); // left boundary
        sim.setSolid(3, y, 0, true); // wall separating the open basin from the pocket
        sim.setSolid(6, y, 0, true); // right boundary
    }
    sim.setSolid(4, 4, 0, true); // lid: fully enclose the pocket (x=4,5 below)
    sim.setSolid(5, 4, 0, true);
    const int seaY = 3;

    sim.fillOcean({glm::ivec3(1, 1, 0)}, seaY); // seed only in the left open basin
    for (int i = 0; i < 60; ++i) sim.step();

    EXPECT_GT(sim.massAt(2, 3, 0), 0.9f);  // open basin is ocean, filled to sea level
    EXPECT_LT(sim.massAt(5, 1, 0), 0.05f); // sealed pocket bone dry
    EXPECT_LT(sim.massAt(4, 1, 0), 0.05f);
}

// Water never seeps into solid cells.
TEST(WaterSimulation, DoesNotLeakIntoSolids) {
    WaterSimulation sim(3, 4, 3);
    addFloor(sim);
    sim.setSolid(1, 1, 1, true); // a solid block sitting on the floor
    sim.addWater(1, 3, 1, 1.0f); // poured directly above it

    for (int i = 0; i < 200; ++i) sim.step();

    EXPECT_FLOAT_EQ(sim.massAt(1, 1, 1), 0.0f); // the solid block stays dry
    for (int x = 0; x < 3; ++x)
        for (int z = 0; z < 3; ++z)
            EXPECT_FLOAT_EQ(sim.massAt(x, 0, z), 0.0f); // floor stays dry
    EXPECT_NEAR(sim.totalMass(), 1.0f, 1e-3f);          // nothing lost into solids
}

// ── FLOW PROXY (WaterSystemV3 Phase 3) ────────────────────────────────────────────────────────
// The renderer needs to know WHICH WAY water is moving and how hard, so ripples can advect with the
// current instead of animating identically on a still pond and in a rapid. Rather than adding a
// momentum term to the CA, the flow proxy is derived from the transfers step() ALREADY performs.
// These tests pin the properties the shading depends on: it points the right way, it is ~zero on
// still water, it decays once flow stops, and it survives a region recenter.

// A one-way channel: water released at one end must report flow pointing DOWN the channel.
TEST(WaterSimulation, FlowProxyPointsDownAChannel) {
    WaterSimulation sim(12, 4, 3);
    addFloor(sim);
    // Walls along z=0 and z=2 leave a 1-wide channel at z=1, so flow can only run along +/-x.
    for (int x = 0; x < 12; ++x)
        for (int y = 1; y < 4; ++y) { sim.setSolid(x, y, 0, true); sim.setSolid(x, y, 2, true); }
    sim.setSolid(0, 1, 1, true);  // closed at the -x end, so water can only travel +x
    sim.setSolid(0, 2, 1, true);

    // Hold the head of the channel full: a sustained source drives a sustained current.
    sim.setSource(1, 1, 1, WaterSimulation::MAX_MASS);
    for (int i = 0; i < 80; ++i) sim.step();

    const glm::vec2 mid = sim.flowAt(5, 1, 1);
    EXPECT_GT(mid.x, 0.0f) << "flow should run +x, away from the source";
    EXPECT_NEAR(mid.y, 0.0f, 1e-4f) << "walls forbid any z component";
    // And it should point the SAME way further downstream, not oscillate.
    EXPECT_GT(sim.flowAt(7, 1, 1).x, 0.0f);
}

// Still water reports no flow — otherwise a calm lake would shimmer as if it were a river.
TEST(WaterSimulation, FlowProxyIsZeroOnSettledWater) {
    WaterSimulation sim(6, 5, 6);
    addFloor(sim);
    for (int z = 1; z < 5; ++z)
        for (int x = 1; x < 5; ++x) sim.addWater(x, 1, z, 1.0f);
    for (int i = 0; i < 200; ++i) sim.step();
    ASSERT_TRUE(sim.settled()) << "precondition: the basin must actually come to rest";

    for (int z = 1; z < 5; ++z)
        for (int x = 1; x < 5; ++x) {
            const glm::vec2 f = sim.flowAt(x, 1, z);
            EXPECT_LT(std::sqrt(f.x * f.x + f.y * f.y), 1e-3f)
                << "settled cell (" << x << ",1," << z << ") still reports flow";
        }
}

// Once the driving source is removed and the water levels out, the reported flow must FADE rather
// than latch — the EMA is what guarantees this.
TEST(WaterSimulation, FlowProxyDecaysAfterFlowStops) {
    WaterSimulation sim(12, 4, 3);
    addFloor(sim);
    for (int x = 0; x < 12; ++x)
        for (int y = 1; y < 4; ++y) { sim.setSolid(x, y, 0, true); sim.setSolid(x, y, 2, true); }
    sim.setSolid(0, 1, 1, true);
    sim.setSolid(0, 2, 1, true);

    sim.setSource(1, 1, 1, WaterSimulation::MAX_MASS);
    for (int i = 0; i < 80; ++i) sim.step();
    const glm::vec2 f0 = sim.flowAt(5, 1, 1);
    const float moving = std::sqrt(f0.x * f0.x + f0.y * f0.y);
    ASSERT_GT(moving, 1e-3f) << "precondition: the channel must actually be flowing";

    sim.clearSource(1, 1, 1);
    for (int i = 0; i < 400; ++i) sim.step();   // let it level out and go quiet

    const glm::vec2 f1 = sim.flowAt(5, 1, 1);
    const float after = std::sqrt(f1.x * f1.x + f1.y * f1.y);
    EXPECT_LT(after, moving * 0.1f) << "flow latched instead of decaying (moving=" << moving
                                    << " after=" << after << ")";
}

// ── SUB-VOXEL FLOOR (WaterSystemV3 Phase 4B) ──────────────────────────────────────────────────

// A cell with a sub-voxel floor has solid ground beneath its water, so water must REST on it — even
// though the cell is passable and the cell below is open air. Found live: without this, making
// floored cells passable let a puddle fall straight through a subcube platform and vanish.
TEST(WaterSimulation, WaterRestsOnASubVoxelFloorInsteadOfFallingThrough) {
    WaterSimulation sim(6, 8, 6);
    addFloor(sim);
    // A 1/3-height platform spanning the WHOLE extent at y=4, with nothing under it (y=1..3 are
    // open air). Full extent on purpose: a platform with open edges would let the water run off the
    // side and fall there, which is correct behaviour and would not test the floor itself.
    for (int x = 0; x < 6; ++x)
        for (int z = 0; z < 6; ++z) sim.setFloor(x, 4, z, 1.0f / 3.0f);
    for (int x = 1; x < 5; ++x)
        for (int z = 1; z < 5; ++z) sim.addWater(x, 4, z, 0.4f);
    const float total = sim.totalMass();

    for (int i = 0; i < 200; ++i) sim.step();

    float onPlatform = 0.0f, below = 0.0f;
    for (int x = 0; x < 6; ++x)
        for (int z = 0; z < 6; ++z) {
            onPlatform += sim.massAt(x, 4, z);
            for (int y = 1; y <= 3; ++y) below += sim.massAt(x, y, z);
        }
    EXPECT_NEAR(onPlatform, total, 1e-3f) << "water did not stay on the platform";
    EXPECT_LT(below, 1e-3f) << "water fell THROUGH the sub-voxel floor into the air below";
}

// ── MOMENTUM (WaterSystemV3 Phase 4) ──────────────────────────────────────────────────────────
// Without momentum the CA is pure diffusion and a spill fans out equally in all directions. These
// pin the behaviour change AND the two invariants it could plausibly destroy: exact mass
// conservation and the field's ability to come to rest.

// A FIXED PULSE of water runs down a channel and out onto an open flat shelf. With momentum the
// water that reaches the shelf must end up FURTHER DOWNSTREAM, measured as the mass-weighted
// centroid.
//
// Why a centroid and not "how far did it reach": momentum also raises throughput (an aligned
// neighbour's leveling factor goes 0.25 -> 0.45), so more water arrives on the shelf and then
// spreads from there. A raw downstream/lateral REACH ratio is confounded by that extra volume — the
// first version of this test compared reaches, and momentum "lost" 4/2 vs 3/1 purely because it had
// delivered more water. A mass-weighted centroid is volume-normalised and measures what we actually
// claim: where the water goes, not how much of it there is.
TEST(WaterSimulation, MomentumCarriesASpillFurtherDownstream) {
    auto run = [](float momentum) {
        WaterSimulation sim(40, 5, 21);
        sim.setMomentum(momentum);
        addFloor(sim);
        // A 1-wide channel along z=10 from x=1..14, opening onto a completely flat shelf at x>14.
        for (int x = 0; x <= 14; ++x)
            for (int y = 1; y < 5; ++y) { sim.setSolid(x, y, 9, true); sim.setSolid(x, y, 11, true); }
        for (int y = 1; y < 5; ++y) sim.setSolid(0, y, 10, true);   // closed upstream end
        // A fixed slug, NOT a sustained source: both runs carry exactly the same water.
        for (int x = 1; x <= 12; ++x) sim.addWater(x, 1, 10, 1.0f);
        for (int i = 0; i < 400; ++i) sim.step();

        // Mass-weighted centroid of everything that made it past the mouth (x >= 15).
        double m = 0.0, mx = 0.0, mz = 0.0;
        for (int x = 15; x < 40; ++x)
            for (int z = 0; z < 21; ++z)
                for (int y = 1; y < 5; ++y) {
                    const double c = sim.massAt(x, y, z);
                    if (c <= 0.0) continue;
                    m += c; mx += c * x; mz += c * std::abs(z - 10);
                }
        struct R { double mass, centroidX, lateral; };
        return R{ m, m > 0 ? mx / m : 0.0, m > 0 ? mz / m : 0.0 };
    };

    const auto mom = run(1.0f);
    const auto dif = run(0.0f);
    ASSERT_GT(mom.mass, 0.5) << "no water reached the shelf with momentum on";
    ASSERT_GT(dif.mass, 0.5) << "no water reached the shelf with momentum off";

    EXPECT_GT(mom.centroidX, dif.centroidX)
        << "momentum did not carry the spill further downstream (centroid x " << mom.centroidX
        << " vs " << dif.centroidX << ")";
    // The spread ACROSS the flow, per unit of delivered water, should not grow: inertia focuses the
    // stream, it does not fan it.
    EXPECT_LE(mom.lateral, dif.lateral + 0.25)
        << "momentum widened the spill instead of focusing it (lateral " << mom.lateral << " vs "
        << dif.lateral << ")";
}

// Momentum only changes WHICH neighbour receives a cell's outflow. Every transfer is still a paired
// -from/+to, so mass must be conserved to the bit.
TEST(WaterSimulation, MomentumConservesMassExactly) {
    WaterSimulation sim(16, 10, 16);
    sim.setMomentum(1.0f);
    addFloor(sim);
    sim.addWater(3, 8, 3, 1.0f);
    sim.addWater(8, 9, 9, 2.0f);   // overfull, drains under pressure
    sim.addWater(12, 7, 5, 0.5f);
    const float total = sim.totalMass();
    for (int i = 0; i < 400; ++i) {
        sim.step();
        ASSERT_NEAR(sim.totalMass(), total, 1e-3f) << "momentum leaked mass at step " << i;
        ASSERT_GE(sim.minMass(), -1e-6f) << "momentum drove a cell negative at step " << i;
    }
}

// The bias is clamped below the 0.5 overshoot bound, so the surface must still converge and the
// field must still reach rest — otherwise water would oscillate forever ("popcorn water") and the
// settle-skip optimisation would never engage.
TEST(WaterSimulation, MomentumFieldStillSettlesFlat) {
    WaterSimulation sim(12, 8, 12);
    sim.setMomentum(1.0f);
    addFloor(sim);
    for (int z = 2; z < 10; ++z)
        for (int x = 2; x < 10; ++x) sim.addWater(x, 5, z, 1.0f);   // a slab dropped from height

    int steps = 0;
    while (!sim.settled() && steps < 3000) { sim.step(); ++steps; }
    ASSERT_TRUE(sim.settled()) << "momentum kept the field awake for " << steps << " steps";

    // ...and it settled FLAT, not into a lopsided heap the inertia bias pushed to one side.
    float lo = 1e9f, hi = -1e9f;
    for (int z = 2; z < 10; ++z)
        for (int x = 2; x < 10; ++x) {
            float col = 0.0f;
            for (int y = 1; y < 8; ++y) col += sim.massAt(x, y, z);
            lo = std::min(lo, col); hi = std::max(hi, col);
        }
    EXPECT_LT(hi - lo, 0.05f) << "settled surface is not flat (column mass " << lo << ".." << hi << ")";
}

// The proxy must ride along with its water when the region recenters, or a river's shading would
// scramble every time the player walks far enough to move the sim window.
TEST(WaterSimulation, FlowProxySurvivesShift) {
    WaterSimulation sim(12, 4, 3);
    addFloor(sim);
    for (int x = 0; x < 12; ++x)
        for (int y = 1; y < 4; ++y) { sim.setSolid(x, y, 0, true); sim.setSolid(x, y, 2, true); }
    sim.setSolid(0, 1, 1, true);
    sim.setSolid(0, 2, 1, true);
    sim.setSource(1, 1, 1, WaterSimulation::MAX_MASS);
    for (int i = 0; i < 80; ++i) sim.step();

    const glm::vec2 before = sim.flowAt(5, 1, 1);
    ASSERT_GT(before.x, 0.0f);
    sim.shift(glm::ivec3(2, 0, 0));            // window moves +2x → content lands at local x-2
    const glm::vec2 after = sim.flowAt(3, 1, 1);
    EXPECT_FLOAT_EQ(after.x, before.x);
    EXPECT_FLOAT_EQ(after.y, before.y);
}
