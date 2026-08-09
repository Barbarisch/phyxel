#pragma once

// ============================================================================
// StructureForge — the staged v2 structure-build orchestrator (ForgePattern).
//
// Decomposes the former StructureBuildService::buildV2 monolith into named
// stages threaded over one context, with a StageReport gate protocol between
// them:
//
//   intake            parse BuildingProgram, resolve typology + style + position
//   floorplan         autofillRoomLayout (rooms + doors + windows + stairs)
//   validate_program  BuildingProgramValidator gate (repair-then-refuse)
//   validate_assets   every fixture THIS building's rooms need must resolve to a
//                     real asset — else record AssetRequests and REFUSE (never
//                     substitute, never half-furnish). Runs before any world edit.
//   footprint         context-aware overlap removal + object-wise vegetation gate
//   realize           StructureRealizer::realizeShell -> MicroCanvas + AssemblyPlan
//   validate_realized L2/L3 gates on the realized canvas (M1: empty anchor)
//   place             prepare_pad + grounding/vegetation gates + excavation +
//                     place + register + habitation metadata
//   furnish           engine-decided furniture / chimneys / surface items / signage
//   emit              phase timings + response assembly
//
// Every response carries "gates": [{stage, outcome, ms}] in stage order; a
// Refused stage returns its refusal json verbatim (plus the gates so far), so
// callers keep the exact error shapes buildV2 always produced.
//
// StructureBuildService::buildV2 is a thin wrapper over run() — the wire
// format and Deps are unchanged; SettlementBuildService and the API server
// need no edits. Behavior parity with the pre-restage monolith is pinned by
// BuildingHarness.ForgeParityDigests.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "core/StructureBuildService.h"

namespace Phyxel {
namespace Core {

class StructureForge {
public:
    /// Gate protocol: one report per executed stage. M1 policy is permissive —
    /// only the refusals the monolith always had (missing deps, realize failure,
    /// ungrounded footprint, vegetation, zero placement) act as Refused; the
    /// validation stages gain teeth in M3 (repair-then-refuse).
    struct StageReport {
        enum class Action { Proceeded, Repaired, Refused };
        Action action = Action::Proceeded;
        long long ms = 0;
        nlohmann::json refusal;   ///< returned verbatim when Refused
    };

    /// Full v2 build (the body formerly known as buildV2). Returns the API
    /// response json ({"success":true,...} or {"error":...}), always with
    /// "gates" attached.
    static nlohmann::json run(const nlohmann::json& params,
                              const StructureBuildService::Deps& deps);

    /// The canonical stage order (names as they appear in response["gates"]).
    static const std::vector<std::string>& stageNames();

    // ------------------------------------------------------------------
    // Signage (place_signage #47). A trade sign is a fine-voxel ITEM (a real
    // painted board), spawned through ItemPropManager; which item a typology
    // hangs is data (`RoomProgram::signItem`), never a name chosen here.
    // ------------------------------------------------------------------

    /// Which exterior wall the entrance door sits in, named by its OUTWARD normal.
    enum class WallSide { MinusX, PlusX, MinusZ, PlusZ };

    /// A planned sign pose, in ItemPropManager::spawnProp terms: `worldPos.x/z`
    /// is the board's horizontal CENTER and `worldPos.y` is where its bottom
    /// rests (a static prop rests exactly on pos.y).
    struct SignMount {
        bool        ok = false;
        std::string skipReason;      ///< why no sign was hung (never silent)
        std::string form;            ///< "projecting" (bracket) | "flush" (facade)
        int         rotationDeg = 0; ///< yaw about +Y
        glm::vec3   worldPos{0.0f};
        int         projectionMicro = 0;   ///< how far the board juts from the wall
        int         boardBottomMicroY = 0;
    };

    /// Choose the sign's mount BY FIT against the grounded projecting-sign code
    /// (>= 8 ft clearance, <= 48 in projection, above the door head — enforced by
    /// RealizedStructureValidator::checkSignClearance).
    ///
    /// A board narrow enough to swing on a bracket gets the authentic PROJECTING
    /// form. A board too wide for the projection cap is not skipped — it mounts
    /// FLUSH to the facade, which is a real signage form and keeps the painted
    /// face readable. Only a roof too low for either refuses.
    ///
    /// `wallOuterMicro` is the world micro-coordinate of the wall's OUTER face on
    /// its normal axis; `alongCenterMicro` the door's center on the other axis.
    /// Board dimensions are in world units, MEASURED from the item template
    /// (never assumed): `boardW` across the face, `boardH` up, `boardT` thickness.
    static SignMount planSignMount(WallSide side, int wallOuterMicro, int alongCenterMicro,
                                   int floorMicroY, int doorHeadMicroY, int roofApexMicroY,
                                   float boardW, float boardH, float boardT);

private:
    struct Context;   // defined in StructureForge.cpp (heavy members stay internal)

    static StageReport stageIntake(Context& ctx);
    static StageReport stageFloorplan(Context& ctx);
    static StageReport stageValidateProgram(Context& ctx);
    static StageReport stageValidateAssets(Context& ctx);
    static StageReport stageFootprint(Context& ctx);
    static StageReport stageRealize(Context& ctx);
    static StageReport stageValidateRealized(Context& ctx);
    static StageReport stagePlace(Context& ctx);
    static StageReport stageFurnish(Context& ctx);
    static StageReport stageEmit(Context& ctx);
};

} // namespace Core
} // namespace Phyxel
