#pragma once

#include <cstdint>
#include <vector>

namespace Phyxel {

// ── Priority-Flood depression filling (Barnes, Lehman & Mulla 2014) ──────────────────────────
//
// The load-bearing hydrology primitive (docs/TerrainGenerationV2.md §2b). Given a low-resolution
// heightfield with the grid EDGE treated as the outlet (ocean/world boundary), it floods inward
// from the edge with a min-priority-queue and raises every cell to the lowest level from which it
// can still drain monotonically to the edge. The result:
//   • `filled[i]` — the depression-filled elevation (>= the input; every cell now drains to the edge)
//   • a lake wherever `filled[i] > elevation[i]`: `filled[i]` IS the flat water-surface level, and the
//     depression's rim (its lowest spill saddle) comes for free — so lakes are flat and contained,
//     with no leaks (the #1 failure of naive noise-lakes).
//
// This is where "lakes need global knowledge" is resolved: run it ONCE on the coarse grid, then each
// streamed chunk reads the baked level locally. Complexity O(n log n) (a plain binary-heap variant;
// the integer O(n) radix variant is a later optimization). Pure + deterministic.
//
// Ordering note: pushing a monotonically-increasing sequence tag alongside the priority makes the
// pop order fully deterministic for equal elevations, so the fill is reproducible run-to-run.
class PriorityFlood {
public:
    // Fill depressions in a row-major (w×h) heightfield. Returns the filled field (same size).
    // The grid border cells are the drainage outlets. w,h must be >= 1; a degenerate 1-wide grid
    // returns a copy (every cell is already a border/outlet).
    static std::vector<float> fill(const std::vector<float>& elevation, int w, int h);

    // Convenience: per-cell water depth = filled - elevation (>= 0). depth[i] > 0 marks a lake cell
    // whose flat surface is at filled[i]; depth[i] == 0 is dry land.
    static std::vector<float> waterDepth(const std::vector<float>& elevation, int w, int h);
};

}  // namespace Phyxel
