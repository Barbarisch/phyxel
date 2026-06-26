#pragma once

// ============================================================================
// SettlementLayout — the settlement tier (the leap from one building to a town).
// subdivide_plots (#40): partition a settlement footprint into non-overlapping
// PLOTS separated by STREET corridors, each plot >= a min building size. The
// settlement analog of generateRoomLayout: deterministic, tiling-with-gaps.
//
// L2 invariants (gated by SettlementLayoutTest): plots don't overlap, all fit the
// footprint, each >= minPlot, and adjacent plots are separated by >= streetWidth
// (a character-walkable corridor). The emitted street bands feed the later L3
// walkability slice (a TraversalProbe walks the streets to every plot frontage).
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

} // namespace Core
} // namespace Phyxel
