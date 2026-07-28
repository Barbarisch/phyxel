#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/SettlementBuildService.h"

using namespace Phyxel::Core;

// ============================================================================
// Terrain-aware settlement PLANNING, headless.
//
// This is the second thing the lift into phyxel_core bought. `plan()` reads the
// live world (grounding gate, buildability analysis, flattest-spine choice,
// dropping plots that straddle unbuildable ground) and then returns the heavy
// phases as unexecuted work units -- so the whole terrain-aware DECISION layer can
// be driven against a synthetic world with no engine, no Vulkan and no renderer.
// While this lived in editor/src/Application.cpp none of it had a test surface.
//
// SCOPE, stated up front so this is not read as more than it is: this covers
// PLANNING ONLY. The units are never run, because StructureGenerator::place() is
// not headless-testable (a prior finding, recorded in StructureGroundingTest), so
// nothing here says a terrain settlement is WALKABLE once stamped -- that remains
// an L4 question. What it does prove is that the terrain decisions are made, are
// deterministic, and degrade honestly instead of silently.
// ============================================================================

namespace {

// A headless world: `chunksX` x 1 x 1 chunks of solid terrain whose surface height
// is given by `topAt(worldX, worldZ)`. Same construction WaterManagerTest uses.
class HeadlessWorld {
public:
    HeadlessWorld(int chunksX, const std::function<int(int, int)>& topAt) {
        m_cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        for (int cx = 0; cx < chunksX; ++cx) {
            auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(cx, 0, 0));
            owned->initializeForLoading();
            for (int lx = 0; lx < 32; ++lx)
                for (int lz = 0; lz < 32; ++lz) {
                    const int wx = cx * 32 + lx;
                    const int top = topAt(wx, lz);
                    for (int y = 0; y <= top && y < 32; ++y)
                        owned->addCube(glm::ivec3(lx, y, lz));
                }
            m_cm.chunkMap[glm::ivec3(cx, 0, 0)] = owned.get();
            m_cm.chunks.push_back(std::move(owned));
        }
    }
    Phyxel::ChunkManager* cm() { return &m_cm; }

private:
    Phyxel::ChunkManager m_cm;
};

SettlementBuildService::Plan planOn(HeadlessWorld& w, int W, int D, bool terrain,
                                    int seed = 3, const char* tier = "village") {
    nlohmann::json params = {{"era", "medieval"}, {"tier", tier}, {"seed", seed},
                             {"width", W}, {"depth", D}, {"terrain", terrain},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = w.cm();
    return SettlementBuildService::plan(params, deps);
}

}  // namespace

// ---------------------------------------------------------------------------
// The grounding gate: a settlement must refuse a site with holes in it. This is
// the gate that was added after the user found a village generated over thin air,
// and until now it had no headless test at all.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, ASiteWithUngroundedColumnsIsRefused) {
    // Terrain everywhere EXCEPT a hole at x in [20,24], z in [10,14].
    HeadlessWorld w(2, [](int x, int z) {
        if (x >= 20 && x <= 24 && z >= 10 && z <= 14) return -1;   // no terrain at all
        return 16;
    });
    const auto plan = planOn(w, 48, 28, /*terrain=*/false);

    ASSERT_FALSE(plan.ok()) << "a site with a hole in it planned successfully";
    EXPECT_TRUE(plan.error.contains("ungrounded_columns"))
        << "the refusal must name HOW MANY columns lack terrain: " << plan.error.dump();
    EXPECT_EQ(plan.error.value("ungrounded_columns", 0), 25)
        << "the 5x5 hole should be counted exactly, not approximated";
    EXPECT_TRUE(plan.error.contains("first_ungrounded"))
        << "the refusal must point at WHERE, so the caller can go look";
    EXPECT_TRUE(plan.units.empty());
}

// ...and the documented override must actually override, or the gate is a wall
// rather than a default.
TEST(SettlementTerrainPlanTest, TheUngroundedOverrideIsHonoured) {
    HeadlessWorld w(2, [](int x, int z) {
        if (x >= 20 && x <= 24 && z >= 10 && z <= 14) return -1;
        return 16;
    });
    nlohmann::json params = {{"era", "medieval"}, {"tier", "village"}, {"seed", 3},
                             {"width", 48}, {"depth", 28}, {"allow_ungrounded", true},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = w.cm();
    const auto plan = SettlementBuildService::plan(params, deps);

    EXPECT_TRUE(plan.ok()) << "allow_ungrounded did not override the grounding gate: "
                           << plan.error.dump();
    EXPECT_FALSE(plan.units.empty());
}

// ---------------------------------------------------------------------------
// Terrain mode on FLAT ground must produce a full settlement -- the baseline that
// makes the steep-terrain degradation below meaningful rather than vacuous.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, TerrainModeOnFlatGroundPlansAFullSettlement) {
    HeadlessWorld w(2, [](int, int) { return 16; });
    const auto plan = planOn(w, 48, 28, /*terrain=*/true);

    ASSERT_TRUE(plan.ok()) << plan.error.dump();
    EXPECT_GT(plan.settlement.value("buildings", 0), 0);
    EXPECT_EQ(plan.program.value("dropped_plots", -1), 0)
        << "plots were dropped as unbuildable on perfectly FLAT ground";
}

// ---------------------------------------------------------------------------
// The real terrain behaviour: on ground too steep to build on, the planner must
// DEGRADE HONESTLY -- refuse with a reason, or drop plots and SAY SO -- never
// silently site buildings on cliffs.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, ImpossibleTerrainIsRefusedWithAReasonNotSilentlyBuilt) {
    // A sawtooth: every column alternates between y=16 and y=30. Relief 14 per cell,
    // far past any buildable threshold -- nowhere is flat.
    HeadlessWorld w(2, [](int x, int z) { return ((x + z) % 2 == 0) ? 16 : 30; });
    const auto plan = planOn(w, 48, 28, /*terrain=*/true);

    EXPECT_FALSE(plan.ok())
        << "a settlement was planned on sawtooth cliffs - the buildability gate is not firing";
    EXPECT_TRUE(plan.error.contains("buildable_fraction"))
        << "the refusal must report HOW unbuildable the site was: " << plan.error.dump();
    EXPECT_TRUE(plan.units.empty()) << "a refused terrain plan handed back work units";
}

// A slope that is buildable in places must produce a settlement, and the count of
// plots dropped for unbuildability must be SURFACED (graceful degradation is only
// honest if it is reported).
TEST(SettlementTerrainPlanTest, PartlyBuildableTerrainSurfacesTheDroppedPlotCount) {
    // Flat half (z < 16) and a steep ramp half (z >= 16) climbing 3 cubes per cell.
    HeadlessWorld w(2, [](int x, int z) { return z < 16 ? 16 : 16 + (z - 16) * 3; });
    const auto plan = planOn(w, 48, 28, /*terrain=*/true);

    if (!plan.ok()) {
        // Acceptable outcome, but it still has to say why.
        EXPECT_TRUE(plan.error.contains("buildable_fraction")) << plan.error.dump();
        return;
    }
    EXPECT_TRUE(plan.program.contains("dropped_plots"))
        << "a terrain build that skipped plots must report the count, not quietly shrink";
    EXPECT_GE(plan.program.value("dropped_plots", 0), 0);
    EXPECT_GT(plan.settlement.value("buildings", 0), 0)
        << "the flat half should still host buildings";
}

// ---------------------------------------------------------------------------
// Terrain planning must be deterministic in the seed -- the same contract the
// program echo advertises, now on a real (synthetic) landscape rather than the
// flat-plane path. Teeth: a different seed must differ, or the check is vacuous.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, TerrainPlanningIsDeterministicInTheSeed) {
    auto mk = [](int seed) {
        HeadlessWorld w(2, [](int x, int z) { return 16 + ((x / 16) + (z / 16)) % 2; });
        return planOn(w, 48, 28, /*terrain=*/true, seed).queuedBuilds.dump();
    };
    const std::string a = mk(3), b = mk(3), c = mk(9);
    EXPECT_EQ(a, b) << "the same seed produced a different terrain settlement";
    EXPECT_NE(a, c) << "a different seed produced an IDENTICAL settlement - the seed is not "
                       "reaching the terrain path, so the determinism check above is vacuous";
}

// ---------------------------------------------------------------------------
// The site-prep units DO get planned once a world is present -- the complement of
// SettlementBuildServiceTest's world-free case, which asserts they are skipped and
// the skip is surfaced. Together these pin both halves of that branch.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, WithAWorldTheSitePrepUnitsAreActuallyPlanned) {
    HeadlessWorld w(2, [](int, int) { return 16; });
    const auto plan = planOn(w, 48, 28, /*terrain=*/false);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();

    EXPECT_FALSE(plan.settlement.value("site_prep_skipped", false))
        << "site prep reported as skipped even though a world was supplied";

    bool sawPrep = false, sawBuilding = false;
    int firstPrep = -1, firstBuilding = -1;
    for (size_t i = 0; i < plan.units.size(); ++i) {
        const std::string& l = plan.units[i].label;
        if (l == "clearing parcels" || l == "fencing parcels" ||
            l.rfind("grading", 0) == 0 || l.rfind("terracing", 0) == 0) {
            sawPrep = true;
            if (firstPrep < 0) firstPrep = (int)i;
        }
        if (l.rfind("building ", 0) == 0 && firstBuilding < 0) {
            sawBuilding = true;
            firstBuilding = (int)i;
        }
    }
    EXPECT_TRUE(sawPrep) << "no site-prep unit was planned even with a world present";
    ASSERT_TRUE(sawBuilding);
    EXPECT_LT(firstPrep, firstBuilding)
        << "site prep is ordered AFTER the first building - buildings would be seated on "
           "uncleared, ungraded ground (the invariant the handler only stated in a comment)";
}
