#pragma once

// ============================================================================
// SiteAnalysis — analyze_site (#01), the KEYSTONE of terrain-aware settlement.
// Sample the terrain over a region and classify each cell's BUILDABILITY:
//   Flat       — near-level, ideal for a building
//   SlopeOk    — gentle grade, buildable with some cut/fill
//   TooSteep   — relief exceeds the buildable limit (skip)
//   Water      — under/at water (skip)
// RELIEF = max-min ground height over a building-footprint WINDOW around the cell
// (= the cut/fill a building there would need). A point 1-cube slope is the WRONG
// metric — smooth real terrain (Perlin/Mountains) reads as all-buildable at 1-cube
// spacing even when overall grade differs; the footprint-relief discriminates. A
// flat valley OR a hilltop PLATEAU has low relief (buildable); a slope/cliff/peak
// has high relief. Phases 2-4 (terrain placement, paths, degradation) consume this.
//
// PURE: the caller injects the terrain sampler (heightOf / waterAt) so this is
// unit-testable against synthetic terrain fixtures (a hill, a cliff, water, flat)
// with NO live engine; the runtime supplies a ChunkManager column-scan sampler
// (as seatStructure already does via hasVoxelAt).
// ============================================================================

#include <functional>
#include <vector>

namespace Phyxel {
namespace Core {

enum class Buildability { Flat, SlopeOk, TooSteep, Water };

struct SiteCell {
    int          height = 0;   ///< ground top (cubes) at this cell
    int          relief = 0;   ///< max-min ground height over the footprint window (cubes of cut/fill)
    bool         water = false;
    Buildability cls = Buildability::Flat;
};

struct BuildabilityMap {
    int W = 0, D = 0;               ///< region size in cells
    int maxBuildableRelief = 0;     ///< the SlopeOk/TooSteep relief threshold used
    int window = 0;                 ///< footprint half-window (cells) used for relief
    std::vector<SiteCell> cells;    ///< row-major [z*W + x]

    const SiteCell& at(int x, int z) const { return cells[static_cast<size_t>(z) * W + x]; }
    /// Fraction of cells that are Flat or SlopeOk (a quick "how buildable is this site" scalar).
    double buildableFraction() const;
};

/// Analyze a W×D cell region. `heightOf(x,z)` returns the ground top (cubes); `waterAt(x,z)` (optional)
/// returns whether the column is water. RELIEF(cell) = max-min height over the (2*window+1)² in-bounds
/// square centred on the cell — the cut/fill a building of ~that footprint would need. A cell is Water
/// if waterAt; else Flat if relief <= flatRelief, SlopeOk if relief <= maxBuildableRelief, else
/// TooSteep. `window` should be ~half the building footprint. Deterministic.
BuildabilityMap analyzeSite(int W, int D, int maxBuildableRelief,
                            const std::function<int(int, int)>& heightOf,
                            const std::function<bool(int, int)>& waterAt = {},
                            int flatRelief = 1, int window = 3);

} // namespace Core
} // namespace Phyxel
