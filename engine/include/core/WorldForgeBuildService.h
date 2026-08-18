#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/MainThreadJobs.h"
#include "core/SettlementBuildService.h"
#include "core/WorldForgeLedger.h"
#include "core/WorldForgePlan.h"

namespace Phyxel {
namespace Core {

// ── WorldForge realization orchestrator (docs/WorldForge.md M2) ───────────────────────────────
//
// Walks the baked WorldForgePlan's sites in tier order (town → villages → hamlets) and
// realizes each one via SettlementBuildService, as a SELF-CHAINING MainThreadJobs job: every
// unit performs one step and appends its successor, so rendering/input/API stay alive
// throughout ([no-frozen-engine]) and /api/jobs shows honest progress. Per site:
//
//   residency (focus the streaming pump on the site, poll with a hard deadline)
//     → plan (SettlementBuildService::plan with the site's DERIVED seed; a refusal is
//        RECORDED, never retried into a broken build)
//     → build (the settlement plan's own work units, spliced one-per-frame)
//     → checkpoint (save dirty chunks + persist the ledger — crash-safe progress)
//
// Engine-side (not editor glue) so the state machine is headless-testable: all effects go
// through Deps functions; tests drive MainThreadJobs::tick() with stubs
// (WorldForgeBuildFlowTest). Bounded waits everywhere — a dead streaming pump surfaces as
// `refused: residency_timeout`, never a hang (the ~2 h pump-death open bug fails loudly).
class WorldForgeBuildService {
public:
    struct Deps {
        // Settlement realization dependencies (same struct build_settlement fills).
        SettlementBuildService::Deps settlement;
        // The applied plan (required) + the seed echo for the result payload.
        const WorldForgePlan* plan = nullptr;
        uint32_t worldSeed = 0;

        // Residency driver. focusResidency(pos, radius): anchor streaming on the site
        // (editor: ChunkManager::setStreamingFocusOverride + a loadDistance floor).
        // residencyReady(site): are the site's surface chunks resident? releaseFocus():
        // hand residency back to the player. Null fns = always-ready (headless tests).
        std::function<void(const glm::vec3& pos, float radius)> focusResidency;
        std::function<bool(const WorldForgeSite& site)> residencyReady;
        std::function<void()> releaseFocus;

        // Ledger persistence (world.db world_meta["worldforge_ledger"]). Null = in-memory only.
        std::function<std::string()> loadLedger;
        std::function<void(const std::string&)> saveLedger;
        // Checkpoint: save dirty chunks so a crash mid-run loses at most one site.
        std::function<void()> checkpointWorld;

        // Settlement planning seam. Defaults to SettlementBuildService::plan(params,
        // settlement); tests inject a stub so the flow runs headless without a world.
        std::function<SettlementBuildService::Plan(const nlohmann::json& params)> planSettlement;

        // Residency bounds — BOTH enforced, whichever fires first refuses the site with
        // "residency_timeout" and moves on (bounded waits — never hang the job):
        // wall-clock seconds (the real bound — poll counts scale with frame rate, and a
        // Debug build at 4 fps would stretch a frame-count budget 15x), plus a poll-count
        // cap (deterministic bound for headless tests, which tick faster than wall time).
        int residencyTimeoutSeconds = 120;
        int maxResidencyPolls = 1 << 30;
    };

    /// The exact settlement-build params a plan site realizes with — THE canonical
    /// derivation (seed from the plan, tier/footprint from the site, terrain mode on,
    /// street axis biased toward the first arriving road so the main street MEETS it).
    /// Pure; unit-pinned so a live build is reproducible from the plan alone.
    static nlohmann::json settlementParamsFor(const WorldForgePlan& plan,
                                              const WorldForgeSite& site);

    /// Sites in realization order: tier rank descending (town first), id ascending within
    /// a rank. Pure; unit-pinned.
    static std::vector<int> realizationOrder(const WorldForgePlan& plan);

    /// Start the self-chaining job. `siteFilter` empty = all plan sites; ledger-"built"
    /// sites are skipped (idempotent re-runs). Returns the job id; `immediateResult`
    /// carries the queued-site echo for the command response.
    static MainThreadJobs::Id start(MainThreadJobs& jobs, const Deps& deps,
                                    const std::vector<int>& siteFilter,
                                    nlohmann::json& immediateResult);
};

}  // namespace Core
}  // namespace Phyxel
