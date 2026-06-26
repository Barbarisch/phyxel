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

/// Site one building per plot: footprint = the plot inset by `setback` on every side (the yard that
/// leaves room for a path/garden between the building and the street). Plots too small for a building
/// + yard (footprint < minBuilding) are SKIPPED. Buildings inherit `typology`. Because each footprint
/// is contained in its plot and plots don't overlap, the buildings can't overlap or spill into a street.
std::vector<PlacedBuilding> populatePlots(const SettlementLayout& layout, int setback,
                                          int minBuilding, const std::string& typology = "hall_house");

} // namespace Core
} // namespace Phyxel
