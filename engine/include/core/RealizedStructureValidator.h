#pragma once

// ============================================================================
// RealizedStructureValidator — POST-build quality gates that scan the REALIZED output (the painted
// MicroCanvas + the materials it actually used + the furniture/footprints chosen), catching the kinds
// of crude defects a real building would never pass:
//   V1 roof-eave-flush   — the roof must REST on the wall/ceiling top, not hover above it.
//   V3 material-contrast — a building can't be ~all one material (walls/floor/furniture all Wood).
//   V4 material-plausible — per-type material rules (a bed's bedding must not be stone-family).
//   V5 footprint-diversity — a generated SET must not be 100% rectangles (L/T/U variety).
// (V2 chimney-hearth contiguity lives with the chimney code.) Each is a DETECTOR first: proven to
// FIRE on the current broken output (red) before any generator is touched. See
// docs/structure-generation/ValidationLedger.md.
// ============================================================================

#include <climits>
#include <string>
#include <vector>

#include "core/MicroCanvas.h"
#include "core/ValidationReport.h"

namespace Phyxel {
namespace Core {

struct BuildingProgram;

class RealizedStructureValidator {
public:
    // V1: no hovering roof. For each PERIMETER column (where the wall makes a contiguous floor->top
    // solid), the roof's lowest solid directly above must rest with NO air gap on the wall/ceiling top.
    // `maxGapMicro = 0`: ANY empty micro row between the structural top and the roof is a hover defect
    // (a roof must touch its support). The realizer's `eaveSub = ceil(ceilTop/3)` rounding USED to leave
    // a 1-micro air row (the visible gap) — fixed to floor-div so the eave rests on the wall top; this
    // detector guards against regression. Returns an error per hovering column (capped).
    static ValidationReport checkRoofEaveFlush(const MicroCanvas& canvas, int maxGapMicro = 0);

    // V3: material variety. Flags when a single material covers more than `maxFraction` of the realized
    // cells (e.g. an all-Wood shell), so walls/floor/roof/furniture aren't an indistinguishable blob.
    static ValidationReport checkMaterialContrast(const MicroCanvas& canvas, double maxFraction = 0.70);

    // V4: per-type material plausibility. `type` = the furniture/fixture type (e.g. "bed"); `materials`
    // = the materials that piece is built from. Flags implausible choices — e.g. a bed whose bedding is
    // a STONE-FAMILY material (Sandstone/Stone/Cobblestone/StoneBricks/Bricks/Gravel) or Sand.
    static ValidationReport checkFurnitureMaterialPlausibility(const std::string& type,
                                                              const std::vector<std::string>& materials);

    // M1: flora (shrub/bush/tree/fern/flower) must not contain EMISSIVE (glow*) materials — plants do
    // not glow. `type` = the flora template/type; `materials` = materials it is built from. Flags every
    // emissive material found (the user's "some shrubs have light-emitting blocks in them" defect).
    static ValidationReport checkFloraNoEmissive(const std::string& type,
                                                 const std::vector<std::string>& materials);

    // M3: a fireplace/chimney's MASONRY should be brick (Bricks/StoneBricks), not plain quarried stone.
    // Applies ONLY to types containing "fireplace"/"chimney" (a forge/oven of stone is fine). Fuel Log +
    // ember glow are allowed; flags plain Stone/Cobblestone masonry. (User: "fireplaces and chimneys
    // should be brick (red brick).")
    static ValidationReport checkHearthMasonryIsBrick(const std::string& type,
                                                      const std::vector<std::string>& materials);

    // V5: footprint diversity across a generated SET. `footprintShapes` = the shape tag of each building
    // ("" or "rect" = rectangle; "L"/"T"/"U"/... = articulated). Flags a corpus that is 100%
    // rectangular — the generator must actually exercise non-rect plans.
    static ValidationReport checkFootprintDiversity(const std::vector<std::string>& footprintShapes);

    // V2: chimney must be PART OF the hearth, not a column overlapping it. The chimney stack's base must
    // sit at the hearth TOP (rest on the mantel) — never dive down through the hearth body / firebox
    // (overlap) nor float above it. Pass the hearth's base micro-Y + its height (micro) and the chimney
    // stack's lowest micro-Y; fires if |chimneyBaseY - hearthTopY| > tolMicro.
    static ValidationReport checkChimneyOnHearth(int hearthBaseMicroY, int hearthHeightMicro,
                                                 int chimneyBaseMicroY, int tolMicro = 1);

    // V6: a projecting/hanging shop sign must (a) clear heads — the board's BOTTOM hangs at least
    // `minClearanceMicro` above the ground the entrance sits on (default 22 micro ≈ 2.44 m / 8 ft, the
    // historic projecting-sign ground clearance), (b) not over-project — the board's far edge reaches
    // no more than `maxProjectionMicro` out from the wall face (default 11 micro ≈ 1.22 m / 48 in), and
    // (c) hang ABOVE the door head when one is given (`doorHeadMicroY` != INT_MIN) — a sign whose board
    // bottom is below the lintel reads as obscuring the doorway, not crowning it.
    // `boardBottomMicroY`/`groundMicroY`/`doorHeadMicroY` are absolute micro-Y; `projectionMicro` is the
    // board far-edge distance from the mount wall (micro). Fires (a) too low / below grade, (b) juts too
    // far, (c) below the door head.
    static ValidationReport checkSignClearance(int boardBottomMicroY, int groundMicroY,
                                               int projectionMicro, int minClearanceMicro = 22,
                                               int maxProjectionMicro = 11,
                                               int doorHeadMicroY = INT_MIN);

    // M3 L3 gate (validate_realized): a character-box must physically reach EVERY room on EVERY
    // story of the REALIZED shell from the entrance room — through the carved doorways and the
    // built stairs (TraversalProbe over the canvas occupancy). Belt-and-suspenders behind the
    // topological program gate: that one proves the PLAN links up; this one proves the BUILT
    // geometry does (catches carve failures, blocked openings, unbuilt flights).
    // `floorTopByStory` = each story's walkable surface micro-Y (ShellResult.floorTopByStory).
    static ValidationReport checkShellTraversal(const MicroCanvas& canvas,
                                                const std::vector<int>& floorTopByStory,
                                                const BuildingProgram& program);

    /// True if `material` is a hard stone-family / granular material implausible for soft furnishings.
    static bool isStoneFamily(const std::string& material);

    /// True if `material` is an emissive/glow material (glow, glow_blue, glow_green).
    static bool isEmissive(const std::string& material);
};

} // namespace Core
} // namespace Phyxel
