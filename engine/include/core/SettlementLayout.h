#pragma once

// ============================================================================
// SettlementLayout — the settlement tier (the leap from one building to a town).
// subdivide_plots (#40): partition a settlement footprint into non-overlapping
// PLOTS separated by STREET corridors, each plot >= a min building size. The
// settlement analog of generateRoomLayout: deterministic, tiling-with-gaps.
//
// L2 invariants (gated by SettlementLayoutTest): plots don't overlap, all fit the
// footprint, each >= minPlot, and adjacent plots are separated by a >= streetWidth
// GEOMETRIC GAP. (L2 = the gap is wide enough on paper; actually WALKING it with a
// TraversalProbe is the deferred L3 slice — not proven here.) The emitted street
// bands are the corridors that L3 slice will probe.
// ============================================================================

#include <vector>

#include "core/BuildingProgram.h"   // Rect
#include "core/SiteAnalysis.h"      // BuildabilityMap (terrain-aware placement, Phase 2)

namespace Phyxel {
namespace Core {

struct Plot {
    Rect rect;     ///< the buildable footprint of this plot (cubes), local to the settlement origin
    int  row = 0;  ///< grid row/col (for addressing / frontage)
    int  col = 0;
};

struct SettlementLayout {
    std::vector<Plot> plots;    ///< non-overlapping buildable plots
    std::vector<Rect> streets;  ///< street corridors between/around the plots (>= streetWidth wide)
};

/// Partition a W×D (cubes) settlement footprint into a cols×rows grid of plots separated by
/// `streetWidth`-wide street corridors. Each plot is >= minPlot per side. Returns an EMPTY layout
/// (caller reduces density) if the grid can't fit every plot at >= minPlot. Deterministic.
SettlementLayout subdividePlots(int W, int D, int cols, int rows, int streetWidth, int minPlot = 6);

/// One building sited on a plot (populate_plots #45).
struct PlacedBuilding {
    int         plotIndex = 0;
    Rect        footprint;   ///< building footprint (settlement-local), inset within the plot by the yard
    std::string typology;    ///< RoomProgram typology to build (passthrough to the building generator)
};

/// TERRAIN-AWARE plot placement (Phase 2): choose up to `maxPlots` non-overlapping plotSize×plotSize
/// plots whose ENTIRE footprint is buildable (no TooSteep/Water cell in `site`), separated by >=
/// `spacing` cells (the street/yard gap), preferring the FLATTEST spots (lowest total relief). On
/// mostly-unbuildable terrain (a steep mountain) it returns few/none — graceful degradation, not a
/// broken result. Plot rects are in site-cell coords. Deterministic (flattest-first, stable tiebreak).
std::vector<Plot> selectBuildablePlots(const BuildabilityMap& site, int plotSize, int spacing,
                                       int maxPlots);

/// Site one building per plot: footprint = the plot inset by `setback` on every side. A positive
/// setback leaves a yard (room for a path/garden) between the building and the street; setback=0 is
/// allowed and puts the building FLUSH to the plot edge (an urban row-house — NO yard, but still clear
/// of the street, since the plot itself starts a streetWidth in). Plots too small for the building
/// (footprint < minBuilding) are SKIPPED. Buildings inherit `typology`. Each footprint is contained in
/// its plot and plots don't overlap, so the buildings don't overlap each other or sit in a street.
std::vector<PlacedBuilding> populatePlots(const SettlementLayout& layout, int setback,
                                          int minBuilding, const std::string& typology = "hall_house");

/// One building's chosen VARIATION (so a village isn't N identical boxes). Each dimension is picked
/// deterministically + independently from the plot index, so neighbours differ in typology AND style
/// AND footprint shape, reproducibly.
struct BuildingVariant {
    std::string typology;        ///< from the typologies palette (room layout)
    std::string style;           ///< from the styles palette (wall/roof material + roof form)
    std::string footprintShape;  ///< "rect" | "L" (non-rectangular winged plan)
};

/// Pick a deterministic, varied building for `plotIndex` from the typology + style palettes (cycled via
/// independent hashes so the three dimensions vary independently). ~1/3 of buildings get an L-plan
/// footprint. Empty palettes fall back to sane defaults. Deterministic in (plotIndex, seed).
BuildingVariant pickBuildingVariant(int plotIndex, const std::vector<std::string>& typologies,
                                    const std::vector<std::string>& styles, unsigned seed);

} // namespace Core
} // namespace Phyxel
