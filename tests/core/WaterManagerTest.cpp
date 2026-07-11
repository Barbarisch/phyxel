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

// A fully settled field must not pay the O(box) surface rebuild every update tick: update() only
// calls rebuildSurface() when a sweep actually ran (settled skips don't advance sweepsRun). Without
// this, "settled water is free" was only true of the sim step — the 20 Hz surface scan remained.
TEST(WaterManagerTest, SettledFieldSkipsSurfaceRebuild) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildBasinAndFill(wm);   // settled walled pool (40 updates)

    const auto rebuildsAtRest = wm.surfaceRebuilds();
    ASSERT_GT(rebuildsAtRest, 0ull) << "filling the basin should have rebuilt the surface";
    for (int i = 0; i < 10; ++i) wm.update(0.1f);   // all steps are settled skips
    EXPECT_EQ(wm.surfaceRebuilds(), rebuildsAtRest)
        << "settled field kept rebuilding its surface every tick";

    wm.placeWater(glm::vec3(13.5f, 3.0f, 13.5f), 0.5f);  // disturbance
    wm.update(0.1f);
    EXPECT_GT(wm.surfaceRebuilds(), rebuildsAtRest)
        << "disturbed field did not rebuild its surface — water would render stale";
}

// ─── Sea-level unification (WaterSystemV2 Phase A wrap-up) ────────────────────────────────────────
// The sim's default sea level must be the SHARED engine constant (WorldConstants.h) — it used to
// default to 0 while the sea-plane renderer defaulted to 16, so the plane drew a sea the sim didn't
// have. RenderCoordinator::m_seaLevel is initialized from the same constant (not unit-instantiable
// here — it's Vulkan-bound — so that side is enforced by construction, this side by assertion).
static_assert(Phyxel::Core::kSeaLevelY == 16.0f,
              "kSeaLevelY changed — re-check every consumer (WorldGenerator, WaterManager, "
              "RenderCoordinator, game.json defaults) and the grounded-values table");

TEST(WaterManagerTest, DefaultSeaLevelIsTheSharedWorldConstant) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(8, 8, 8));
    EXPECT_FLOAT_EQ(wm.seaLevel(), Phyxel::Core::kSeaLevelY)
        << "WaterManager re-declared its own sea-level default — render/sim drift is back";
}

// ─── Baked WATER TABLE (Phase C: generation feeds water) ──────────────────────────────────────────
// With a table bound (world column → baked level), rebuildOcean derives ALL pins from it: a baked
// lake fills at its own level, dry columns stay dry, and — because recenter re-runs the derivation —
// the lake persists at its WORLD position when the region moves. No authored seeds anywhere.
TEST(WaterManagerTest, BakedWaterTableFillsLakeAndSurvivesRecenter) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    for (int y = 0; y <= 4; ++y) {   // basin walls (world space) around interior x,z in [12,15]
        for (int z = 11; z <= 16; ++z) { wm.setSolidWorld(11, y, z, true); wm.setSolidWorld(16, y, z, true); }
        for (int x = 11; x <= 16; ++x) { wm.setSolidWorld(x, y, 11, true); wm.setSolidWorld(x, y, 16, true); }
    }
    // Baked table: the basin interior is a lake with surface at world y=2; everywhere else dry.
    wm.setWaterTable([](float wx, float wz) -> float {
        return (wx >= 12.0f && wx < 16.0f && wz >= 12.0f && wz < 16.0f) ? 2.0f : -1e30f;
    });
    wm.update(0.1f);   // rebuild (table pins) + a step to fill the pins

    const float volume = wm.totalMass();
    EXPECT_NEAR(volume, 48.0f, 1.0f) << "lake should fill its 4x4 columns from y=0 to level y=2";
    EXPECT_GT(wm.massAtWorld(glm::vec3(13.5f, 2.5f, 13.5f)), 0.9f) << "lake surface should be wet";
    EXPECT_LT(wm.massAtWorld(glm::vec3(13.5f, 3.5f, 13.5f)), 1e-3f) << "water above the baked level";
    EXPECT_LT(wm.massAtWorld(glm::vec3(25.5f, 0.5f, 25.5f)), 1e-3f) << "dry column got water";

    // A lake sits at ITS OWN level (≠ the sea plane's height), so it must render per-cell —
    // the sea-suppression below must not swallow it.
    EXPECT_FALSE(wm.surfaceCells().empty()) << "baked lake lost its per-cell surface";

    // Move the region; the table re-derives at the new origin — the lake stays at its world position.
    wm.recenter(glm::ivec3(6, 0, 6));
    wm.update(0.1f);
    EXPECT_NEAR(wm.totalMass(), volume, 1.0f) << "lake volume changed across the recenter";
    EXPECT_GT(wm.massAtWorld(glm::vec3(13.5f, 2.5f, 13.5f)), 0.9f)
        << "lake left its world position after recenter — table re-derivation broken";
    EXPECT_LT(wm.massAtWorld(glm::vec3(13.5f, 3.5f, 13.5f)), 1e-3f);
}

// The undisturbed SEA is drawn by the flat sea plane (one look, inside and outside the region) —
// per-cell rendering of pinned sea-surface cells is SUPPRESSED. Emitting them double-drew the
// ocean as a darker, hard-edged slab exactly the size of the sim region, following the camera
// (user-reported 2026-07-11). Disturbed water (a splash above sea level) must still render
// per-cell — that's what the sim renderer is for.
TEST(WaterManagerTest, UndisturbedSeaIsLeftToTheFlatPlaneNotPerCellRendered) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 16, 16));
    wm.setSeaLevel(4.0f);
    wm.setOceanBoundary(true);
    wm.update(0.1f);
    ASSERT_GT(wm.totalMass(), 100.0f) << "sea should have flooded";
    EXPECT_TRUE(wm.surfaceCells().empty())
        << "undisturbed sea emitted per-cell surface quads — the region renders as a slab again";

    wm.placeWater(glm::vec3(8.5f, 6.5f, 8.5f), 2.0f);   // a splash ABOVE sea level
    EXPECT_FALSE(wm.surfaceCells().empty())
        << "disturbed water above sea level must still render per-cell";
}

// ─── Phase A STRESS (doc-required: docs/WaterSystemV2.md §Phase A "Stress") ───────────────────────
// Walk the focus back and forth so the region recenters MANY times over a standing (walled) lake
// that always stays in-window, asserting the invariant at EVERY recenter (not just at the end):
//   volume — total mass exactly conserved;
//   level  — the lake's settled surface stays at the same world Y (wet at y=0, dry at y=1);
//   place  — the water stays at its WORLD position while the window slides under it.
// 50 recenters, alternating window x ∈ [10,42) and x ∈ [-10,22) — the second window has a NEGATIVE
// origin, so this also stresses the negative-world-coordinate path the single-recenter tests never
// crossed. The basin (world x,z ∈ [11,16]) is inside both windows, so nothing may fall off.
TEST(WaterManagerTest, StressManyRecentersOverStandingLakeKeepInvariants) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildBasinAndFill(wm);

    const float volume = wm.totalMass();
    const float levelCell = wm.massAtWorld(glm::vec3(13.5f, 0.5f, 13.5f));  // settled surface cell
    ASSERT_GT(volume, 15.0f) << "pool did not fill";
    ASSERT_GT(levelCell, 0.9f) << "lake should be full at y=0";
    ASSERT_LT(wm.massAtWorld(glm::vec3(13.5f, 1.5f, 13.5f)), 0.05f) << "lake should be dry at y=1";

    int recenters = 0;
    for (int i = 0; i < 25; ++i) {
        for (const float fx : {26.5f, 6.5f}) {          // oscillate past the dead zone each time
            const bool moved = wm.followTo(glm::vec3(fx, 50.0f, 16.5f), 4);
            ASSERT_TRUE(moved) << "iteration " << i << ": focus " << fx
                               << " did not trigger a recenter — the stress isn't stressing";
            ++recenters;
            wm.update(0.1f);  // one live-loop tick after the move (as the frame loop would)
            ASSERT_NEAR(wm.totalMass(), volume, 1e-3f)
                << "volume changed at recenter #" << recenters;
            ASSERT_NEAR(wm.massAtWorld(glm::vec3(13.5f, 0.5f, 13.5f)), levelCell, 1e-3f)
                << "lake level dropped at its world position at recenter #" << recenters;
            ASSERT_LT(wm.massAtWorld(glm::vec3(13.5f, 1.5f, 13.5f)), 0.05f)
                << "lake level ROSE (water above the settled surface) at recenter #" << recenters;
        }
    }
    EXPECT_EQ(recenters, 50);
}

// Long one-way walk with the ocean BOUNDARY CONDITION on: 100 consecutive recenters marching the
// region ~800 cells, asserting at EVERY recenter that the sea re-establishes to the same volume and
// stays at the same LEVEL (wet at y=4, dry at y=5). This is the live following-region scenario —
// a point seed fails it after the first recenter that leaves the seed behind.
TEST(WaterManagerTest, StressLongWalkOceanBoundaryKeepsSeaAtEveryRecenter) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 16, 16));
    wm.setSeaLevel(4.0f);
    wm.setOceanBoundary(true);
    wm.update(0.1f);
    const float volume = wm.totalMass();
    ASSERT_GT(volume, 100.0f) << "boundary condition did not flood the initial region";

    int recenters = 0;
    for (int step = 1; step <= 100; ++step) {
        const float fx = 8.0f + 8.0f * step;            // stride 8 > hysteresis 4 → recenter each step
        if (!wm.followTo(glm::vec3(fx, 30.0f, 8.0f), 4)) continue;
        ++recenters;
        wm.update(0.1f);  // the live loop steps every frame; fillOcean pins re-fill on the step
        ASSERT_NEAR(wm.totalMass(), volume, volume * 0.05f)
            << "sea volume wrong at recenter #" << recenters << " (focus x=" << fx << ")";
        ASSERT_GT(wm.massAtWorld(glm::vec3(fx, 4.5f, 8.5f)), 0.5f)
            << "no sea at the new window centre at recenter #" << recenters;
        ASSERT_LT(wm.massAtWorld(glm::vec3(fx, 5.5f, 8.5f)), 0.05f)
            << "sea ABOVE sea level at recenter #" << recenters;
    }
    EXPECT_EQ(recenters, 100) << "the walk should recenter on every stride";
}
