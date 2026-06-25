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

    /// Micro thickness (cells) for a thickness given in cubes; at least 1 cell.
    static int thicknessMicro(double cubes);
};

} // namespace Core
} // namespace Phyxel
