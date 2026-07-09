#pragma once

// ============================================================================
// StreetPaver — street paving as REAL geometry (lay_street_network #39 slice 2 /
// place_path #24 cut-cell closure). Turn a settlement's logical street rects +
// building door anchors into a PAVING PLAN: every micro column of every street
// (and a spur from each door to the nearest street) gets a target walkable
// surface, graded so a character can walk the whole network (planTerrainPath's
// slope-limited lower envelope along the street centerline, LEVEL cross-section
// across the width — a graded road, not a terrain-hugging ribbon).
//
// CUT columns (target surface BELOW the terrain top) are INCLUDED in the plan —
// the applier removes the terrain cubes above the surface, then paves. This
// closes the honest "cut_cells_unpaved" gap: the old ribbon stamper skipped
// them, leaving unwalkable terrain bulges at ~6% of hill transitions.
//
// PURE: terrain injected as a micro sampler; unit-testable with no engine
// (StreetPaverTest L2 coverage/continuity + L3 TraversalProbe end-to-end walk).
// Units: micro = 1/9 m. Conventions match the proven build_settlement stamper:
// a column's paving occupies micro rows [terrain top face .. surface] inclusive
// (>= 1 micro thick), so the walk top is surface+1 uniformly.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/BuildingProgram.h"  // Rect
#include "core/PathPlanner.h"      // DoorAnchor, AgentBox

namespace Phyxel {
namespace Core {

struct PavedColumn {
    int  x = 0;             ///< micro column coord (world, east)
    int  z = 0;             ///< micro column coord (world, north)
    int  surface = 0;       ///< target surface (micro): paving occupies [ground/base .. surface]
    bool cut = false;       ///< terrain top is ABOVE surface: remove cubes >= surface/9 first
};

struct PavingPlan {
    bool ok = false;
    std::string material = "Cobblestone";  ///< paving material (tier data; must exist in materials.json)
    std::vector<PavedColumn> columns;      ///< deduped micro columns, deterministic order
    long levelCols = 0;                    ///< surface == terrain top face (thin cap)
    long fillCols = 0;                     ///< surface above terrain (causeway fill)
    long cutCols = 0;                      ///< surface below terrain (cut, then cap)
    int  spursPlanned = 0;                 ///< door spurs graded
    int  spursFailed = 0;                  ///< door spurs too steep (surfaced, not silent)
};

/// Plan paving for a street network. `streets` are settlement-local street rects (cubes);
/// `originCubes` = the settlement origin (ox, oz) mapping them to world; `doors` are world-micro
/// door anchors (one per building, on its street-facing wall); `buildingFootprints` are world-cube
/// building rects whose INTERIOR (inset 1 cube) must never be paved (V7 — paths route around
/// buildings, meeting the door at the perimeter). `groundMicroAt(mx, mz)` = terrain top FACE in
/// micro at a micro column. Streets are graded along their long axis on the centerline
/// (planTerrainPath) and broadcast LEVEL across the width; spurs run door -> nearest street column,
/// meeting the STREET's planned surface (not raw terrain). First writer wins at junctions (street
/// surfaces dominate spur ends). Deterministic.
PavingPlan planStreetPaving(const std::vector<Rect>& streets, glm::ivec2 originCubes,
                            const std::vector<DoorAnchor>& doors,
                            const std::vector<glm::ivec4>& buildingFootprints,  // (x, z, w, d) world cubes
                            const std::function<int(int, int)>& groundMicroAt,
                            const AgentBox& box, const std::string& material);

}  // namespace Core
}  // namespace Phyxel
