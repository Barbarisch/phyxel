#include <gtest/gtest.h>
#include "core/WaterSimulation.h"

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
