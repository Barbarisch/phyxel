#pragma once

// ============================================================================
// BuildingProgramValidator — the PRE-BUILD gate (no voxels yet) for Structure
// Generation v2 (docs/StructureGenerationV2.md). Proves a BuildingProgram is
// legal before the realizer spends any work, and feeds the LLM repair loop.
//
// Gates:
//   * geometry  — rooms in-bounds, w/d > 0, no overlap; interior portals join
//                 adjacent rooms; exterior portals lie on the perimeter.
//   * function  — at least one exterior entrance; every room on every story is
//                 reachable from "exterior" via passable portals (door/arch) +
//                 stairs.
//   * scale     — story height >= ceiling min; door/arch openings >= the
//                 character's clear height/width (the character is the ruler).
//
// (Site/slope buildability is a build-time gate — terrain isn't known here.)
//
// Clean reimplementation of the v1 Python validator's intent; no v1 code reused.
// ============================================================================

#include "core/BuildingProgram.h"
#include "core/RoomProgram.h"
#include "core/ValidationReport.h"

namespace Phyxel {
namespace Core {

/// The character is the ruler. GROUNDED to building code + the engine grid (these
/// are anthropometric/gameplay clearances, not period values). The grid is integer
/// cubes for openings/story heights, so real metric mins are rounded to cubes and
/// the rounding is stated. A loader for resources/character_design_constraints.json
/// can populate `characterHeight` later.
struct CharacterScale {
    double characterHeight = 1.751;  ///< character_design_constraints.json (head_top)
    double ceilingMin      = 2.134;  ///< IRC R305.1 habitable min 7 ft = 2.134 m; a 2-cube
                                     ///< (2.0 m) story correctly fails -> min habitable = 3 cubes
    double doorClearMin    = 2.0;    ///< 2 cubes (2.0 m): the integer-cube floor that clears the
                                     ///< 1.751 character; real door/egress 80 in = 2.032 m (IRC
                                     ///< R311.2 / doors.com) rounds DOWN to 2 cubes on the 1 m grid
    int    doorWidthMin    = 1;      ///< 1 cube (1.0 m): integer-cube min; exceeds the real 32 in =
                                     ///< 0.81 m egress clear (IRC R311.2) — grid-locked
    double minRoomDim      = 2.0;    ///< min usable room short side (cubes) — anthropometric;
                                     ///< cf. IRC R304 habitable min 7 ft (2.134 m), grid-rounded
};

class BuildingProgramValidator {
public:
    /// Validate a program. If `roomProgram` is non-null, ALSO enforce the grounded,
    /// period typology sizing (footprint width range + length:width proportion bounds
    /// from room_program.json) and a usable min room dimension.
    static ValidationReport validate(const BuildingProgram& program,
                                     const CharacterScale& scale = {},
                                     const RoomProgram* roomProgram = nullptr);
};

} // namespace Core
} // namespace Phyxel
