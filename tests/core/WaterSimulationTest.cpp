#include <gtest/gtest.h>
#include "core/WaterSimulation.h"
#include <chrono>
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
