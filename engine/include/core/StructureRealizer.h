#pragma once

// ============================================================================
// StructureRealizer — the in-engine, ground-up shell builder for Structure
// Generation v2 (docs/structure-generation/StructureGenerationV2.md). P1: single-story shell on FLAT
// ground (passes 0-5: substructure -> floor -> walls -> ceiling -> roof, plus
// carved openings). Terrain adaptation (stepped footings, excavation) is P2.
//
// It paints into a MicroCanvas so walls/floors/roof are SUBCUBE-thick (the v2
// anti-Minecraft rule): NO full-cube 1 m walls. Thicknesses + materials come
// from the StyleProfile; geometry from the BuildingProgram. The result also
// carries an AssemblyPlan (the derived anatomy) for the geometry gates.
//
// Output is in a LOCAL frame: y=0 at the bottom of the foundation; world
// placement/offset happens at spawn time (P1b).
// ============================================================================

#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/StyleProfile.h"
#include "core/AssemblyPlan.h"
#include "core/MicroCanvas.h"
#include "core/StructureGenerator.h"   // StructureResult / VoxelPlacement (engine placement path)

namespace Phyxel {
namespace Core {

class StructureRealizer {
public:
    struct ShellResult {
        MicroCanvas  canvas;            ///< the painted shell (local coords)
        AssemblyPlan plan;             ///< derived anatomy (for gates/inspection)
        int  floorTopMicro = 0;        ///< micro Y of the walkable finish-floor surface (story 0)
        std::vector<int> floorTopByStory;  ///< micro Y of EACH story's walkable surface (per-story)
        int  crawlHeightCubes = 0;     ///< foundation/crawlspace height below the floor
        bool ok = false;
        std::string error;
    };

    /// Realize a single-story shell from `program` using `style`. P1: flat ground.
    static ShellResult realizeShell(const BuildingProgram& program, const StyleProfile& style);

    /// Convert a realized shell's MicroCanvas into engine VoxelPlacements offset to
    /// `worldOrigin` (the canvas's local y=0 maps to worldOrigin.y), for placement via
    /// StructureGenerator::place / VoxelModificationSystem.
    static StructureResult toStructureResult(const ShellResult& shell, const glm::ivec3& worldOrigin);

    /// Derive the building's schedule-target location marker (playable-town): one
    /// LocationMarker per building, typed from `typology` (tavern->Tavern, shop/smith->
    /// Work, dwelling->Home, else Custom), anchored just OUTSIDE the ground-story
    /// exterior door — an outdoor cell the 2.5D NavGrid can reach (interior columns
    /// read as roof there). Falls back to the footprint centre at floor level when the
    /// plan has no perimeter door. Pure (no engine deps) — unit-testable.
    static std::vector<LocationMarker> deriveLocations(const BuildingProgram& program,
                                                       const std::string& typology,
                                                       const AssemblyPlan& plan,
                                                       const glm::ivec3& worldOrigin,
                                                       int floorTopMicro);

    /// Micro thickness (cells) for a thickness given in cubes, CLAMPED to [1,9] (min 1 cell, max one
    /// full cube). The furniture pass uses this same converter for its wall-inset so the inset matches
    /// the realized wall band exactly (a raw >9-micro value would over-inset furniture off a clamped
    /// 1-cube wall).
    static int thicknessMicro(double cubes);
};

} // namespace Core
} // namespace Phyxel
