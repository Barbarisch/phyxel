#pragma once

// ============================================================================
// AssetValidator — deterministic gates for generated OBJECTS (no LLM in the
// verdict). Structure Generation v2, the component/asset tier
// (docs/StructureGenerationV2.md).
//
// Measures a realized asset (a MicroCanvas) against its DimensionCanon archetype
// and returns a ValidationReport. An asset must pass these before it can even
// become 'provisional' in the trusted library; it only becomes 'approved' (and
// thus reusable by the realizer) after the gates + the comparative visual judge
// + a one-time user approval.
//
// Gates implemented here (A0):
//   * dimensional  — overall bounding size (height/width/depth/length/diameter)
//                    within canon +/- tolerance.
//   * structural   — base rests on the floor plane (no float), single connected
//                    component (no floating parts), voxel-count budget.
//   * functional   — required anchors/interaction points present.
//   * symmetry     — opt-in (archetype flag "symmetric"), advisory.
//
// (Internal feature dims like seat_top / table top height, and opening-clearance
// vs the character, are richer measures that arrive with the furnishing pass.)
// ============================================================================

#include <string>
#include <vector>

#include "core/MicroCanvas.h"
#include "core/DimensionCanon.h"
#include "core/ValidationReport.h"

namespace Phyxel {
namespace Core {

class AssetValidator {
public:
    struct Options {
        int maxVoxelBudget = 20000;   ///< warn (not fail) past this realized voxel count
        int expectedComponents = 1;   ///< default; overridden by archetype "components" if present
    };

    /// Validate `canvas` (the realized asset) against `arch`. `anchorIds` are the
    /// interaction-point ids the asset declares (for the required-anchor gate).
    static ValidationReport validate(const MicroCanvas& canvas,
                                     const ArchetypeDims& arch,
                                     const std::vector<std::string>& anchorIds = {},
                                     const Options& opts = {});

    /// Number of 6-connected components among the occupied micro cells.
    static int connectedComponents(const MicroCanvas& canvas);
};

} // namespace Core
} // namespace Phyxel
