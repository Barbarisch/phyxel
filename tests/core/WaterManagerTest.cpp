#include <gtest/gtest.h>

#include "core/WaterManager.h"
#include "core/ChunkManager.h"

#include <cmath>
#include <glm/glm.hpp>

using Phyxel::ChunkManager;
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

// followTo recenters the region on a focus (camera) only once it drifts past the dead zone —
// horizontally (hysteresisCells) AND vertically (max(4, dims.y/4)). Vertical following (Phase C2)
// is what lets inland rivers/lakes at altitude get water: a Y-anchored box only ever covered the
// sea band, so a river bed at y≈72 could never be wet. Y origin clamps at 0.
TEST(WaterManagerTest, FollowToRecentersPastHysteresisHorizontallyAndVertically) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));  // centre (16,8,16); vHyst 4
    // Focus inside BOTH dead zones → no recenter.
    EXPECT_FALSE(wm.followTo(glm::vec3(18.0f, 10.0f, 12.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(0, 0, 0));
    // Far in x AND high above → recenter horizontally and vertically at once.
    EXPECT_TRUE(wm.followTo(glm::vec3(100.0f, 72.0f, 16.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(100 - 16, 72 - 8, 16 - 16));  // (84, 64, 0)
    // Inside both new dead zones (centre 100/72/16) → no further move.
    EXPECT_FALSE(wm.followTo(glm::vec3(103.0f, 70.0f, 18.0f), 8));
    // Pure VERTICAL drift (horizontal still) recenters too — a player climbing a mountain river.
    EXPECT_TRUE(wm.followTo(glm::vec3(100.0f, 30.0f, 16.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(84, 30 - 8, 0));
    // Descending to the world floor clamps Y at 0 (never below the world band).
    EXPECT_TRUE(wm.followTo(glm::vec3(100.0f, 2.0f, 16.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(84, 0, 0));
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

// ─── Poured-water persistence (water-as-terrain-stage P3) ─────────────────────────────────────────
// This suite REPLACES RecenterPastThePoolDropsItAtTheFrontier: the silent drop that test pinned is
// exactly what P3 removes. The sim still drops off-frontier mass (bounded by design), but the
// departing columns' unpinned surface levels are CAPTURED first and reseeded when the window
// returns. Terrain here is a REAL chunk (persists across recenters — setSolidWorld walls travel
// with the window and are lost off the frontier, which is why buildBasinAndFill can't back these).

namespace {

// Ground everywhere at y<=2; wall ring y 3..5 around the 4x4 interior [12,15]^2; 16 cells poured →
// a settled 1-deep pool at y=3 (surface level 4.0).
struct TerrainBasinFixture {
    ChunkManager cm;
    std::unique_ptr<WaterManager> wm;
    explicit TerrainBasinFixture(const glm::ivec3& origin = glm::ivec3(0, 0, 0), bool pour = true) {
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(0, 0, 0));
        owned->initializeForLoading();
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) {
                for (int y = 0; y <= 2; ++y) owned->addCube(glm::ivec3(x, y, z));
                const bool ring = x >= 11 && x <= 16 && z >= 11 && z <= 16 &&
                                  !(x >= 12 && x <= 15 && z >= 12 && z <= 15);
                if (ring)
                    for (int y = 3; y <= 5; ++y) owned->addCube(glm::ivec3(x, y, z));
            }
        cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
        cm.chunks.push_back(std::move(owned));
        wm = std::make_unique<WaterManager>(&cm, origin, glm::ivec3(32, 16, 32));
        if (pour) {
            for (int x = 12; x <= 15; ++x)
                for (int z = 12; z <= 15; ++z)
                    wm->placeWater(glm::vec3(x + 0.5f, 4.0f, z + 0.5f), 1.0f);
            for (int i = 0; i < 40; ++i) wm->update(0.1f);
        }
    }
};

}  // namespace

// Walking away captures the pour; walking back reseeds it at its level. The in-window mass still
// drops to zero while away (the sim is bounded — that part is by design and stays).
TEST(WaterManagerTest, RecenterPastThePoolCapturesAndReseedsIt) {
    TerrainBasinFixture f;
    WaterManager& wm = *f.wm;
    const float before = wm.totalMass();
    ASSERT_GT(before, 15.0f) << "pool did not fill";
    ASSERT_EQ(wm.overrideCount(), 0u) << "nothing departed yet — no overrides expected";

    wm.recenter(glm::ivec3(200, 0, 200));  // pool far outside the window
    EXPECT_FLOAT_EQ(wm.totalMass(), 0.0f) << "in-window mass while away should be zero";
    EXPECT_EQ(wm.overrideCount(), 16u) << "each departing pool column should be captured once";

    wm.recenter(glm::ivec3(0, 0, 0));      // walk back
    EXPECT_NEAR(wm.totalMass(), before, 0.1f) << "pour did not reseed at its captured level";
    EXPECT_GT(wm.massAtWorld(glm::vec3(13.5f, 3.5f, 13.5f)), 0.9f)
        << "reseeded water is not at the pool's world position";
    EXPECT_EQ(wm.overrideCount(), 0u) << "consumed overrides must be erased (live again)";
}

// Vertical window travel drops water too (the region follows the camera in Y — this is how live
// pours were FIRST observed draining). A pure-Y recenter must capture and restore the same way.
TEST(WaterManagerTest, VerticalRecenterCapturesAndReseedsThePour) {
    TerrainBasinFixture f;
    WaterManager& wm = *f.wm;
    const float before = wm.totalMass();
    ASSERT_GT(before, 15.0f);

    wm.recenter(glm::ivec3(0, 40, 0));     // camera looked up a cliff: band now y 40..56
    EXPECT_FLOAT_EQ(wm.totalMass(), 0.0f);
    EXPECT_EQ(wm.overrideCount(), 16u) << "the departing vertical slice should be captured";
    wm.recenter(glm::ivec3(0, 0, 0));
    EXPECT_NEAR(wm.totalMass(), before, 0.1f) << "pour lost across a vertical round trip";
}

// A stale override at a column the baked table already covers must NOT double-pour: the reseed
// erases it instead of stacking unpinned mass onto pinned lake water.
TEST(WaterManagerTest, OverridesDoNotDoubleCountTablePinnedColumns) {
    TerrainBasinFixture f(glm::ivec3(0, 0, 0), /*pour=*/false);
    WaterManager& wm = *f.wm;
    // Bake a lake over the basin interior at level 6 (above ground y<=2, inside the walls).
    wm.setWaterTable([](float wx, float wz) -> float {
        return (wx >= 12.0f && wx < 16.0f && wz >= 12.0f && wz < 16.0f) ? 6.0f : -1e30f;
    });
    wm.update(0.1f);
    const float tableOnly = wm.totalMass();
    ASSERT_GT(tableOnly, 5.0f) << "the baked lake should have filled";

    // A stale capture below the lake surface (e.g. recorded before the bake was bound).
    ASSERT_TRUE(wm.loadOverrides("13 13 4.5\n"));
    ASSERT_EQ(wm.overrideCount(), 1u);
    wm.update(0.1f);   // oceanDirty → rebuild → applyOverrides
    EXPECT_NEAR(wm.totalMass(), tableOnly, 1e-3f)
        << "override double-poured mass onto the pinned lake";
    EXPECT_EQ(wm.overrideCount(), 0u) << "redundant override should be erased, not kept forever";
}

// The store round-trips through its serialized form: a fresh manager (fresh world session) restores
// the pond from text alone once its window reaches the columns.
TEST(WaterManagerTest, OverridesSurviveSerializeLoadRoundTrip) {
    TerrainBasinFixture f;
    const float before = f.wm->totalMass();
    f.wm->recenter(glm::ivec3(200, 0, 200));
    ASSERT_EQ(f.wm->overrideCount(), 16u);
    const std::string blob = f.wm->serializeOverrides();
    ASSERT_FALSE(blob.empty());

    TerrainBasinFixture g(glm::ivec3(200, 0, 200), /*pour=*/false);  // fresh session, window far away
    ASSERT_TRUE(g.wm->loadOverrides(blob));
    EXPECT_EQ(g.wm->overrideCount(), 16u);
    g.wm->recenter(glm::ivec3(0, 0, 0));
    EXPECT_NEAR(g.wm->totalMass(), before, 0.1f) << "pond not restored from serialized overrides";
    EXPECT_GT(g.wm->massAtWorld(glm::vec3(13.5f, 3.5f, 13.5f)), 0.9f);

    EXPECT_FALSE(g.wm->loadOverrides("this is not an override line"))
        << "garbage must be rejected, not half-loaded";
}

// ─── CA edge outflow (water-as-terrain-stage P4) ──────────────────────────────────────────────────
// The window edge stops being an invisible WALL: unpinned water reaching the ring bleeds out into
// a world-keyed mass bank instead of piling up, and comes back as live water when the window
// reaches its landing column. Pinned water is exempt (bleeding an infinite reservoir would mint
// mass forever). The legacy wall behavior is the red baseline, asserted here with the flag OFF.

// Repeated pours against the window edge: walls (off) stack the water; outflow (on) bounds the
// in-window mass and grows the bank by exactly what left (conservation across the seam).
TEST(WaterManagerTest, EdgeOutflowBleedsSpillIntoTheBankInsteadOfWalling) {
    // Null cm: the out-of-bounds floor below y=0 is the only ground; pours at the x=0 ring column.
    WaterManager off(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
    WaterManager on(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
    on.setEdgeOutflow(true);
    // Three separate ring columns, so the bleed spreads over three bank landing columns and the
    // per-column bank cap (4.0) is not the limiting factor for the conservation check.
    for (int i = 0; i < 4; ++i)
        for (const float pz : {5.5f, 8.5f, 11.5f}) {
            off.placeWater(glm::vec3(0.5f, 2.5f, pz), 1.0f);
            on.placeWater(glm::vec3(0.5f, 2.5f, pz), 1.0f);
            off.update(0.1f);
            on.update(0.1f);
        }
    for (int i = 0; i < 30; ++i) { off.update(0.1f); on.update(0.1f); }

    // RED baseline (walls): every poured unit is still in-window.
    EXPECT_NEAR(off.totalMass(), 12.0f, 0.5f) << "legacy walls should keep all poured mass in-window";
    EXPECT_FLOAT_EQ(off.outflowBankTotal(), 0.0f);

    // Outflow: the edge bled most of it into the bank; in-window + banked ≈ poured (the seam
    // conserves — nothing deleted below the cap). Films below the hold may remain in-window.
    EXPECT_LT(on.totalMass(), 6.0f) << "edge did not bleed — the wall is back";
    EXPECT_GT(on.outflowBankTotal(), 4.0f) << "bled mass did not reach the bank";
    EXPECT_NEAR(on.totalMass() + on.outflowBankTotal(), 12.0f, 1.0f)
        << "mass vanished at the seam instead of banking";
}

// Banked mass is redeposited as live water when the window recenters over its landing column.
// The window is SMALLER than the fixture's chunk, so the landing column (just outside the window)
// still has real loaded terrain when the window later covers it.
TEST(WaterManagerTest, OutflowBankRedepositsWhenTheWindowArrives) {
    TerrainBasinFixture f(glm::ivec3(0, 0, 0), /*pour=*/false);   // flat ground at y<=2, chunk 0..31
    WaterManager wm(&f.cm, glm::ivec3(4, 0, 4), glm::ivec3(16, 8, 16));  // window x,z in [4,20)
    wm.setEdgeOutflow(true);
    // Pour against the x = 19 ring column: it bleeds to the bank at world x 20 (inside the chunk).
    for (int i = 0; i < 8; ++i) {
        wm.placeWater(glm::vec3(19.5f, 4.5f, 12.5f), 1.0f);
        wm.update(0.1f);
    }
    for (int i = 0; i < 30; ++i) wm.update(0.1f);
    const float banked = wm.outflowBankTotal();
    ASSERT_GT(banked, 2.0f) << "edge pour did not bank";

    wm.recenter(glm::ivec3(8, 0, 4));   // landing column x=20 → local 12: interior, real ground
    EXPECT_LT(wm.outflowBankTotal(), banked * 0.5f)
        << "bank did not redeposit over loaded ground";
    EXPECT_GT(wm.totalMass(), banked * 0.5f) << "redeposited mass is not in the sim";
}

// A pinned (baked-table) shoreline at the window edge must NOT bleed: pins are infinite
// reservoirs, and outflowing them would grow the bank forever from nothing.
TEST(WaterManagerTest, PinnedEdgeWaterDoesNotBleed) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
    wm.setEdgeOutflow(true);
    wm.setWaterTable([](float, float) { return 3.0f; });   // uniform pinned sea to y=3
    wm.update(0.1f);
    const float sea = wm.totalMass();
    ASSERT_GT(sea, 100.0f) << "table sea should have filled";
    for (int i = 0; i < 30; ++i) wm.update(0.1f);
    EXPECT_FLOAT_EQ(wm.outflowBankTotal(), 0.0f) << "pinned sea bled at the edge — mass minted";
    EXPECT_NEAR(wm.totalMass(), sea, sea * 0.02f);
}

// Bank entries survive the serialize/load round trip alongside the level overrides.
TEST(WaterManagerTest, OutflowBankSurvivesSerializeLoadRoundTrip) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
    wm.setEdgeOutflow(true);
    for (int i = 0; i < 8; ++i) {
        wm.placeWater(glm::vec3(0.5f, 2.5f, 8.5f), 1.0f);
        wm.update(0.1f);
    }
    for (int i = 0; i < 20; ++i) wm.update(0.1f);
    const float banked = wm.outflowBankTotal();
    ASSERT_GT(banked, 1.0f);

    WaterManager wm2(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
    ASSERT_TRUE(wm2.loadOverrides(wm.serializeOverrides()));
    EXPECT_NEAR(wm2.outflowBankTotal(), banked, 1e-2f) << "bank lines lost in the round trip";
}

// Monotone stress: walk away and back MANY times. Capture→reseed is level-based, so repeated
// cycles must never grow the water (each reseed fills to at most the captured level; each capture
// records at most the reseeded level). Asserted EVERY cycle, not just at the end.
TEST(WaterManagerTest, StressWalkAwayAndBackNeverGrowsThePour) {
    TerrainBasinFixture f;
    WaterManager& wm = *f.wm;
    const float initial = wm.totalMass();
    ASSERT_GT(initial, 15.0f);
    for (int i = 0; i < 10; ++i) {
        wm.recenter(glm::ivec3(200, 0, 200));
        ASSERT_EQ(wm.overrideCount(), 16u) << "cycle " << i << ": capture count drifted";
        wm.recenter(glm::ivec3(0, 0, 0));
        ASSERT_EQ(wm.overrideCount(), 0u) << "cycle " << i << ": overrides not consumed";
        const float back = wm.totalMass();
        ASSERT_LE(back, initial + 1e-3f) << "cycle " << i << ": the pour GREW — capture/reseed pumps mass";
        ASSERT_NEAR(back, initial, 0.1f) << "cycle " << i << ": the pour eroded";
        for (int u = 0; u < 5; ++u) wm.update(0.1f);   // let it settle between cycles
    }
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

    // ⚠ SPEC CHANGE (water-layer P1): a pinned, undisturbed lake surface is now drawn by the
    // water-layer clipmap at its baked level — per-cell emission is SUPPRESSED, exactly like the
    // sea has been since 2026-07-11 (the old assertion here demanded the opposite and was the
    // red proof for this change). Disturbed water on the lake must still render per-cell.
    EXPECT_TRUE(wm.surfaceCells().empty())
        << "pinned lake surface emitted per-cell quads — double-draws over the water layer";
    wm.placeWater(glm::vec3(13.5f, 4.5f, 13.5f), 2.0f);   // a splash ABOVE the lake level
    EXPECT_FALSE(wm.surfaceCells().empty())
        << "disturbed water above a lake must still render per-cell";
    // Drain the splash back out so the recenter checks below measure the undisturbed lake.
    wm.placeWater(glm::vec3(13.5f, 4.5f, 13.5f), -2.0f);
    wm.update(0.1f);

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

// ─── Baked RIVERS (Phase C2: channel tags + frontier inflow) ──────────────────────────────────────
// A bound river query waters the carved channel: the bed cell of every river column is channel-
// tagged, region-EDGE river columns are pinned as upstream inflow, and the CA carries the water
// downhill through the region — over steps, along the bed — with no authored springs anywhere.
TEST(WaterManagerTest, RiverInflowFlowsDownhillThroughTheRegion) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    // A descending staircase bed along x at z=16: solid shelves at y=6, y=4, y=2.
    for (int x = 0; x <= 9; ++x)  wm.setSolidWorld(x, 6, 16, true);
    for (int x = 10; x <= 19; ++x) wm.setSolidWorld(x, 4, 16, true);
    for (int x = 20; x <= 31; ++x) wm.setSolidWorld(x, 2, 16, true);
    // Channel walls so the flow follows the bed instead of spilling sideways off the shelves.
    for (int x = 0; x <= 31; ++x)
        for (int y = 2; y <= 8; ++y) { wm.setSolidWorld(x, y, 15, true); wm.setSolidWorld(x, y, 17, true); }

    wm.setRiverQuery([](float, float wz) { return (wz >= 16.0f && wz < 17.0f) ? 1.0f : 0.0f; });
    wm.update(0.1f);   // rebuild: bed tags + recessed-bed pins

    EXPECT_TRUE(wm.sim().isChannel(0, 7, 16)) << "river bed cell not channel-tagged";
    ASSERT_GT(wm.totalMass(), 0.5f) << "edge inflow pin did not inject any water";

    for (int i = 0; i < 150; ++i) wm.update(0.2f);   // let it run downstream (~600 steps)
    EXPECT_GT(wm.massAtWorld(glm::vec3(25.5f, 3.5f, 16.5f)), 0.1f)
        << "river water never reached the downstream shelf — inflow/flow broken";
    EXPECT_LT(wm.massAtWorld(glm::vec3(5.5f, 12.5f, 16.5f)), 1e-3f)
        << "water appeared far ABOVE the channel bed";
}

// River flow TUNING: the ribbon is pinned full inside its recessed carve; where the carve is
// BREACHED (a bake-vs-terrain mismatch — the bank sits below the water), the pins pour water out.
// With evaporation OFF that pour accumulates without bound (the observed rising-pool defect); with
// evaporation ON the thin spill dries and the pour reaches a bounded equilibrium while the ribbon
// itself stays full end-to-end. Both halves in one scenario so the contrast is the red/green.
TEST(WaterManagerTest, RiverWithEvaporationReachesBoundedSteadyStateWithWetBed) {
    auto build = [](WaterManager& wm) {
        // A recessed channel along x at z=16 (slab + 1-high lips on both banks), stepping down,
        // with a 3-column BREACH in the z=17 lip at x 14..16 — the mismatch the CA leaks through.
        auto shelf = [&](int x0, int x1, int ySlab) {
            for (int x = x0; x <= x1; ++x) {
                wm.setSolidWorld(x, ySlab, 16, true);           // channel slab (bed sits on it)
                wm.setSolidWorld(x, ySlab, 15, true);           // bank support
                wm.setSolidWorld(x, ySlab, 17, true);
                wm.setSolidWorld(x, ySlab + 1, 15, true);       // z=15 lip (intact bank)
                if (x < 14 || x > 16)
                    wm.setSolidWorld(x, ySlab + 1, 17, true);   // z=17 lip, breached at 14..16
            }
        };
        shelf(0, 9, 6); shelf(10, 19, 4); shelf(20, 31, 2);
        // Centerline carve depth 1.0 (recessed → pinned); the z=17 bank is on the parabolic band
        // EDGE (depth 0.3, not recessed) — tagged but NOT pinned, else the bank itself becomes a
        // full water source and the valley floods by construction.
        wm.setRiverQuery([](float, float wz) {
            if (wz >= 16.0f && wz < 17.0f) return 1.0f;
            if (wz >= 17.0f && wz < 18.0f) return 0.3f;
            return 0.0f;
        });
    };

    // Half 1 (the defect): evaporation off → the breach pour accumulates without bound.
    WaterManager without(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    build(without);
    for (int i = 0; i < 100; ++i) without.update(0.2f);
    const float m1 = without.totalMass();
    for (int i = 0; i < 100; ++i) without.update(0.2f);
    const float growthWithout = without.totalMass() - m1;
    EXPECT_GT(growthWithout, 5.0f)
        << "expected the untuned river to keep growing through the breach (rising-pool defect)";

    // Half 2: the pour is BOUNDED — post-MIN_HOLD, by GEOMETRY rather than evaporation.
    // ⚠ SPEC CHANGE (small-scale plan Phase 1): pre-gate, the breach pour spread as a thin film
    // whose sub-0.1 frontier evaporated on arrival — "evaporation bounds off-channel spill"
    // held within ~400 steps. With the hold, spilled water rests at visible depth, and a LINE
    // source is not rim-bounded (rim capacity doesn't grow with extent), so a breach now does
    // the physical thing: it FILLS the downhill shelves toward their spill/pin level and then
    // STOPS. The guarantee worth pinning is that a true steady state exists — growth decays to
    // ~zero once the containment fills — plus the unchanged ribbon/bank assertions below.
    WaterManager with(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    build(with);
    with.setEvaporation(true);
    float lastGrowth = 1e9f;
    int windows = 0;
    for (; windows < 14 && lastGrowth >= 1.0f; ++windows) {
        const float before = with.totalMass();
        for (int i = 0; i < 250; ++i) with.update(0.2f);
        lastGrowth = with.totalMass() - before;
    }
    EXPECT_LT(lastGrowth, 1.0f)
        << "breach pour never saturated its containment (" << windows
        << " windows, last growth " << lastGrowth << ")";
    EXPECT_GT(with.massAtWorld(glm::vec3(0.5f, 7.5f, 16.5f)), 0.5f)
        << "the river bed dried out at the top of the course";
    EXPECT_GT(with.massAtWorld(glm::vec3(15.5f, 5.5f, 16.5f)), 0.5f)
        << "the ribbon is not full MID-course (at the breach) — bed pins must span the channel";
    EXPECT_GT(with.massAtWorld(glm::vec3(30.5f, 3.5f, 16.5f)), 0.5f)
        << "the ribbon is not full at the BOTTOM of the course";
    // The non-recessed band edge (carve depth 0.3 on the z=17 bank) must NOT be a pinned source:
    // its bed cell sits ON the bank top, and pinning it floods the valley by construction.
    EXPECT_LT(with.massAtWorld(glm::vec3(5.5f, 8.5f, 17.5f)), 0.5f)
        << "a non-recessed band-edge column was pinned — full water standing on the bank";
}

// ── CREEKS (small-scale plan Phase 2a) ────────────────────────────────────────────────────────
// Orders 1-2 were pure labels: four separate gates kept them bone dry, and the one attempt to
// open them (496cdc10) pinned FULL voxels into channels with no bed and flooded a hillside —
// reverted. This is the re-opening with both fixes in place: the pin is FRACTIONAL and clamped
// to the sim's MIN_HOLD, so a pinned creek cell can never make a horizontal transfer. The worst
// case is a static ribbon, never a sheet. RED before the order-aware pin mapping: an order-2
// creek's carve depth (0.66) is below the legacy 0.5→full-pin threshold's intent but above the
// threshold itself — under the OLD mapping it would FULL-PIN (1.0 > hold → donates → spreads);
// an order-1 creek (0.33 < 0.5) pins nothing at all and stays dry. Both wrong, both caught here.
TEST(WaterManagerTest, CreekPinIsFractionalAndConfined) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(24, 8, 24));
    for (int z = 0; z < 24; ++z)          // flat solid ground at y=2 — no bed recess AT ALL,
        for (int x = 0; x < 24; ++x)      // the exact geometry that made the full pin flood
            wm.setSolidWorld(x, 2, z, true);
    // An order-2 creek along x at z=12.5: parabolic band, half-width 1.5, centreline depth 0.66.
    wm.setRiverQuery([](float, float wz) {
        const float d = std::fabs(wz - 12.5f);
        if (d >= 1.5f) return 0.0f;
        const float t = d / 1.5f;
        return 0.66f * (1.0f - t * t);
    });
    wm.setRiverOrderQuery([](float, float) { return 2; });

    for (int i = 0; i < 200; ++i) wm.update(0.2f);

    const float hold = wm.sim().minHold();
    // Bed cells (y=3, on the solid ground): wet, fractional, at/below the hold, channel-tagged.
    for (int x = 4; x <= 20; x += 4) {
        const float m = wm.massAtWorld(glm::vec3(x + 0.5f, 3.5f, 12.5f));
        EXPECT_GT(m, 0.1f)  << "creek bed dry at x=" << x << " — the order gates are back";
        EXPECT_LE(m, hold + 1e-4f)
            << "creek pinned ABOVE the hold at x=" << x << " — the 496cdc10 flood risk";
    }
    // CONFINEMENT — the anti-hillside-sheet assertion. No evaporation is enabled here: the bound
    // must come from the hold alone. Off-band columns must be EXACTLY dry.
    for (int z = 0; z < 24; ++z) {
        if (std::fabs((z + 0.5f) - 12.5f) < 2.5f) continue;   // the creek band ± a cell
        for (int x = 0; x < 24; ++x)
            for (int y = 3; y < 8; ++y)
                ASSERT_EQ(wm.massAtWorld(glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f)), 0.0f)
                    << "creek water escaped the band at (" << x << "," << y << "," << z << ")";
    }
    // And the ribbon is STATIC: total mass constant over another observation window.
    const float before = wm.totalMass();
    for (int i = 0; i < 200; ++i) wm.update(0.2f);
    EXPECT_NEAR(wm.totalMass(), before, 1e-3f) << "creek ribbon is not at rest";
}

// River columns whose terrain isn't loaded yet (no real solid below anywhere in the column) get
// neither a channel tag nor an inflow pin — otherwise the river would pour into the void at the
// region floor wherever chunks haven't streamed in yet.
// ── sampleWater / submergedFraction (small-scale plan Phase 4.1) ──────────────────────────────
// The entity-facing water query: fill-fraction + floor aware inside the region, table/implicit-
// sea fallback outside, honest about dry. This is the foundation buoyancy/wading/fog build on.

TEST(WaterManagerTest, SampleWaterReadsFractionalFillAndStackedColumns) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 12, 16));
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) wm.setSolidWorld(x, 0, z, true);
    // Walled 2x2 pool holding a 2.4-deep body: y1 full, y2 full, y3 at 0.4.
    for (int y = 1; y < 8; ++y)
        for (int i = 3; i <= 6; ++i) {
            wm.setSolidWorld(i, y, 3, true); wm.setSolidWorld(i, y, 6, true);
            wm.setSolidWorld(3, y, i, true); wm.setSolidWorld(6, y, i, true);
        }
    wm.placeWater(glm::vec3(4.5f, 2.5f, 4.5f), 9.6f);   // 9.6 over the 4 interior columns → 2.4 each
    for (int i = 0; i < 400; ++i) wm.update(0.2f);

    // Fractional top cell: surface at ~3.4; probe inside the top cell.
    const auto top = wm.sampleWater(glm::vec3(4.5f, 3.1f, 4.5f));
    EXPECT_TRUE(top.inWater);
    EXPECT_NEAR(top.surfaceY, 3.4f, 0.1f);
    EXPECT_NEAR(top.depthBelow, 0.3f, 0.1f);
    // Deep probe in the full bottom cell: the walk must climb the stack to the same surface.
    const auto deep = wm.sampleWater(glm::vec3(4.5f, 1.2f, 4.5f));
    EXPECT_TRUE(deep.inWater);
    EXPECT_NEAR(deep.surfaceY, 3.4f, 0.1f);
    EXPECT_NEAR(deep.depthBelow, 2.2f, 0.15f);
    // Dry point just above the surface, and dry land outside the pool.
    EXPECT_FALSE(wm.sampleWater(glm::vec3(4.5f, 3.6f, 4.5f)).inWater);
    EXPECT_FALSE(wm.sampleWater(glm::vec3(10.5f, 1.5f, 10.5f)).inWater);
}

TEST(WaterManagerTest, SampleWaterRespectsSubVoxelFloors) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(8, 8, 8));
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) wm.setSolidWorld(x, 0, z, true);
    for (int y = 1; y < 5; ++y)     // walls around one cell so the film can't creep
        for (int i = 2; i <= 5; ++i) {
            wm.setSolidWorld(i, y, 2, true); wm.setSolidWorld(i, y, 5, true);
            wm.setSolidWorld(2, y, i, true); wm.setSolidWorld(5, y, i, true);
        }
    wm.setFloorWorld(3, 1, 3, 1.0f / 3.0f);            // a subcube step under the water
    wm.placeWater(glm::vec3(3.5f, 1.8f, 3.5f), 0.3f);  // resting film ON the step
    for (int i = 0; i < 200; ++i) wm.update(0.2f);

    // Surface = floor + fill·(1−floor) = 1/3 + 0.3·(2/3) = 0.533 up the cell.
    const auto s = wm.sampleWater(glm::vec3(3.5f, 1.4f, 3.5f));
    EXPECT_TRUE(s.inWater);
    EXPECT_NEAR(s.surfaceY, 1.0f + (1.0f / 3.0f) + 0.3f * (2.0f / 3.0f), 0.05f);
}

TEST(WaterManagerTest, SampleWaterFallsBackToTableThenImplicitSeaOutsideRegion) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(8, 8, 8));
    // No table, implicit sea OFF: a far point below sea level must read DRY (waterless world).
    EXPECT_FALSE(wm.sampleWater(glm::vec3(500.0f, 5.0f, 500.0f)).inWater);
    // Implicit sea ON: below-sea far points submerge to the scalar level.
    wm.setSeaLevel(20.0f);
    wm.setImplicitSea(true);
    const auto sea = wm.sampleWater(glm::vec3(500.0f, 5.0f, 500.0f));
    EXPECT_TRUE(sea.inWater);
    EXPECT_FLOAT_EQ(sea.surfaceY, 20.0f);
    EXPECT_FLOAT_EQ(sea.depthBelow, 15.0f);
    EXPECT_FALSE(wm.sampleWater(glm::vec3(500.0f, 25.0f, 500.0f)).inWater);
    // A bound baked table supersedes the scalar: per-column levels answer far queries.
    wm.setWaterTable([](float wx, float) { return wx > 400.0f ? 60.0f : -1e30f; });
    const auto lake = wm.sampleWater(glm::vec3(500.0f, 5.0f, 500.0f));
    EXPECT_TRUE(lake.inWater);
    EXPECT_FLOAT_EQ(lake.surfaceY, 60.0f);
    EXPECT_FALSE(wm.sampleWater(glm::vec3(100.0f, 5.0f, 100.0f)).inWater)
        << "table says dry → dry, even below the scalar sea level";
}

TEST(WaterManagerTest, SubmergedFractionScalesWithImmersion) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(8, 8, 8));
    wm.setSeaLevel(20.0f);
    wm.setImplicitSea(true);
    // Out-of-region unit boxes against the flat sea: fully under, half under, dry.
    EXPECT_NEAR(wm.submergedFraction(glm::vec3(500, 10, 500), glm::vec3(501, 11, 501)), 1.0f, 1e-4f);
    EXPECT_NEAR(wm.submergedFraction(glm::vec3(500, 19.5f, 500), glm::vec3(501, 20.5f, 501)), 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(wm.submergedFraction(glm::vec3(500, 30, 500), glm::vec3(501, 31, 501)), 0.0f);
}

TEST(WaterManagerTest, RiverColumnsWithoutLoadedTerrainAreSkipped) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 16, 16));
    wm.setRiverQuery([](float, float) { return 1.0f; });   // "river everywhere" — but no terrain
    for (int i = 0; i < 10; ++i) wm.update(0.1f);
    EXPECT_FLOAT_EQ(wm.totalMass(), 0.0f)
        << "river inflow pinned into unloaded columns — water pouring into the void";
}

// ─── Runtime shoreline SNAP (the L3 rim-leak fix, water side) ─────────────────────────────────────
// The coarse bake's wet/dry boundary sits far from the carved waterline; the snap expands each wet
// level into adjacent baked-DRY columns whose in-band terrain top is BELOW that level, stopping at
// terrain that rises to/above it. Snapped columns become PINNED water (sources) — the structural
// difference from the old rim leak, whose water was unpinned creep that churned forever.
namespace {
bool isPinnedAtLocal(const WaterManager& wm, int lx, int ly, int lz) {
    const auto& d = wm.dims();
    const size_t i = static_cast<size_t>(lx) +
                     static_cast<size_t>(d.x) * (static_cast<size_t>(ly) +
                     static_cast<size_t>(d.y) * static_cast<size_t>(lz));
    return wm.sim().sourceMask()[i] >= 0.0f;
}
}  // namespace

// Vertical following is WATER-AWARE: if the baked table has water under the new footprint, the
// band stays centered no higher than that water's surface — a viewer on a coastal clifftop must
// not lift the box above the sea (measured live: camera y=45 at a shore → band 29..61, sea level
// 16 below it, simulated ocean mass 0). Dry footprints still follow the camera (mountain rivers).
TEST(WaterManagerTest, FollowToKeepsTheBandOnLocalWater) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    wm.setWaterTable([](float, float) { return 16.0f; });   // sea everywhere at level 16
    // Clifftop viewer: focus y=45 would put the band at 37..53 — the clamp must hold it on the
    // water instead (origin.y = floor(16) - dims.y/2 = 8 → band 8..24 covers the surface).
    EXPECT_TRUE(wm.followTo(glm::vec3(100.0f, 45.0f, 16.0f), 8));
    EXPECT_EQ(wm.origin(), glm::ivec3(84, 8, 0));
    // Re-calling with the same focus must not thrash (clamp lands on the current origin).
    EXPECT_FALSE(wm.followTo(glm::vec3(100.0f, 45.0f, 16.0f), 8));

    // Dry footprint (no table water) keeps pure camera-following.
    WaterManager dry(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    dry.setWaterTable([](float, float) { return -1e30f; });
    EXPECT_TRUE(dry.followTo(glm::vec3(100.0f, 72.0f, 16.0f), 8));
    EXPECT_EQ(dry.origin(), glm::ivec3(84, 64, 0));
}

TEST(WaterManagerTest, ShorelineSnapPinsLowRimAndStopsAtHighGround) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    // A low shelf (top y=5) spans x 10..14 beside the baked-wet zone; a high wall (top y=12) at
    // x=15; nothing beyond it. The bake calls everything x >= 10 DRY (the coarse boundary).
    for (int x = 10; x <= 14; ++x)
        for (int z = 0; z < 32; ++z) wm.setSolidWorld(x, 5, z, true);
    for (int z = 0; z < 32; ++z)
        for (int y = 0; y <= 12; ++y) wm.setSolidWorld(15, y, z, true);
    wm.setWaterTable([](float wx, float) { return wx < 10.0f ? 8.0f : -1e30f; });
    wm.update(0.1f);

    // The shelf snapped: wet AND PINNED at the level, dry above it.
    EXPECT_GT(wm.massAtWorld(glm::vec3(12.5f, 8.5f, 16.5f)), 0.9f) << "snapped rim not wet at level";
    EXPECT_TRUE(isPinnedAtLocal(wm, 12, 8, 16))
        << "rim water is unpinned creep, not snapped table water";
    EXPECT_LT(wm.massAtWorld(glm::vec3(12.5f, 9.5f, 16.5f)), 1e-3f) << "water above the level";
    // Contained at the high wall — the snap must NOT climb terrain at/above the level.
    EXPECT_FALSE(isPinnedAtLocal(wm, 15, 8, 16));
    EXPECT_LT(wm.massAtWorld(glm::vec3(16.5f, 8.5f, 16.5f)), 1e-3f) << "water beyond the high wall";
    // A properly-pinned shore SETTLES (the old rim leak churned indefinitely).
    int guard = 0;
    while (!wm.sim().settled() && guard++ < 200) wm.update(0.1f);
    EXPECT_TRUE(wm.sim().settled()) << "snapped shoreline still churning";
}

// Columns with no in-band solid (void / not-yet-streamed terrain) are never snapped — pinning them
// would hang floating water in the void, the same failure class the river bed guard prevents.
TEST(WaterManagerTest, ShorelineSnapSkipsVoidColumns) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    wm.setWaterTable([](float wx, float) { return wx < 10.0f ? 8.0f : -1e30f; });
    wm.update(0.1f);   // wet zone pins; everything x >= 10 is bottomless void
    EXPECT_FALSE(isPinnedAtLocal(wm, 12, 8, 16))
        << "a void column was snapped — floating pinned water";
    EXPECT_FALSE(isPinnedAtLocal(wm, 30, 8, 16));
}

// ─── L3 bake-vs-terrain validation: rim leaks ─────────────────────────────────────────────────────
// The validator flags exactly the observed live defect: a baked-DRY rim column whose carved surface
// sits BELOW the adjacent water level — the CA levels water into it, so the lake/sea spreads beyond
// its baked extent. A properly-carved rim (surface at/above the level) is NOT flagged.
TEST(WaterManagerTest, ValidateTableFlagsCarvedRimLeaksOnly) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(0, 0, 0));
    owned->initializeForLoading();
    // A lake basin: interior columns x,z in [10,13] with floor at y=5 (under water, level 8);
    // rim ring x,z in [9,14] \ interior at y=10 (contains); everything else solid to y=10.
    // ONE rim column (9,12) carved down to y=6 — below the level → the leak.
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) {
            const bool interior = (x >= 10 && x <= 13 && z >= 10 && z <= 13);
            int top = interior ? 5 : 10;
            if (x == 9 && z == 12) top = 6;   // the carved (leaky) rim column
            for (int y = 0; y <= top; ++y) owned->addCube(glm::ivec3(x, y, z));
        }
    cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
    cm.chunks.push_back(std::move(owned));

    WaterManager wm(&cm, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    wm.setWaterTable([](float wx, float wz) -> float {
        return (wx >= 10.0f && wx < 14.0f && wz >= 10.0f && wz < 14.0f) ? 8.0f : -1e30f;
    });

    const auto v = wm.validateTable(glm::ivec2(5, 5), glm::ivec2(18, 18), 31);
    EXPECT_EQ(v.unloaded, 0);
    EXPECT_EQ(v.wet, 16) << "the 4x4 lake interior should be baked-wet";
    EXPECT_GT(v.rim, 0) << "the ring around the lake should count as rim";
    EXPECT_EQ(v.rimLeaks, 1) << "exactly the one carved rim column should be flagged";
    EXPECT_EQ(v.worstLeakAt, glm::ivec2(9, 12));
    EXPECT_FLOAT_EQ(v.worstLeakDepth, 2.0f) << "floor(8) - surface 6 = 2";
}

// ─── Stale-solid window (streamed chunks → water solidity) ────────────────────────────────────────
// A chunk that streams in INSIDE the water region must push its solids into the sim: without it,
// water flooded where terrain later loads stays INSIDE that terrain until the next recenter happens
// to re-read solidity (the stale-solid window). ChunkManager::syncChunkToOccupancy is the per-chunk
// push the streaming pump's onChunkLoaded hook calls; here we drive it exactly the way the pump
// does, against a REAL ChunkManager wired to the manager like the editor wires it.
TEST(WaterManagerTest, StreamedInTerrainDrainsTheWaterItDisplaced) {
    ChunkManager cm;
    // The subsystem callbacks (incl. the chunk-map accessor every voxel query needs) are wired in
    // initialize(), not the constructor — null Vulkan handles are fine headless (nothing in the
    // wiring touches the device; only chunk-buffer creation would, and we bypass it).
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    // Terrain that exists in the world before the water sim knows about it: a solid 8×8 island
    // block, world x,z in [8,15], y in [0,7]. Built headless and inserted directly (the
    // createChunk path needs a Vulkan device), exactly what a not-yet-streamed chunk looks like.
    auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(0, 0, 0));
    owned->initializeForLoading();
    for (int x = 8; x <= 15; ++x)
        for (int z = 8; z <= 15; ++z)
            for (int y = 0; y <= 7; ++y) owned->addCube(glm::ivec3(x, y, z));
    cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
    cm.chunks.push_back(std::move(owned));

    WaterManager wm(&cm, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    cm.setVoxelOccupancyCallback([&wm](int x, int y, int z, bool solid) {
        wm.setSolidWorld(x, y, z, solid);
    });
    // NOTE: the sim's solidity was synced at construction (syncSolidsFromChunks reads the chunk),
    // so to reproduce the STALE state we flood first with an empty mask: clear the island cells.
    for (int x = 8; x <= 15; ++x)
        for (int z = 8; z <= 15; ++z)
            for (int y = 0; y <= 7; ++y) wm.setSolidWorld(x, y, z, false);

    wm.setWaterTable([](float, float) { return 6.0f; });   // uniform sea, level y=6
    wm.update(0.1f);
    ASSERT_GT(wm.totalMass(), 1000.0f) << "sea should have flooded";
    ASSERT_GT(wm.massAtWorld(glm::vec3(10.5f, 3.5f, 10.5f)), 0.5f)
        << "precondition: water should sit where the island terrain exists (the stale window)";

    // The chunk "streams in": the pump pushes its solids at every occupancy consumer.
    cm.syncChunkToOccupancy(glm::ivec3(0, 0, 0));
    wm.update(0.1f);   // solidity change dirtied the ocean → re-derive + step

    EXPECT_LT(wm.massAtWorld(glm::vec3(10.5f, 3.5f, 10.5f)), 1e-3f)
        << "water still INSIDE streamed-in terrain — the stale-solid window is back";
    EXPECT_GT(wm.massAtWorld(glm::vec3(3.5f, 3.5f, 3.5f)), 0.5f)
        << "open sea next to the island should still be flooded";
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
            // focus y=8 = box centre → inside the vertical dead zone (pure horizontal stress)
            const bool moved = wm.followTo(glm::vec3(fx, 8.0f, 16.5f), 4);
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
        // focus y=8 = box centre → vertical dead zone holds (pure horizontal long walk)
        if (!wm.followTo(glm::vec3(fx, 8.0f, 8.0f), 4)) continue;
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

// ── SUB-VOXEL FLOOR (WaterSystemV3 Phase 4B) ──────────────────────────────────────────────────
// A voxel holding a low subcube/microcube platform is passable, and water rests ON that platform.
// These use setFloor directly (the ChunkManager query that derives it in the live engine is
// exercised by ChunkVoxelManager's own coverage; here the concern is what the RENDERER draws).

// Water over a 1/3-height platform must render 1/3 of a voxel higher than the same water over a
// bare cell — that difference IS the feature.
TEST(WaterManagerTest, SubVoxelFloorRaisesTheRenderedSurface) {
    auto surfaceOverFloor = [](float floorFraction) {
        WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
        // A walled 2x2 basin resting on the out-of-bounds floor below y=0.
        for (int y = 0; y <= 3; ++y) {
            for (int i = 4; i <= 7; ++i) {
                wm.setSolidWorld(4, y, i, true); wm.setSolidWorld(7, y, i, true);
                wm.setSolidWorld(i, y, 4, true); wm.setSolidWorld(i, y, 7, true);
            }
        }
        wm.setFloorWorld(5, 0, 5, floorFraction);
        wm.setFloorWorld(6, 0, 5, floorFraction);
        wm.setFloorWorld(5, 0, 6, floorFraction);
        wm.setFloorWorld(6, 0, 6, floorFraction);
        for (int x = 5; x <= 6; ++x)
            for (int z = 5; z <= 6; ++z) wm.placeWater(glm::vec3(x + 0.5f, 0.5f, z + 0.5f), 0.5f);
        for (int i = 0; i < 30; ++i) wm.update(0.1f);

        float top = -1e9f;
        for (const auto& c : wm.surfaceCells()) top = std::max(top, c.centerDepth.y);
        return top;
    };

    const float bare = surfaceOverFloor(0.0f);
    const float third = surfaceOverFloor(1.0f / 3.0f);
    ASSERT_GT(bare, -1e8f) << "no surface emitted over the bare cell";
    ASSERT_GT(third, -1e8f) << "no surface emitted over the platform";
    EXPECT_GT(third, bare) << "the platform did not raise the water surface at all";

    // Closed form: the surface sits at floor + fill*(1-floor), so the lift over a bare cell is
    // floor*(1-fill). The water's fill fraction is scaled into the space ABOVE the platform, which
    // is what keeps a full cell rendering at the cell top and an empty one at the platform top.
    // (fill here is 0.5 => a 1/3 platform lifts by 1/6, not 1/3.)
    const float fill = 0.5f, floorFrac = 1.0f / 3.0f;
    EXPECT_NEAR(third - bare, floorFrac * (1.0f - fill), 0.02f)
        << "lift should be floor*(1-fill) (bare " << bare << ", floored " << third << ")";
}

// The case the feature exists for: a SHALLOW puddle on a low step must sit on the step, not most of
// a voxel above it. As the water thins the lift approaches the platform's full height.
TEST(WaterManagerTest, SubVoxelFloorLiftsAShallowPuddleOntoTheStep) {
    auto surfaceForThinFilm = [](float floorFraction) {
        WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
        for (int y = 0; y <= 3; ++y)
            for (int i = 4; i <= 7; ++i) {
                wm.setSolidWorld(4, y, i, true); wm.setSolidWorld(7, y, i, true);
                wm.setSolidWorld(i, y, 4, true); wm.setSolidWorld(i, y, 7, true);
            }
        for (int x = 5; x <= 6; ++x)
            for (int z = 5; z <= 6; ++z) wm.setFloorWorld(x, 0, z, floorFraction);
        for (int x = 5; x <= 6; ++x)
            for (int z = 5; z <= 6; ++z) wm.placeWater(glm::vec3(x + 0.5f, 0.5f, z + 0.5f), 0.10f);
        for (int i = 0; i < 30; ++i) wm.update(0.1f);
        float top = -1e9f;
        for (const auto& c : wm.surfaceCells()) top = std::max(top, c.centerDepth.y);
        return top;
    };

    const float onStep = surfaceForThinFilm(2.0f / 3.0f);
    ASSERT_GT(onStep, -1e8f) << "no surface emitted for the thin film";
    // A 2/3 step under a ~0.1-deep film: the surface must be up on the step (>= 2/3), not near the
    // voxel's base where it would render as though the step were not there.
    EXPECT_GT(onStep, 2.0f / 3.0f)
        << "a thin puddle did not sit on top of the 2/3 step (surface " << onStep << ")";
    EXPECT_LT(onStep, 1.0f) << "the puddle floated above the voxel entirely (surface " << onStep << ")";
}

// The floor is terrain-derived, so like solidity it must travel with the window when the region
// recenters — otherwise walking past a platform would drop its water through the floor.
TEST(WaterManagerTest, SubVoxelFloorSurvivesRecenter) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(16, 8, 16));
    wm.setFloorWorld(8, 2, 8, 2.0f / 3.0f);
    ASSERT_NEAR(wm.floorAtWorld(glm::vec3(8.5f, 2.5f, 8.5f)), 2.0f / 3.0f, 1e-4f);

    wm.recenter(glm::ivec3(4, 0, 4));   // window moves; the world content must stay put
    EXPECT_NEAR(wm.floorAtWorld(glm::vec3(8.5f, 2.5f, 8.5f)), 2.0f / 3.0f, 1e-4f)
        << "the sub-voxel floor did not travel with the region";
}

// ── KINEMATIC RIVER FLOW (WaterSystemV3 Phase 3) ──────────────────────────────────────────────
//
// A baked river is PINNED FULL along its whole carve, so it performs no transfers and the CA's flow
// proxy reads exactly zero — left alone, a river would shade as a long thin lake. WaterManager
// therefore stamps the BAKE's downhill direction onto river surface cells instead. These tests pin
// that stamping, which is the part this engine owns; the hydrology bake that feeds it in a real
// world is FlowField's own concern (and is covered by FlowFieldTest).
namespace {

// A channel one cell wide running along +x at z = 20, bed at y = 3, with banks either side, so the
// water is confined and the surface cells are unambiguous.
void buildRiverChannel(WaterManager& wm) {
    for (int x = 8; x <= 26; ++x) {
        for (int y = 0; y <= 5; ++y) {
            wm.setSolidWorld(x, y, 19, true);   // bank -z
            wm.setSolidWorld(x, y, 21, true);   // bank +z
        }
        wm.setSolidWorld(x, 2, 20, true);       // bed
    }
}

// Find the emitted surface cell whose centre is at this world column (cells carry world centres at
// +0.5). Returns false if the column emitted nothing.
bool surfaceCellAt(const WaterManager& wm, int wx, int wz, Phyxel::Core::WaterSurfaceCell& out) {
    for (const auto& c : wm.surfaceCells())
        if (std::fabs(c.centerDepth.x - (wx + 0.5f)) < 0.01f &&
            std::fabs(c.centerDepth.z - (wz + 0.5f)) < 0.01f) { out = c; return true; }
    return false;
}

}  // namespace

// The bake says "this column is a channel flowing +x"; the rendered surface cell must report that
// direction, even though the CA itself moved no mass there.
TEST(WaterManagerTest, RiverFlowQueryStampsBakedDirectionOnPinnedRiver) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildRiverChannel(wm);
    // Bake stubs: recessed channel along z=20 for x in [8,26], draining toward +x.
    auto inChannel = [](float wx, float wz) {
        return wz >= 20.0f && wz < 21.0f && wx >= 8.0f && wx < 27.0f;
    };
    wm.setRiverQuery([inChannel](float wx, float wz) { return inChannel(wx, wz) ? 1.0f : 0.0f; });
    wm.setRiverFlowQuery([inChannel](float wx, float wz) {
        return inChannel(wx, wz) ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f);
    });
    for (int i = 0; i < 40; ++i) wm.update(0.1f);

    Phyxel::Core::WaterSurfaceCell cell{};
    ASSERT_TRUE(surfaceCellAt(wm, 17, 20, cell)) << "the pinned river emitted no surface cell";
    EXPECT_GT(cell.flow.x, 0.9f)  << "river cell should report the baked +x direction";
    EXPECT_NEAR(cell.flow.y, 0.0f, 1e-3f);
    EXPECT_GT(cell.flow.z, 0.1f)  << "river cell should report a non-zero flow STRENGTH; a pinned "
                                     "river derives none from the CA, so the bake must supply it";
}

// The stamp must be confined to the channel: a still pool elsewhere must NOT pick up a river
// direction, or every pond in a world with rivers would shade as though it were flowing.
TEST(WaterManagerTest, RiverFlowQueryDoesNotTouchWaterOffTheChannel) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildBasinAndFill(wm);                       // a settled pool at x,z in [12,15]
    // A channel that exists far away, nowhere near the basin.
    wm.setRiverQuery([](float, float wz) { return (wz >= 25.0f && wz < 26.0f) ? 1.0f : 0.0f; });
    wm.setRiverFlowQuery([](float, float wz) {
        return (wz >= 25.0f && wz < 26.0f) ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f);
    });
    for (int i = 0; i < 20; ++i) wm.update(0.1f);

    Phyxel::Core::WaterSurfaceCell cell{};
    ASSERT_TRUE(surfaceCellAt(wm, 13, 13, cell)) << "the settled pool emitted no surface cell";
    EXPECT_LT(cell.flow.z, 0.05f)
        << "a still pool away from any channel must report no flow (got strength " << cell.flow.z
        << ", dir " << cell.flow.x << "," << cell.flow.y << ")";
}

// Unbinding the query must remove the stamp — otherwise switching to a world without rivers would
// leave the previous world's currents shading the water.
TEST(WaterManagerTest, ClearingRiverFlowQueryRemovesTheStamp) {
    WaterManager wm(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 16, 32));
    buildRiverChannel(wm);
    auto inChannel = [](float wx, float wz) {
        return wz >= 20.0f && wz < 21.0f && wx >= 8.0f && wx < 27.0f;
    };
    wm.setRiverQuery([inChannel](float wx, float wz) { return inChannel(wx, wz) ? 1.0f : 0.0f; });
    wm.setRiverFlowQuery([inChannel](float wx, float wz) {
        return inChannel(wx, wz) ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f);
    });
    for (int i = 0; i < 40; ++i) wm.update(0.1f);
    Phyxel::Core::WaterSurfaceCell before{};
    ASSERT_TRUE(surfaceCellAt(wm, 17, 20, before));
    ASSERT_GT(before.flow.z, 0.1f) << "precondition: the river must be stamped first";

    wm.setRiverFlowQuery(nullptr);
    Phyxel::Core::WaterSurfaceCell after{};
    ASSERT_TRUE(surfaceCellAt(wm, 17, 20, after));
    EXPECT_LT(after.flow.z, 0.05f) << "unbinding left the river direction stamped";
}
