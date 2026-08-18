#include <gtest/gtest.h>

#include <cmath>

#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/MainThreadJobs.h"
#include "core/WaterBodyIndex.h"
#include "core/WorldForgeBuildService.h"
#include "core/WorldForgeLedger.h"
#include "core/WorldForgePlan.h"

using namespace Phyxel;
using Core::MainThreadJobs;
using Core::SettlementBuildService;
using Core::WorldForgeBuildService;

// ============================================================================
// WorldForge M2 — the realization orchestrator, headless (docs/WorldForge.md).
// The state machine's effects all go through Deps functions, so the whole flow
// (residency → plan → build units → ledger checkpoint) drives via
// MainThreadJobs::tick() with stubs — no world, no editor. Red-before-green:
// the flow tests ran RED against a stub start() that sealed an empty job.
// ============================================================================

namespace {

// Flat viable terrain: every cell dry and buildable; pins make siting deterministic.
float flatHeight(float, float) { return 30.0f; }

std::shared_ptr<const WorldForgePlan> makePlan(int siteCount = 3) {
    static HydrologyMap hydro(flatHeight, 0.0f, 0.0f, 48, 48, 32.0f, 16.0f);
    static FlowField flow(flatHeight, 0.0f, 0.0f, 48, 48, 32.0f, 16.0f, /*threshold=*/1 << 20);
    static WaterBodyIndex bodies(hydro, flatHeight);
    WorldForgeParams p;
    p.enabled = true;
    p.siteCount = siteCount;
    p.regionRadius = 700.0f;
    p.minSpacing = 200.0f;
    p.sitePins = {{400, 400}, {700, 700}, {400, 1000}};
    return WorldForgePlan::bake(p, 42, flatHeight, hydro, flow, bodies,
                                [](int, int) { return std::string("Grass"); },
                                [](int, int) { return 30; },
                                [](float x, float z) { return flow.channelAt(x, z); });
}

struct Harness {
    std::shared_ptr<const WorldForgePlan> plan = makePlan();
    MainThreadJobs jobs;
    WorldForgeBuildService::Deps deps;
    std::string ledgerStore;                 // the fake world_meta slot
    int checkpoints = 0;
    int focusCalls = 0, releaseCalls = 0;
    std::vector<uint32_t> plannedSites;      // seeds, in settlement-planning invocation order

    Harness() {
        deps.plan = plan.get();
        deps.worldSeed = 42;
        deps.loadLedger = [this] { return ledgerStore; };
        deps.saveLedger = [this](const std::string& s) { ledgerStore = s; };
        deps.checkpointWorld = [this] { ++checkpoints; };
        deps.focusResidency = [this](const glm::vec3&, float) { ++focusCalls; };
        deps.releaseFocus = [this] { ++releaseCalls; };
        deps.residencyReady = [](const WorldForgeSite&) { return true; };
        deps.planSettlement = [this](const nlohmann::json& params) {
            plannedSites.push_back(params.value("seed", 0u));   // record call order by seed
            SettlementBuildService::Plan p;
            p.settlement = {{"buildings", 4}};
            p.queuedBuilds = nlohmann::json::array({1, 2, 3, 4});
            p.jobLabel = "stub settlement";
            p.residents = std::make_shared<nlohmann::json>(nlohmann::json::array({1, 2}));
            p.lotFailures = std::make_shared<nlohmann::json>(nlohmann::json::array());
            p.paths = std::make_shared<nlohmann::json>();
            p.yardProps = std::make_shared<nlohmann::json>();
            p.units.push_back({"stub unit", [] {}});
            return p;
        };
    }

    // Drive the job to completion (bounded — a hang is a failure, not a wait).
    nlohmann::json run(MainThreadJobs::Id id, int maxTicks = 100000) {
        for (int i = 0; i < maxTicks; ++i) {
            jobs.tick(1);
            const auto st = jobs.statusJson(id);
            if (!st.is_null() && st.value("state", std::string()) == "complete") return st;
        }
        return {{"error", "job did not complete within tick budget"}};
    }
};

}  // namespace

// ── Pure derivations (pinned) ───────────────────────────────────────────────

TEST(WorldForgeBuildFlowTest, SettlementParamsDerivedFromSite) {
    auto plan = makePlan();
    ASSERT_FALSE(plan->sites().empty());
    const auto& s = plan->sites()[0];
    const auto params = WorldForgeBuildService::settlementParamsFor(*plan, s);
    EXPECT_EQ(params.value("seed", 0u), s.seed) << "the plan's derived seed must reach the build";
    EXPECT_EQ(params.value("tier", std::string()), s.tier);
    EXPECT_EQ(params.value("width", 0), s.width);
    EXPECT_EQ(params.value("depth", 0), s.depth);
    EXPECT_EQ(params["position"].value("x", 1 << 30), s.pos.x - s.width / 2);   // min corner
    EXPECT_TRUE(params.value("terrain", false));
    EXPECT_FALSE(params.value("async", true)) << "units are spliced, not nested-async";
    EXPECT_FALSE(params.value("residents", true))
        << "remote sites spawn NO residents (they fall through evicted chunks and don't "
           "persist — measured live 2026-08-16; see StructurePipelineGaps)";
}

// Road-arrival orientation: the settlement's street axis follows the FIRST road arriving
// at the site (the main street should MEET the road, not sit perpendicular to it on a
// whim). Expected axis computed from the plan's own road geometry — never hardcoded.
TEST(WorldForgeBuildFlowTest, SettlementParamsCarryArrivalAxis) {
    auto plan = makePlan();
    ASSERT_FALSE(plan->roads().empty());
    // First site that has a road touching it, in road order (the derivation's contract).
    for (const auto& s : plan->sites()) {
        const WorldForgeRoad* arriving = nullptr;
        bool atA = false;
        for (const auto& r : plan->roads())
            if (r.a == s.id || r.b == s.id) {
                arriving = &r;
                atA = (r.a == s.id);
                break;
            }
        if (!arriving || arriving->centerline.size() < 2) continue;
        // Direction of the road AT the site's end of the centerline.
        const auto& cl = arriving->centerline;
        const glm::vec2 dir = atA ? (cl[0] - cl[1]) : (cl[cl.size() - 1] - cl[cl.size() - 2]);
        const std::string expected = std::abs(dir.x) >= std::abs(dir.y) ? "x" : "z";
        const auto params = WorldForgeBuildService::settlementParamsFor(*plan, s);
        EXPECT_EQ(params.value("street_axis", std::string("<absent>")), expected)
            << "site " << s.id << " must orient its street along the arriving road\n"
            << "params: " << params.dump() << "\nroads:"
            << [&] {
                   std::string out;
                   for (const auto& r : plan->roads())
                       out += " (" + std::to_string(r.a) + "-" + std::to_string(r.b) +
                              " pts " + std::to_string(r.centerline.size()) + ")";
                   return out;
               }();
        return;   // one verified site suffices
    }
    FAIL() << "no site with an arriving road found";
}

TEST(WorldForgeBuildFlowTest, RealizationOrderTownFirst) {
    auto plan = makePlan();
    const auto order = WorldForgeBuildService::realizationOrder(*plan);
    ASSERT_EQ(order.size(), plan->sites().size());
    EXPECT_EQ(plan->sites()[order[0]].tier, "town");
    int lastRank = 4;
    for (const int id : order) {
        const std::string& t = plan->sites()[id].tier;
        const int r = t == "town" ? 3 : (t == "village" ? 2 : 1);
        EXPECT_LE(r, lastRank);
        lastRank = r;
    }
}

// ── Ledger (serialization; green on first run — pure data) ──────────────────

TEST(WorldForgeLedgerTest, RoundTrip) {
    WorldForgeLedger l;
    l.planHash = 0xDEADBEEFCAFEull;
    auto& a = l.ensure(0, "town");
    a.status = "built";
    a.buildings = 7;
    a.residents = 5;
    auto& b = l.ensure(2, "hamlet");
    b.status = "refused";
    b.error = "residency_timeout";
    const WorldForgeLedger back = WorldForgeLedger::fromJson(l.toJson());
    ASSERT_EQ(back.sites.size(), 2u);
    EXPECT_EQ(back.planHash, l.planHash);
    EXPECT_EQ(back.sites[0].status, "built");
    EXPECT_EQ(back.sites[0].buildings, 7);
    EXPECT_EQ(back.sites[1].error, "residency_timeout");
}

TEST(WorldForgeLedgerTest, StaleDetection) {
    WorldForgeLedger l;
    EXPECT_FALSE(l.stale(123));   // empty ledger is never stale
    l.planHash = 111;
    EXPECT_FALSE(l.stale(111));
    EXPECT_TRUE(l.stale(222));
    EXPECT_EQ(WorldForgeLedger::fromJson("garbage").sites.size(), 0u);
}

// ── The flow itself (RED against the stub start) ────────────────────────────

// Happy path: every site flows residency → plan → units → ledger "built", in tier order,
// with a checkpoint per site and the focus released at the end.
TEST(WorldForgeBuildFlowTest, AllSitesBuildThroughTheLedger) {
    Harness h;
    nlohmann::json immediate;
    const auto id = WorldForgeBuildService::start(h.jobs, h.deps, {}, immediate);
    EXPECT_EQ(immediate.value("sites_queued", -1), 3);
    const auto st = h.run(id);
    ASSERT_EQ(st.value("state", std::string()), "complete") << st.dump();
    const WorldForgeLedger ledger = WorldForgeLedger::fromJson(h.ledgerStore);
    ASSERT_EQ(ledger.sites.size(), 3u) << "every site must be recorded";
    for (const auto& s : ledger.sites) {
        EXPECT_EQ(s.status, "built");
        EXPECT_EQ(s.buildings, 4);
        EXPECT_EQ(s.residents, 2);
    }
    EXPECT_EQ(ledger.planHash, h.plan->planHash());
    EXPECT_EQ(h.checkpoints, 3);
    EXPECT_GE(h.focusCalls, 3);
    EXPECT_GE(h.releaseCalls, 1);
    // Tier order: the town's seed must be the FIRST settlement planned.
    const auto order = WorldForgeBuildService::realizationOrder(*h.plan);
    ASSERT_FALSE(h.plannedSites.empty());
    EXPECT_EQ(h.plannedSites[0], h.plan->sites()[order[0]].seed);
}

// A settlement refusal is RECORDED, never thrown, and the job continues to the next site.
TEST(WorldForgeBuildFlowTest, RefusalRecordedAndJobContinues) {
    Harness h;
    int calls = 0;
    h.deps.planSettlement = [&calls](const nlohmann::json&) {
        ++calls;
        SettlementBuildService::Plan p;
        p.error = {{"error", "terrain too steep - no buildable plot"}};
        return p;
    };
    nlohmann::json immediate;
    const auto id = WorldForgeBuildService::start(h.jobs, h.deps, {}, immediate);
    const auto st = h.run(id);
    ASSERT_EQ(st.value("state", std::string()), "complete");
    EXPECT_EQ(calls, 3) << "a refusal must not stop the remaining sites";
    const WorldForgeLedger ledger = WorldForgeLedger::fromJson(h.ledgerStore);
    ASSERT_EQ(ledger.sites.size(), 3u);
    for (const auto& s : ledger.sites) {
        EXPECT_EQ(s.status, "refused");
        EXPECT_FALSE(s.error.empty());
    }
}

// Residency that never becomes ready refuses with residency_timeout after the poll budget
// — bounded waits, the job completes (the streaming-pump-death failure mode fails LOUDLY).
TEST(WorldForgeBuildFlowTest, ResidencyTimeoutRefusesSite) {
    Harness h;
    h.deps.residencyReady = [](const WorldForgeSite&) { return false; };
    h.deps.maxResidencyPolls = 5;
    int planned = 0;
    h.deps.planSettlement = [&planned](const nlohmann::json&) {
        ++planned;
        SettlementBuildService::Plan p;
        p.error = {{"error", "unreachable"}};
        return p;
    };
    nlohmann::json immediate;
    const auto id = WorldForgeBuildService::start(h.jobs, h.deps, {}, immediate);
    const auto st = h.run(id);
    ASSERT_EQ(st.value("state", std::string()), "complete");
    EXPECT_EQ(planned, 0) << "no settlement may plan without residency";
    const WorldForgeLedger ledger = WorldForgeLedger::fromJson(h.ledgerStore);
    ASSERT_EQ(ledger.sites.size(), 3u);
    for (const auto& s : ledger.sites) EXPECT_EQ(s.error, "residency_timeout");
}

// Idempotent re-run: sites already "built" in the ledger are skipped entirely.
TEST(WorldForgeBuildFlowTest, BuiltSitesSkippedOnRerun) {
    Harness h;
    // First run builds everything.
    nlohmann::json immediate;
    h.run(WorldForgeBuildService::start(h.jobs, h.deps, {}, immediate));
    h.plannedSites.clear();
    h.checkpoints = 0;
    // Second run: nothing to do.
    nlohmann::json immediate2;
    const auto id2 = WorldForgeBuildService::start(h.jobs, h.deps, {}, immediate2);
    const auto st = h.run(id2);
    ASSERT_EQ(st.value("state", std::string()), "complete");
    EXPECT_EQ(immediate2.value("sites_queued", -1), 0);
    EXPECT_TRUE(h.plannedSites.empty()) << "built sites must not re-plan";
}

// Site filter restricts the run to the named ids.
TEST(WorldForgeBuildFlowTest, SiteFilterRestrictsRun) {
    Harness h;
    nlohmann::json immediate;
    const auto id = WorldForgeBuildService::start(h.jobs, h.deps, {1}, immediate);
    EXPECT_EQ(immediate.value("sites_queued", -1), 1);
    h.run(id);
    const WorldForgeLedger ledger = WorldForgeLedger::fromJson(h.ledgerStore);
    int built = 0;
    for (const auto& s : ledger.sites)
        if (s.status == "built") ++built;
    EXPECT_EQ(built, 1);
}
