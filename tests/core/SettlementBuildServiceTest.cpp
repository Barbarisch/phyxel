#include <gtest/gtest.h>

#include <set>
#include <string>

#include "core/SettlementBuildService.h"

using namespace Phyxel::Core;

// ============================================================================
// SettlementBuildService::plan() — the first test of the settlement COMPOSITION
// itself.
//
// This test could not exist a commit ago. The orchestrator lived inside the
// editor's build_settlement handler, so every settlement test could only exercise
// the pure planners it CALLS (planMainStreetLayout, planStreetPaving,
// planParcelFenceRuns...) and never the assembly that turns them into a build.
// That is the concrete capability the lift into phyxel_core bought, so it is what
// this file spends it on.
//
// The auditor's standing criticism of the L4 evidence applies here too: one live
// run covered ONE morphology (main-street/village). These cases cover the branch
// MATRIX -- hamlet/village/town/city and the legacy non-program path -- plus the
// fail-fast error paths, which a live run never exercises at all.
//
// deps.chunkManager is deliberately null: plan() is the INLINE planning phase and
// must not touch the world (the grounding gate self-skips without a ChunkManager,
// and the work units are returned unexecuted). So this runs headless in
// milliseconds. What it does NOT cover: running the units, which need a world --
// that remains L4's job.
// ============================================================================

namespace {

SettlementBuildService::Plan planTier(const std::string& tier, int w = 80, int d = 48,
                                      int seed = 3) {
    nlohmann::json params = {{"era", "medieval"},
                             {"tier", tier},
                             {"seed", seed},
                             {"width", w},
                             {"depth", d},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;   // no world: planning only
    return SettlementBuildService::plan(params, deps);
}

std::set<std::string> unitLabels(const SettlementBuildService::Plan& p) {
    std::set<std::string> out;
    for (const auto& u : p.units) out.insert(u.label);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Every shipped tier plans. The city tier especially: planCityLayout and
// CityLayoutTest exist, but the city was never covered end-to-end through the
// orchestrator, and it is absent from ValidationLedger.md entirely.
// ---------------------------------------------------------------------------
TEST(SettlementBuildServiceTest, EveryShippedTierProducesABuildablePlan) {
    for (const char* tier : {"hamlet", "village", "town", "city"}) {
        const auto plan = planTier(tier);
        ASSERT_TRUE(plan.ok()) << tier << " failed to plan: " << plan.error.dump();
        EXPECT_FALSE(plan.units.empty()) << tier << " planned zero work units";
        ASSERT_TRUE(plan.settlement.contains("buildings")) << tier;
        EXPECT_GT(plan.settlement["buildings"].get<int>(), 0) << tier << " planned no buildings";
        EXPECT_EQ(plan.program.value("tier", std::string()), tier)
            << "the program echo must identify the tier that was actually built (determinism "
               "contract: a live build has to be reproducible from its own response)";
        EXPECT_FALSE(plan.jobLabel.empty()) << tier << " produced no job label";
    }
}

// The legacy (no era/tier) path must keep working -- it is a different branch of
// plan() and the live run never touched it.
TEST(SettlementBuildServiceTest, TheLegacyNonProgramPathStillPlans) {
    nlohmann::json params = {{"width", 52}, {"depth", 36}, {"cols", 2}, {"rows", 2},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    const auto plan = SettlementBuildService::plan(params, deps);

    ASSERT_TRUE(plan.ok()) << plan.error.dump();
    EXPECT_GT(plan.settlement.value("buildings", 0), 0);
    EXPECT_TRUE(plan.program.empty() || !plan.program.contains("tier"))
        << "the legacy path must not fabricate a program echo it never resolved";
}

// ---------------------------------------------------------------------------
// The ORDER of the work units is load-bearing and was previously enforced only by
// a comment ("Buildings LAST -- site prep must precede them"). Site prep clears
// and grades the ground the buildings are then seated on; running a building
// before its parcel is cleared puts it under a tree.
// ---------------------------------------------------------------------------
TEST(SettlementBuildServiceTest, NavIsRebuiltAfterTheBuildingsExist) {
    const auto plan = planTier("village");
    ASSERT_TRUE(plan.ok()) << plan.error.dump();

    int firstBuilding = -1, navRebuild = -1;
    for (size_t i = 0; i < plan.units.size(); ++i) {
        const std::string& l = plan.units[i].label;
        if (l.rfind("building ", 0) == 0 && firstBuilding < 0) firstBuilding = (int)i;
        if (l == "nav rebuild") navRebuild = (int)i;
    }
    ASSERT_GE(firstBuilding, 0) << "no building units were planned";
    ASSERT_GE(navRebuild, 0) << "no nav rebuild unit was planned";
    EXPECT_GT(navRebuild, firstBuilding)
        << "nav is rebuilt before the buildings exist, so the graph would not see them";
}

// A world-free plan is BUILDINGS ONLY -- every site-prep unit (clearing, terracing,
// paving, fences, yard props) is world-gated. That is legitimate, but it must be
// SURFACED: a caller that ran this plan would otherwise get houses with no streets
// and no way to know the difference. This test was written after the ordering test
// above failed with "no site-prep units were planned" -- the code was right and the
// test's premise was wrong, but the silence it exposed was worth closing.
TEST(SettlementBuildServiceTest, AWorldFreePlanSurfacesThatSitePrepWasSkipped) {
    const auto plan = planTier("village");
    ASSERT_TRUE(plan.ok()) << plan.error.dump();

    EXPECT_TRUE(plan.settlement.value("site_prep_skipped", false))
        << "planning with no ChunkManager silently produced a partial plan - a caller cannot "
           "tell that clearing/paving/fences were never scheduled";
    EXPECT_FALSE(plan.settlement.value("site_prep_skipped_reason", std::string()).empty())
        << "the skip must say WHY, not just that it happened";

    for (const auto& u : plan.units)
        EXPECT_NE(u.label, "fencing parcels")
            << "a world-gated site-prep unit was planned without a world";
}

// The phases the response reports must be the phases that were planned -- these
// shared json handles are what the caller folds into its result, and they start
// empty (the units have not run).
TEST(SettlementBuildServiceTest, PerPhaseResultHandlesExistAndStartEmpty) {
    const auto plan = planTier("village");
    ASSERT_TRUE(plan.ok());
    ASSERT_NE(plan.paths, nullptr);
    ASSERT_NE(plan.yardProps, nullptr);
    ASSERT_NE(plan.residents, nullptr);
    EXPECT_TRUE(plan.paths->empty()) << "phase results are non-empty before any unit ran";
    EXPECT_TRUE(plan.yardProps->empty());
    EXPECT_TRUE(plan.residents->empty());
}

// ---------------------------------------------------------------------------
// TEETH — fail-fast paths. An unknown era/tier must be SURFACED, never silently
// defaulted; that is the whole point of the era hook staying honest, and a live
// run never exercises it.
// ---------------------------------------------------------------------------
TEST(SettlementBuildServiceTest, AnUnknownEraOrTierIsRefusedNotSilentlyDefaulted) {
    for (const auto& bad : {std::pair<const char*, const char*>{"bronze_age", "village"},
                            std::pair<const char*, const char*>{"medieval", "metropolis"}}) {
        nlohmann::json params = {{"era", bad.first}, {"tier", bad.second}, {"width", 80},
                                 {"depth", 48}, {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
        SettlementBuildService::Deps deps;
        const auto plan = SettlementBuildService::plan(params, deps);

        EXPECT_FALSE(plan.ok()) << bad.first << "/" << bad.second
                                << " planned successfully - an unknown era/tier was silently "
                                   "defaulted, which is exactly what the era hook must never do";
        EXPECT_TRUE(plan.units.empty()) << "a failed plan handed back work units to run";
        EXPECT_TRUE(plan.error.contains("known_eras") || plan.error.contains("known_tiers"))
            << "the refusal must tell the caller what IS valid, not just say no: "
            << plan.error.dump();
    }
}

// A footprint too small for the tier's morphology must fail fast with a reason,
// not plan a degenerate settlement.
TEST(SettlementBuildServiceTest, AFootprintTooSmallForTheTierIsRefused) {
    const auto plan = planTier("town", /*w=*/12, /*d=*/8);
    EXPECT_FALSE(plan.ok())
        << "a 12x8 footprint planned a whole market town - the size gate is not firing";
    EXPECT_TRUE(plan.units.empty());
}

// Terrain mode without a world must be refused rather than crashing or silently
// falling back to the flat-plane path (the fallback would place a town on terrain
// it never analysed).
TEST(SettlementBuildServiceTest, TerrainModeWithoutAWorldIsRefused) {
    nlohmann::json params = {{"era", "medieval"}, {"tier", "village"}, {"terrain", true},
                             {"width", 80}, {"depth", 48},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;   // no ChunkManager
    const auto plan = SettlementBuildService::plan(params, deps);

    EXPECT_FALSE(plan.ok()) << "terrain mode planned with no world to analyse";
    EXPECT_TRUE(plan.units.empty());
}

// Determinism is the contract the program echo advertises (era+tier+seed). Same
// inputs must plan the same settlement, or a "reproducible" build is a fiction.
TEST(SettlementBuildServiceTest, PlanningIsDeterministicInTheSeed) {
    const auto a = planTier("village", 80, 48, 3);
    const auto b = planTier("village", 80, 48, 3);
    const auto c = planTier("village", 80, 48, 4);
    ASSERT_TRUE(a.ok() && b.ok() && c.ok());

    EXPECT_EQ(a.queuedBuilds.dump(), b.queuedBuilds.dump())
        << "same era/tier/seed produced a different settlement - the determinism contract in the "
           "program echo is false";
    EXPECT_EQ(unitLabels(a), unitLabels(b));
    EXPECT_NE(a.queuedBuilds.dump(), c.queuedBuilds.dump())
        << "a different seed produced an IDENTICAL settlement - the seed is not wired through, so "
           "the determinism check above is vacuous";
}
