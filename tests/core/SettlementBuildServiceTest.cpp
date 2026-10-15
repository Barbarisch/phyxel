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


// WalkabilityGateAndPlaytestLoop increment 1: with a world present, the plan carries a
// "walkability gate" unit that runs AFTER every unit that changes occupancy (buildings,
// fences, sweep, doorsteps) and BEFORE the nav rebuild - the gate measures the world
// the residents will get. RED on the old code: no such unit was planned.
#include "core/ChunkManager.h"
TEST(SettlementBuildServiceTest, TheWalkabilityGateRunsOnTheFinishedWorldBeforeNav) {
    Phyxel::ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    // An empty ChunkManager has no terrain: the grounding pre-check would refuse the
    // site, and this test is about the UNIT ORDER of a world-gated plan, not siting.
    nlohmann::json params = {{"era", "medieval"}, {"tier", "village"}, {"seed", 7},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}, {"width", 80}, {"depth", 48},
                             {"allow_ungrounded", true}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = &cm;
    const auto plan = SettlementBuildService::plan(params, deps);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();
    int gate = -1, doorsteps = -1, nav = -1, lastBuilding = -1, fences = -1, sweep = -1;
    for (size_t i = 0; i < plan.units.size(); ++i) {
        const std::string& l = plan.units[i].label;
        if (l == "walkability gate") gate = (int)i;
        if (l == "doorsteps") doorsteps = (int)i;
        if (l == "nav rebuild") nav = (int)i;
        if (l == "fencing parcels") fences = (int)i;
        if (l == "street sweep") sweep = (int)i;
        if (l.rfind("building ", 0) == 0) lastBuilding = (int)i;
    }
    ASSERT_GE(gate, 0) << "no walkability gate unit was planned";
    ASSERT_GE(nav, 0); ASSERT_GE(doorsteps, 0); ASSERT_GE(lastBuilding, 0);
    EXPECT_GT(gate, lastBuilding) << "the gate must see every building";
    EXPECT_GT(gate, doorsteps)    << "the gate must see the stoops";
    EXPECT_GT(gate, fences)       << "the gate must see the fences";
    EXPECT_GT(gate, sweep)        << "the gate must see the swept streets";
    EXPECT_LT(gate, nav)          << "the gate reports before the graph is built on the same world";
}

// ── M0 (docs/WorldForgeV2Plan.md §5 M0): the gate runs for EVERY morphology ──────
// F7 live baseline 2026-10-14: the 8-site build logged a "walkability gate:" line for the
// three main_street sites only; the five cluster-morphology hamlets were never gated.
// A hamlet's residents walk street -> door like anyone else; the gate's routes start at
// each recorded front-door marker, not at street rectangles, so nothing in the gate itself
// needs the main street. RED: today the unit is queued under `mainStreetMode`.
TEST(SettlementBuildServiceTest, TheWalkabilityGateRunsForEveryMorphology) {
    Phyxel::ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    for (const char* tier : {"hamlet", "village", "town", "city"}) {
        nlohmann::json params = {{"era", "medieval"}, {"tier", tier}, {"seed", 7},
                                 {"position", {{"x", 0}, {"y", 16}, {"z", 0}}},
                                 {"width", 160}, {"depth", 160}, {"allow_ungrounded", true}};
        SettlementBuildService::Deps deps;
        deps.chunkManager = &cm;
        const auto plan = SettlementBuildService::plan(params, deps);
        ASSERT_TRUE(plan.ok()) << tier << ": " << plan.error.dump();
        bool gate = false;
        for (const auto& u : plan.units) gate = gate || (u.label == "walkability gate");
        EXPECT_TRUE(gate) << tier << ": no walkability gate unit was planned (morphology "
                          << plan.program.value("morphology", std::string("?")) << ")";
        ASSERT_NE(plan.walkability, nullptr) << tier << ": plan carries no walkability result slot";
        EXPECT_FALSE(plan.allowUnwalkable) << tier << ": allow_unwalkable must default to false";
    }
}

// The verdict is a pure function of the gate result + the override flag (owner ruling
// 2026-10-14: "mark it failed, keep the voxels, retry later").
TEST(SettlementBuildServiceTest, GateOutcomeBlocksUnlessOverridden) {
    const nlohmann::json clean   = {{"walkable", 12}, {"blocked", 0}, {"skipped", 0}};
    const nlohmann::json pinched = {{"walkable", 27}, {"blocked", 1}, {"skipped", 0}};
    const nlohmann::json notRun  = nlohmann::json::object();

    auto o = SettlementBuildService::gateOutcome(clean, false);
    EXPECT_EQ(o.value("gate", ""), "passed");
    EXPECT_FALSE(o.value("fails_build", true));

    o = SettlementBuildService::gateOutcome(pinched, false);
    EXPECT_EQ(o.value("gate", ""), "blocked");
    EXPECT_TRUE(o.value("fails_build", false)) << "a blocked gate must fail the build by default";
    EXPECT_EQ(o.value("blocked", 0), 1);

    o = SettlementBuildService::gateOutcome(pinched, true);
    EXPECT_EQ(o.value("gate", ""), "overridden");
    EXPECT_FALSE(o.value("fails_build", true)) << "allow_unwalkable keeps the build";

    o = SettlementBuildService::gateOutcome(notRun, false);
    EXPECT_EQ(o.value("gate", ""), "not_run");
    EXPECT_FALSE(o.value("fails_build", true)) << "a gate that never ran is not a refusal";

    // M0 row 2d (solution-auditor 2026-10-15): a SKIPPED front door is a door nobody can use -
    // the gate skipped routes whose endpoint did not seat (the town's tavern: passage cell
    // unseated at the threshold) and "5 front doors, 1 skipped" PASSED. RED: skipped > 0 passes.
    const nlohmann::json unseated = {{"walkable", 13}, {"blocked", 0}, {"skipped", 1}};
    o = SettlementBuildService::gateOutcome(unseated, false);
    EXPECT_EQ(o.value("gate", ""), "unseated");
    EXPECT_TRUE(o.value("fails_build", false)) << "a door whose endpoint does not seat is unusable";
    EXPECT_EQ(o.value("skipped", 0), 1);
    o = SettlementBuildService::gateOutcome(unseated, true);
    EXPECT_EQ(o.value("gate", ""), "overridden");
    EXPECT_FALSE(o.value("fails_build", true));
}

// ── M0 ("site prep reordered", docs/WorldForgeV2Plan.md §5 M0) ──────────────────────
// F7 live baseline 2026-10-14 + the M0 design-check: fences, yard props and the door spurs
// / cluster paths were queued BEFORE the building units, anchored on footprint midpoints.
// A lot whose building refused twice therefore kept a fence run, a woodpile and a paved spur
// to nothing. Street GRADING stays before the buildings (they re-sample graded ground);
// everything that dresses a LOT runs after its building exists and skips refused lots.
// RED: today "fencing parcels" / "yard props + well" / "grading + paving paths" precede the
// first "building N/M" unit, and no "door spurs" unit exists.
TEST(SettlementBuildServiceTest, LotDressingRunsAfterTheBuildingsForEveryMorphology) {
    Phyxel::ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    for (const char* tier : {"hamlet", "village", "town", "city"}) {
        nlohmann::json params = {{"era", "medieval"}, {"tier", tier}, {"seed", 7},
                                 {"position", {{"x", 0}, {"y", 16}, {"z", 0}}},
                                 {"width", 160}, {"depth", 160}, {"allow_ungrounded", true},
                                 {"terrain", true}};
        SettlementBuildService::Deps deps;
        deps.chunkManager = &cm;
        const auto plan = SettlementBuildService::plan(params, deps);
        ASSERT_TRUE(plan.ok()) << tier << ": " << plan.error.dump();
        int firstBuilding = -1, lastBuilding = -1, streets = -1, paths = -1, spurs = -1,
            fences = -1, props = -1, sweep = -1;
        for (size_t i = 0; i < plan.units.size(); ++i) {
            const std::string& l = plan.units[i].label;
            const int idx = static_cast<int>(i);
            if (l.rfind("building ", 0) == 0) { if (firstBuilding < 0) firstBuilding = idx; lastBuilding = idx; }
            if (l == "grading + paving streets") streets = idx;
            if (l == "grading + paving paths")   paths = idx;
            if (l == "door spurs")               spurs = idx;
            if (l == "fencing parcels")          fences = idx;
            if (l == "yard props + well")        props = idx;
            if (l == "street sweep")             sweep = idx;
        }
        ASSERT_GE(lastBuilding, 0) << tier << ": no building units";
        const std::string morph = plan.program.value("morphology", std::string("?"));
        const bool mainStreet = (morph == "main_street" || morph == "semi_organic");
        if (mainStreet) {
            ASSERT_GE(streets, 0) << tier << ": no street unit";
            EXPECT_LT(streets, firstBuilding) << tier << ": streets must be graded before the buildings";
            ASSERT_GE(spurs, 0) << tier << ": no 'door spurs' unit (spurs still bundled into the street unit?)";
            EXPECT_GT(spurs, lastBuilding) << tier << ": door spurs must run from the REALIZED doors";
            // Yard props need a PlacedObjectManager + templates (the headless rig has neither, so
            // the unit is legitimately absent); when it IS planned it must follow the buildings.
            if (props >= 0)
                EXPECT_GT(props, lastBuilding) << tier << ": yard props must not dress a refused lot";
        } else {
            ASSERT_GE(paths, 0) << tier << ": no cluster path unit";
            EXPECT_GT(paths, lastBuilding) << tier << ": paths must run door-to-door from the REALIZED doors";
        }
        ASSERT_GE(fences, 0) << tier << ": no fence unit";
        EXPECT_GT(fences, lastBuilding) << tier << ": fences must not fence a refused lot";
        if (sweep >= 0) {
            EXPECT_LT(fences, sweep) << tier;
            if (spurs >= 0) EXPECT_LT(spurs, sweep) << tier << ": the sweep must see the spur stamp";
        }
    }
}

// A refused lot (both the draw and its re-roll refused) maps to its plot; nothing else does.
TEST(SettlementBuildServiceTest, FailedPlotsMapLotFailuresToPlotIndices) {
    const nlohmann::json none = nlohmann::json::array();
    EXPECT_TRUE(SettlementBuildService::failedPlots(none, {4, 7, 2}).empty());
    const nlohmann::json one = nlohmann::json::array({{{"lot", 1}, {"typology", "croft"}}});
    const auto fp = SettlementBuildService::failedPlots(one, {4, 7, 2});
    ASSERT_EQ(fp.size(), 1u);
    EXPECT_TRUE(fp.count(7));
    // out-of-range / malformed entries never map to a plot
    const nlohmann::json bad = nlohmann::json::array({{{"lot", 9}}, {{"typology", "x"}}});
    EXPECT_TRUE(SettlementBuildService::failedPlots(bad, {4, 7, 2}).empty());
    EXPECT_TRUE(SettlementBuildService::failedPlots(nlohmann::json::object(), {4}).empty());
}

// ── M0 row 2b: a door spur starts OUTSIDE THE DOOR, not at the marker ─────────────────
// M0 L4 (docs/evidence/worldforge_v2/m0_l4_engine.txt): the town's spurs went "2 planned
// from 5 realized doors, 3 too steep" (F7: 8 ok / 0 too steep). The spur unit started
// every spur at the forge's MARKER - 2 cells out from the door, often on the street's
// edge - so a straight run to the street had one cell less than the old start (1 cell
// outside the wall) to absorb the pad-to-street drop; and the marker is not always in
// front of the door (regen #14). RED: doorApproach ignores `seats` and answers the
// marker's column.
TEST(SettlementBuildServiceTest, DoorApproachFindsTheDoorAlongTheMarkersWall) {
    // footprint x 10..15, z 20..25 (6x6); the street is SOUTH (z < 20): south wall z = 20.
    const glm::ivec4 foot{10, 20, 6, 6};
    // marker at the footprint's corner column, 2 cells out on the street side
    const glm::ivec3 marker{10, 17, 18};
    // the only opening: wall cell (14,20) and the cell inside it (14,21) seat; no other wall cell does
    auto seats = [](int cx, int cz) { return cx == 14 && (cz == 20 || cz == 21); };
    const auto d = SettlementBuildService::doorApproach(marker, foot, seats);
    EXPECT_EQ(d.normal, glm::ivec2(0, -1));
    EXPECT_TRUE(d.found);
    EXPECT_EQ(d.wall, glm::ivec2(14, 20));
    EXPECT_EQ(d.outside, glm::ivec2(14, 19)) << "the spur starts one cell outside the DOOR";
    EXPECT_EQ(d.inside, glm::ivec2(14, 21));
    // a wall cell that seats but whose inside cell does not (a window bay) is not a door
    const auto w = SettlementBuildService::doorApproach(marker, foot,
        [](int cx, int cz) { return (cx == 12 && cz == 20) || (cx == 14 && (cz == 20 || cz == 21)); });
    EXPECT_EQ(w.wall, glm::ivec2(14, 20));
    // no seated opening at all -> the marker's column, flagged as the fallback
    const auto f = SettlementBuildService::doorApproach(marker, foot, [](int, int) { return false; });
    EXPECT_FALSE(f.found);
    EXPECT_EQ(f.wall, glm::ivec2(10, 20));
    EXPECT_EQ(f.outside, glm::ivec2(10, 19));
    // null predicate (no threshold recorded) -> the same fallback, no crash
    const auto n = SettlementBuildService::doorApproach(marker, foot, {});
    EXPECT_FALSE(n.found);
    EXPECT_EQ(n.outside, glm::ivec2(10, 19));
    // east wall: marker east of the footprint, door 2 cells along the wall
    const auto e = SettlementBuildService::doorApproach({17, 17, 23}, foot,
        [](int cx, int cz) { return cz == 22 && (cx == 15 || cx == 14); });
    EXPECT_EQ(e.normal, glm::ivec2(1, 0));
    EXPECT_TRUE(e.found);
    EXPECT_EQ(e.wall, glm::ivec2(15, 22));
    EXPECT_EQ(e.outside, glm::ivec2(16, 22));
    EXPECT_EQ(e.inside, glm::ivec2(14, 22));
}

// ── M0 (docs/WorldForgeV2Plan.md §5 M0): the era reaches every building ──────────────
// The settlement program is era-keyed (settlement_program.json eras.<era>), the forge is
// period-aware and REFUSES an unknown period rather than substitute medieval — but the
// settlement never passed `period`, so every building of every era was built medieval and
// the era hook never reached a wall. RED: no queued build carries a period today.
TEST(SettlementBuildServiceTest, BuildingsCarryTheEraPeriod) {
    nlohmann::json params = {{"era", "medieval"}, {"tier", "village"}, {"seed", 7},
                             {"width", 80}, {"depth", 48}};
    SettlementBuildService::Deps deps;   // planning only
    const auto plan = SettlementBuildService::plan(params, deps);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();
    ASSERT_FALSE(plan.queuedBuilds.empty());
    for (const auto& b : plan.queuedBuilds)
        EXPECT_EQ(b.value("period", std::string()), "medieval")
            << "a queued building carries no period - the era never reaches the forge: " << b.dump();
}

// The room program is a hard dependency: a load failure must refuse with the file named,
// never drop every plot and report "footprint too small" (the symptom seen when the load
// result was ignored). The path is overridable so the failure is testable.
TEST(SettlementBuildServiceTest, MissingRoomProgramRefuses) {
    nlohmann::json params = {{"era", "medieval"}, {"tier", "village"}, {"seed", 7},
                             {"width", 80}, {"depth", 48},
                             {"room_program", "resources/does_not_exist_room_program.json"}};
    SettlementBuildService::Deps deps;
    const auto plan = SettlementBuildService::plan(params, deps);
    ASSERT_FALSE(plan.ok()) << "a missing room program must refuse the plan";
    const std::string err = plan.error.value("error", plan.error.dump());
    EXPECT_NE(err.find("room program"), std::string::npos) << err;
    EXPECT_NE(err.find("does_not_exist_room_program.json"), std::string::npos) << "the refusal must name the file: " << err;
}
