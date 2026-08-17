#include "core/WorldForgeBuildService.h"

#include "utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Phyxel {
namespace Core {

nlohmann::json WorldForgeBuildService::settlementParamsFor(const WorldForgeSite& site) {
    // Position is the site CENTRE in the plan but SettlementBuildService takes the MIN
    // corner; convert. Y is a nominal base — terrain mode re-grounds per column.
    return nlohmann::json{
        {"era", "medieval"},
        {"tier", site.tier},
        {"seed", site.seed},
        {"position", {{"x", site.pos.x - site.width / 2},
                      {"y", static_cast<int>(site.surfaceY)},
                      {"z", site.pos.y - site.depth / 2}}},
        {"width", site.width},
        {"depth", site.depth},
        {"terrain", true},
        {"async", false},   // units are spliced into the worldforge job (one per frame)
        // NO residents at remote sites (V1, measured 2026-08-16): the job releases the
        // streaming focus when done, the site's chunks evict, and spawned residents fall
        // through the missing occupancy grids (observed at y=-233k) — and residents are
        // not DB-persisted anyway (recorded gap), so they'd vanish on reload regardless.
        // Falling NPCs are worse than none. Real fix: resident re-spawn on stream-in from
        // persisted Location records (docs/StructurePipelineGaps.md 2026-08-16).
        {"residents", false},
    };
}

std::vector<int> WorldForgeBuildService::realizationOrder(const WorldForgePlan& plan) {
    auto rank = [](const std::string& tier) {
        if (tier == "town") return 3;
        if (tier == "village") return 2;
        return 1;
    };
    std::vector<int> order;
    for (const auto& s : plan.sites()) order.push_back(s.id);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int ra = rank(plan.sites()[a].tier), rb = rank(plan.sites()[b].tier);
        if (ra != rb) return ra > rb;
        return a < b;
    });
    return order;
}

// ── The self-chaining state machine ───────────────────────────────────────────────────────────
// Every unit performs ONE step and appends its successor(s), so the frame loop stays alive
// and MainThreadJobs::total grows honestly. All mutual recursion below appends units — it
// never runs work inline — so stack depth is constant regardless of site count.
namespace {

struct BuildState {
    WorldForgeBuildService::Deps deps;
    MainThreadJobs* jobs = nullptr;
    MainThreadJobs::Id jobId = 0;
    std::vector<int> queue;   // site ids in realization order
    size_t idx = 0;
    int polls = 0;
    std::chrono::steady_clock::time_point pollDeadline{};
    WorldForgeLedger ledger;
    nlohmann::json outcomes = nlohmann::json::array();
};

void persistLedger(BuildState& s) {
    if (s.deps.saveLedger) s.deps.saveLedger(s.ledger.toJson());
}

void queueNextSite(const std::shared_ptr<BuildState>& s);

void queueFinal(const std::shared_ptr<BuildState>& s) {
    s->jobs->addUnit(s->jobId, "worldforge: finalizing", [s] {
        if (s->deps.releaseFocus) s->deps.releaseFocus();
        persistLedger(*s);
        s->jobs->mergeResult(s->jobId, {{"success", true},
                                        {"sites", s->outcomes},
                                        {"plan_hash", std::to_string(s->ledger.planHash)}});
        s->jobs->seal(s->jobId);
    });
}

void refuseSite(const std::shared_ptr<BuildState>& s, const WorldForgeSite& site,
                const std::string& error) {
    auto& e = s->ledger.ensure(site.id, site.tier);
    e.status = "refused";
    e.error = error;
    persistLedger(*s);
    s->outcomes.push_back({{"id", site.id}, {"tier", site.tier},
                           {"status", "refused"}, {"error", error}});
    LOG_WARN_FMT("WorldForge", "[WORLDFORGE] site " << site.id << " (" << site.tier
                 << ") REFUSED: " << error);
    ++s->idx;
    queueNextSite(s);
}

void queuePlanAndBuild(const std::shared_ptr<BuildState>& s) {
    const int siteId = s->queue[s->idx];
    s->jobs->addUnit(s->jobId, "site " + std::to_string(siteId) + ": planning", [s, siteId] {
        const WorldForgeSite& site = s->deps.plan->sites()[siteId];
        auto plan = std::make_shared<SettlementBuildService::Plan>(
            s->deps.planSettlement(WorldForgeBuildService::settlementParamsFor(site)));
        if (!plan->ok()) {
            // A refusal is an OUTCOME, recorded and surfaced — never retried into a
            // broken build (the grounding gate refused for a reason).
            const std::string err = plan->error.is_object()
                ? plan->error.value("error", plan->error.dump())
                : plan->error.dump();
            refuseSite(s, site, err);
            return;
        }
        // Splice the settlement's own work units (one per frame), then the outcome unit.
        const std::string prefix = "site " + std::to_string(siteId) + ": ";
        for (auto& u : plan->units)
            s->jobs->addUnit(s->jobId, prefix + u.label, std::move(u.run));
        s->jobs->addUnit(s->jobId, prefix + "checkpoint", [s, siteId, plan] {
            const WorldForgeSite& st = s->deps.plan->sites()[siteId];
            // Robust to the phase-result shapes the settlement units actually emit
            // (array of entries, {"count": N}, or a bare number — the first live run
            // showed 15 spawned residents reported as 0 because only arrays counted).
            auto countOf = [](const std::shared_ptr<nlohmann::json>& j) -> int {
                if (!j) return 0;
                if (j->is_array()) return static_cast<int>(j->size());
                if (j->is_number_integer()) return j->get<int>();
                if (j->is_object()) {
                    if (j->contains("count") && (*j)["count"].is_number_integer())
                        return (*j)["count"].get<int>();
                    if (j->contains("spawned") && (*j)["spawned"].is_array())
                        return static_cast<int>((*j)["spawned"].size());
                }
                return 0;
            };
            auto& e = s->ledger.ensure(st.id, st.tier);
            e.status = "built";
            e.buildings = plan->queuedBuilds.is_array()
                ? static_cast<int>(plan->queuedBuilds.size()) : 0;
            e.lotFailures = countOf(plan->lotFailures);
            e.residents = countOf(plan->residents);
            persistLedger(*s);
            if (s->deps.checkpointWorld) s->deps.checkpointWorld();
            s->outcomes.push_back({{"id", st.id}, {"tier", st.tier}, {"status", "built"},
                                   {"buildings", e.buildings},
                                   {"lot_failures", e.lotFailures},
                                   {"residents", e.residents}});
            LOG_INFO_FMT("WorldForge", "[WORLDFORGE] site " << st.id << " (" << st.tier
                         << ") BUILT: " << e.buildings << " buildings, " << e.residents
                         << " residents, " << e.lotFailures << " lot failures");
            ++s->idx;
            queueNextSite(s);
        });
    });
}

float siteFocusRadius(const WorldForgeSite& site) {
    return 0.5f * std::sqrt(static_cast<float>(site.width * site.width +
                                               site.depth * site.depth)) + 64.0f;
}

void queueResidencyPoll(const std::shared_ptr<BuildState>& s) {
    const int siteId = s->queue[s->idx];
    s->jobs->addUnit(s->jobId, "site " + std::to_string(siteId) + ": residency", [s, siteId] {
        const WorldForgeSite& site = s->deps.plan->sites()[siteId];
        // Re-issue the focus EVERY poll: the editor's driver WALKS the streaming anchor
        // toward the site in steps (an instant far teleport of the anchor is the recorded
        // "spawn-swap never finished booting" streaming fragility — the anchor must move
        // like a fast player, which is the exercised path).
        if (s->deps.focusResidency)
            s->deps.focusResidency(glm::vec3(static_cast<float>(site.pos.x), site.surfaceY,
                                             static_cast<float>(site.pos.y)),
                                   siteFocusRadius(site));
        const bool ready = !s->deps.residencyReady || s->deps.residencyReady(site);
        if (ready) {
            queuePlanAndBuild(s);
            return;
        }
        if (++s->polls >= s->deps.maxResidencyPolls ||
            std::chrono::steady_clock::now() >= s->pollDeadline) {
            // Bounded wait: a dead/stalled streaming pump surfaces as a refusal, not a hang.
            refuseSite(s, site, "residency_timeout");
            return;
        }
        queueResidencyPoll(s);   // re-poll next frame
    });
}

void queueNextSite(const std::shared_ptr<BuildState>& s) {
    if (s->idx >= s->queue.size()) {
        queueFinal(s);
        return;
    }
    s->polls = 0;
    s->pollDeadline = std::chrono::steady_clock::now() +
                      std::chrono::seconds(std::max(1, s->deps.residencyTimeoutSeconds));
    queueResidencyPoll(s);
}

}  // namespace

MainThreadJobs::Id WorldForgeBuildService::start(MainThreadJobs& jobs, const Deps& deps,
                                                 const std::vector<int>& siteFilter,
                                                 nlohmann::json& immediateResult) {
    if (!deps.plan) {
        const auto id = jobs.start("worldforge_build", "worldforge: no plan");
        jobs.mergeResult(id, {{"error", "no applied worldforge plan"}});
        jobs.seal(id);
        immediateResult = {{"error", "no applied worldforge plan"}, {"job_id", id}};
        return id;
    }
    auto s = std::make_shared<BuildState>();
    s->deps = deps;
    s->jobs = &jobs;
    if (!s->deps.planSettlement)
        s->deps.planSettlement = [d = s->deps.settlement](const nlohmann::json& params) {
            return SettlementBuildService::plan(params, d);
        };
    if (deps.loadLedger) s->ledger = WorldForgeLedger::fromJson(deps.loadLedger());
    const uint64_t liveHash = deps.plan->planHash();
    if (s->ledger.stale(liveHash)) {
        // Sites were built against a DIFFERENT plan (params/seed edited since). Surface it —
        // building this plan's sites into that world would interleave two plans' output.
        const std::string err = "worldforge ledger is stale (built against plan " +
                                std::to_string(s->ledger.planHash) + ", live plan " +
                                std::to_string(liveHash) +
                                ") — regenerate the world or clear world_meta[worldforge_ledger]";
        const auto id = jobs.start("worldforge_build", "worldforge: stale ledger");
        jobs.mergeResult(id, {{"error", err}});
        jobs.seal(id);
        immediateResult = {{"error", err}, {"job_id", id}};
        return id;
    }
    s->ledger.planHash = liveHash;
    for (const int id : realizationOrder(*deps.plan)) {
        if (!siteFilter.empty() &&
            std::find(siteFilter.begin(), siteFilter.end(), id) == siteFilter.end())
            continue;
        if (const auto* e = s->ledger.find(id); e && e->status == "built") continue;
        s->queue.push_back(id);
    }
    s->jobId = jobs.start("worldforge_build",
                          "worldforge: " + std::to_string(s->queue.size()) + " sites");
    immediateResult = {{"success", true},
                       {"job_id", s->jobId},
                       {"sites_queued", static_cast<int>(s->queue.size())},
                       {"plan_hash", std::to_string(liveHash)}};
    queueNextSite(s);
    return s->jobId;
}

}  // namespace Core
}  // namespace Phyxel
