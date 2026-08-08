#pragma once

// ============================================================================
// SettlementBuildService — the ONE engine entry point for composing a whole
// settlement into the live world. The settlement-scale sibling of
// StructureBuildService (which owns the single-building pipeline).
//
// WHY THIS EXISTS: this logic used to live inside the editor's build_settlement
// command handler (~980 lines in editor/src/Application.cpp). Every dependency it
// needs was ALREADY engine-side, so it was located in the editor rather than
// coupled to it — and that location cost real capability:
//   * no headless test surface for the COMPOSITION itself (settlement tests could
//     only exercise the pure planners it calls, never the handler's own
//     assembly), so fixes landed as untestable edits inside lambdas;
//   * a shipped game could not generate a settlement at all — phyxel_core had no
//     settlement entry point, so GameApiService could never reach one;
//   * terrain-mode walkability could not be validated headlessly, because the
//     terrain sampling lived in the handler.
//
// The pipeline, unchanged:
//   grounding gate -> program (era/tier) resolution -> layout (main-street /
//   city / terrain-scatter / legacy grid) -> per-plot building assignment ->
//   WORK UNITS: clear parcels -> terrace -> pave streets/paths -> fence ->
//   yard props + well -> one v2 build per building -> street sweep -> nav
//   rebuild -> spawn residents.
//
// SLICING CONTRACT: plan() does the fail-fast planning inline and returns the
// heavy phases as ordered WORK UNITS instead of running them. The CALLER decides
// how to drive them — the editor queues them on MainThreadJobs (one per frame,
// [no-frozen-engine]); a test or a headless host can just run them in order.
// This is the seam that keeps the job system out of the engine service.
// ============================================================================

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
class ChunkManager;
class ObjectTemplateManager;

namespace Core {
class PlacedObjectManager;
class LocationRegistry;
class NPCManager;

/// Top solid voxel of a WORLD column — the elevation primitive the whole settlement
/// pipeline seats against. `floraBlind` skips flora so ELEVATION decisions (grading,
/// seating, terracing, buildability, fences) never read a canopy as ground; the raw
/// scan answers "what physically occupies this column" (clearing).
///
/// floraBlind requires a real CUBE: terrain is always full cubes, so a present-but-
/// cubeless cell is sub/micro content (tree branches, fences, paving) and is never
/// ground. An earlier version filtered only flora CUBES, and a spruce's micro
/// mid-section read as a hill — the street grader paved a bump OVER the tree and the
/// road stayed nav-blocked underneath.
int settlementTopScan(ChunkManager* cm, int oy, int wx, int wz, bool floraBlind);

class SettlementBuildService {
public:
    struct Deps {
        ChunkManager* chunkManager = nullptr;            ///< REQUIRED for anything world-touching
        PlacedObjectManager* placedObjects = nullptr;    ///< optional: no props/registration without it
        ObjectTemplateManager* templates = nullptr;      ///< optional: props/furniture need it
        LocationRegistry* locations = nullptr;           ///< optional: location auto-registration
        NPCManager* npcs = nullptr;                      ///< optional: nav rebuild + residents
        /// Optional undo hook (the editor snapshots each region before destructive ops).
        std::function<void(const glm::ivec3& minCorner, const glm::ivec3& maxCorner,
                           const std::string& label)> pushUndo;
    };

    /// One deferred phase of the build. `label` is the progress name shown to the user.
    struct WorkUnit {
        std::string label;
        std::function<void()> run;
    };

    /// The planned settlement: a fail-fast result plus the deferred heavy phases.
    struct Plan {
        nlohmann::json error;          ///< non-null => planning FAILED; units is empty
        nlohmann::json settlement;     ///< {plots, streets, buildings, origin}
        nlohmann::json program;        ///< {era, tier, seed, morphology, ...} when program mode
        nlohmann::json queuedBuilds;   ///< array: one entry per building to be built
        std::string jobLabel;          ///< human label for the caller's job/progress system
        std::vector<WorkUnit> units;   ///< run IN ORDER (site prep precedes buildings)

        /// Per-phase results the units fill in as they run. Shared so the caller can
        /// fold them into a response AFTER the units have executed (they are empty at
        /// plan() time — reading them before running the units yields nothing).
        std::shared_ptr<nlohmann::json> paths;
        std::shared_ptr<nlohmann::json> yardProps;
        std::shared_ptr<nlohmann::json> residents;
        /// M3 repair-then-refuse: lots whose build REFUSED and whose one re-rolled
        /// variant ALSO refused (array of {lot, typology, retry_typology, error,
        /// retry_error}). Empty = every lot built (possibly on its re-roll). Filled
        /// by the building units as they run.
        std::shared_ptr<nlohmann::json> lotFailures;

        bool ok() const { return error.is_null(); }
    };

    /// Plan a settlement from build_settlement params. Planning (grounding gate, program
    /// resolution, layout, building assignment) runs INLINE and can fail fast; the heavy
    /// phases come back as `units` for the caller to drive. Deterministic in the params'
    /// seed. Does not touch the world until a unit is run.
    static Plan plan(const nlohmann::json& params, const Deps& deps);
};

} // namespace Core
} // namespace Phyxel
