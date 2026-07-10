#include <gtest/gtest.h>

#include "core/WaterManager.h"

#include <glm/glm.hpp>

using Phyxel::Core::WaterManager;

// WaterSystemV2 Phase A1: the water region can RECENTER (move its window) with the water field
// carried along, so it can later follow the player / travel to procedurally-generated rivers. These
// use a null ChunkManager: syncSolidsFromChunks() no-ops, so there is no terrain and the only solid
// boundary is the out-of-bounds floor below y=0 — enough to hold a pool for a mass-conservation test.
namespace {

// Build a walled 4×4 basin at world x,z in [12,15] (perimeter walls at 11/16, resting on the
// out-of-bounds y=-1 floor), drop 16 cells of water into it, and let it settle into a contained
// pool. Evaporation is off by default, so total mass is conserved and any loss/gain at a recenter
// seam is directly observable. (Walls set via setSolidWorld travel with the shift; with a null
// ChunkManager syncSolidsFromChunks() no-ops, so they stay at their world positions across recenter.)
void buildBasinAndFill(WaterManager& wm) {
    for (int y = 0; y <= 4; ++y) {
        for (int z = 11; z <= 16; ++z) { wm.setSolidWorld(11, y, z, true); wm.setSolidWorld(16, y, z, true); }
        for (int x = 11; x <= 16; ++x) { wm.setSolidWorld(x, y, 11, true); wm.setSolidWorld(x, y, 16, true); }
    }
    for (int x = 12; x <= 15; ++x)
        for (int z = 12; z <= 15; ++z)
            wm.placeWater(glm::vec3(x + 0.5f, 3.0f, z + 0.5f), 1.0f);
    for (int i = 0; i < 40; ++i) wm.update(0.1f);
}

}  // namespace

// A pool fully inside the window keeps its exact total mass across a recenter that keeps it inside,
// and the water stays at the same WORLD position (the window moved, the water did not).
TEST(WaterManagerTest, RecenterConservesContainedMassAndKeepsWorldPosition) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildBasinAndFill(wm);

    const float before = wm.totalMass();
    ASSERT_GT(before, 15.0f) << "pool did not fill (16 cells × 1.0 expected)";
    const float atWorldBefore = wm.massAtWorld(glm::vec3(13.5f, 0.5f, 13.5f));
    ASSERT_GT(atWorldBefore, 0.5f) << "expected water at world (13,0,13) after settling";

    // Move the window +5,+5 in x,z — the pool (world x,z in [12,15]) lands at local [7,10], still
    // well inside [0,32).
    wm.recenter(glm::ivec3(5, 0, 5));
    EXPECT_EQ(wm.origin(), glm::ivec3(5, 0, 5));

    EXPECT_NEAR(wm.totalMass(), before, 1e-3f) << "mass lost/gained across the recenter seam";
    EXPECT_NEAR(wm.massAtWorld(glm::vec3(13.5f, 0.5f, 13.5f)), atWorldBefore, 1e-3f)
        << "water did not stay at its world position — shift direction/sign wrong";
}

// Recentering to the current origin is a no-op (guards against needless re-flood / drift).
TEST(WaterManagerTest, RecenterToSameOriginIsNoOp) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildBasinAndFill(wm);
    const float before = wm.totalMass();
    wm.recenter(glm::ivec3(0, 0, 0));
    EXPECT_FLOAT_EQ(wm.totalMass(), before);
}

// A SOURCE (spring) survives a recenter. recenter re-derives all sources: it calls rebuildOcean(),
// whose fillOcean() clears every source pin, then applySprings() re-pins from the world-space spring
// list at the new origin. If that re-projection were dropped, the spring would stop injecting after a
// recenter. We assert the spring keeps growing the total mass across the move. (Exercises the
// ocean/spring re-projection path the plain-pool tests don't touch — flagged by the audit.)
TEST(WaterManagerTest, RecenterKeepsSpringInjecting) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    wm.addSpring(glm::vec3(16.5f, 10.5f, 16.5f), 1.0f);  // infinite source, re-pinned to 1.0 each step
    for (int i = 0; i < 30; ++i) wm.update(0.1f);
    const float atRecenter = wm.totalMass();
    ASSERT_GT(atRecenter, 1.0f) << "spring should have injected mass before the recenter";

    wm.recenter(glm::ivec3(6, 0, 6));  // spring world (16,10,16) → local (10,10,10), still in-window
    // (This spring's pool is un-walled, so it spreads to the box edges and a little is legitimately
    // dropped at the frontier by the recenter — contained-mass conservation is covered by the walled
    // test above. Here we care only that the SOURCE survived the move.)
    const float justAfter = wm.totalMass();
    for (int i = 0; i < 30; ++i) wm.update(0.1f);
    EXPECT_GT(wm.totalMass(), justAfter + 1.0f)
        << "spring stopped injecting after recenter — source re-projection (applySprings) was lost";
}

// followTo recenters the region on a focus (camera) only once it drifts past the hysteresis dead
// zone, snaps the box to re-centre horizontally, and PRESERVES the Y origin (the box stays on its
// ground/sea band, not the camera altitude).
TEST(WaterManagerTest, FollowToRecentersPastHysteresisAndKeepsY) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));  // centre at world (16,8,16)
    // Focus inside the dead zone (and high above) → no recenter.
    EXPECT_FALSE(wm.followTo(glm::vec3(18.0f, 50.0f, 12.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(0, 0, 0));
    // Focus far in x → recenter so it is centred; Y origin preserved despite the y=50 camera height.
    EXPECT_TRUE(wm.followTo(glm::vec3(100.0f, 50.0f, 16.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(100 - 16, 0, 16 - 16));  // (84, 0, 0)
    // Now inside the dead zone of the new centre (84+16=100) → no further move.
    EXPECT_FALSE(wm.followTo(glm::vec3(103.0f, 5.0f, 18.0f), 8));
}

// The ocean BOUNDARY CONDITION seeds the sea from the region edges (no point seed needed) and, being
// re-derived from the frontier on every recenter, the sea PERSISTS when the region moves away — the
// exact failure a point seed has (it leaves the window and the ocean drains). Null ChunkManager → no
// terrain, so every cell at/below sea level floods (an all-water region up to sea level).
TEST(WaterManagerTest, OceanBoundaryFillsWithoutSeedsAndSurvivesRecenter) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 16, 16));
    wm.setSeaLevel(4.0f);
    // No point seed and boundary off → no ocean, even after a step.
    wm.update(0.1f);
    EXPECT_FLOAT_EQ(wm.totalMass(), 0.0f) << "there should be no ocean without seeds or boundary mode";

    // Boundary mode on → the region floods to sea level from its edges, with no point seed.
    wm.setOceanBoundary(true);
    wm.update(0.1f);
    const float flooded = wm.totalMass();
    EXPECT_GT(flooded, 100.0f) << "boundary condition did not seed the ocean";

    // Move the region far away. A point seed would be left behind and the sea would drain; the
    // boundary condition re-seeds from the new frontier. recenter re-derives the source PINS; a step
    // (as the live frame loop runs every frame) re-pins them to full mass, restoring the same fill.
    wm.recenter(glm::ivec3(500, 0, 500));
    wm.update(0.1f);
    EXPECT_NEAR(wm.totalMass(), flooded, flooded * 0.05f)
        << "ocean drained after the region moved — boundary condition not re-seeding at the new location";
}

// Connectivity-gating still holds WITH boundary seeds: a sealed sub-sea pocket (a non-solid cell
// fully enclosed by solids, below sea level, not reachable from any region edge) stays DRY while the
// rest of the region floods. This is the "sealed pocket stays dry" invariant, now via the boundary path.
TEST(WaterManagerTest, OceanBoundaryLeavesSealedPocketDry) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 16, 16));
    // Enclose the single cell (8,4,8) — well below sea level 8 — with solids on all six faces.
    wm.setSolidWorld(7, 4, 8, true); wm.setSolidWorld(9, 4, 8, true);
    wm.setSolidWorld(8, 3, 8, true); wm.setSolidWorld(8, 5, 8, true);
    wm.setSolidWorld(8, 4, 7, true); wm.setSolidWorld(8, 4, 9, true);
    wm.setSeaLevel(8.0f);
    wm.setOceanBoundary(true);
    for (int i = 0; i < 5; ++i) wm.update(0.1f);

    EXPECT_GT(wm.totalMass(), 100.0f) << "the open region should have flooded";
    EXPECT_LT(wm.massAtWorld(glm::vec3(2.5f, 4.5f, 2.5f)), 100.0f);  // sanity: an open cell exists
    EXPECT_GT(wm.massAtWorld(glm::vec3(2.5f, 4.5f, 2.5f)), 0.5f) << "an open sub-sea cell should be wet";
    EXPECT_LT(wm.massAtWorld(glm::vec3(8.5f, 4.5f, 8.5f)), 1e-3f)
        << "the sealed pocket flooded — connectivity-gating broke with boundary seeds";
}

// Recentering far enough that the pool leaves the window drops it (mass falls off the frontier) —
// the seam-loss the ocean boundary condition (Phase A2) will later replace at the leading edge.
TEST(WaterManagerTest, RecenterPastThePoolDropsItAtTheFrontier) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildBasinAndFill(wm);
    ASSERT_GT(wm.totalMass(), 15.0f);
    wm.recenter(glm::ivec3(200, 0, 200));  // pool now far outside the window
    EXPECT_FLOAT_EQ(wm.totalMass(), 0.0f) << "pool outside the moved window should be gone";
}
