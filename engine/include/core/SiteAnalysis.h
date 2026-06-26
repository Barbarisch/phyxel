#pragma once

// ============================================================================
// SiteAnalysis — analyze_site (#01), the KEYSTONE of terrain-aware settlement.
// Sample the terrain over a region and classify each cell's BUILDABILITY:
//   Flat       — near-level, ideal for a building
//   SlopeOk    — gentle grade, buildable with some cut/fill
//   TooSteep   — grade exceeds the buildable limit (skip)
//   Water      — under/at water (skip)
// Slope = the max ground-height delta to a 4-neighbour (cubes). Phases 2-4
// (terrain placement, paths, degradation) all consume this map.
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
    int          slope = 0;    ///< max |height - neighbour height| over the 4 neighbours (cubes)
    bool         water = false;
    Buildability cls = Buildability::Flat;
};

struct BuildabilityMap {
    int W = 0, D = 0;               ///< region size in cells
    int maxBuildableSlope = 0;      ///< the SlopeOk/TooSteep threshold used
    std::vector<SiteCell> cells;    ///< row-major [z*W + x]

    const SiteCell& at(int x, int z) const { return cells[static_cast<size_t>(z) * W + x]; }
    /// Fraction of cells that are Flat or SlopeOk (a quick "how buildable is this site" scalar).
    double buildableFraction() const;
};

/// Analyze a W×D cell region. `heightOf(x,z)` returns the ground top (cubes); `waterAt(x,z)` (optional)
/// returns whether the column is water. A cell is Water if waterAt; else Flat if slope <= flatSlope,
/// SlopeOk if slope <= maxBuildableSlope, else TooSteep. Deterministic.
BuildabilityMap analyzeSite(int W, int D, int maxBuildableSlope,
                            const std::function<int(int, int)>& heightOf,
                            const std::function<bool(int, int)>& waterAt = {},
                            int flatSlope = 1);

} // namespace Core
} // namespace Phyxel
