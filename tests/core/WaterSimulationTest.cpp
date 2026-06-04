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
