#pragma once

#include <functional>
#include <vector>

namespace Phyxel {

// ── WATER AS OCCUPANCY (docs/Water.md §5) ─────────────────────────────────────────
//
// USER DIRECTIVE: *"water must exist on top of terrain. it should never be possible to create a body
// of water that isnt tied to the physical boundaries of a terrain"* and *"water bodies should be
// defined by the terrain holding them, not the other way around."*
//
// Water stops being a surface painted over the world and becomes DATA IN the world. The model is
// full 3D occupancy; a per-column run-length encoding is the storage, because a column's water is
// contiguous runs and runs are the natural encoding of exactly the same information. That keeps
// cave lakes (disjoint runs), water resting on water (stacked runs) and fractional surfaces, while
// avoiding millions of voxel objects in an ocean.
//
// WHAT THIS REPLACES: today the far field draws a camera-following sheet at a level taken from a
// COARSE 128 m hydrology bake, with no reconciliation against the per-voxel terrain. Measured
// consequence in one view: 606 of 606 rim columns leaking, terrain up to 38 voxels below the water
// level beside it, water rendering over a hillside.

// One contiguous run of water in a single column.
// Forward-declared so BasinFill can use it above its definition point.
inline constexpr float kMinSpanDepthFwd = 0.05f;

struct WaterSpan {
    // The first WATERY cell. It rests directly on solid terrain, or on the top of the span below.
    int   bottomY = 0;
    // World Y of this run's surface. FLOAT on purpose: the CA models a fractional fill in the top
    // cell, so a settled simulation round-trips into storage without being quantised to whole
    // voxels — and a lake surface at 148.9 stays at 148.9 rather than snapping to 148 or 149.
    float topY = 0.0f;

    bool valid() const { return topY > static_cast<float>(bottomY); }
    float depth() const { return topY - static_cast<float>(bottomY); }
};

// Build the water run for a column of OPEN-SURFACE water — the common case: one body of water lying
// on the terrain surface, open to the sky.
//
//   surfaceY  — the column's REAL topmost solid voxel, straight from terrain generation.
//   bodyLevel — the surface height of the body this column belongs to.
//
// Returns false (and leaves `out` untouched) when this column holds no water.
//
// ⚑THE INVARIANT IS STRUCTURAL, NOT CHECKED AFTER THE FACT. The span is expressed RELATIVE to the
// column's own `surfaceY`: its bottom is `surfaceY + 1` and nothing else, so "water untied to
// terrain" is not representable — which is what the directive demands. A validator that merely
// *detects* floating water afterwards is what the engine had, and it reported 100% failure for weeks.
//
// ⚑ONE QUALIFICATION, because this was stated as an absolute and an absolute was disproved: the
// `+1` is signed-int arithmetic, and at `surfaceY == INT_MAX` it wraps, which DID produce a span
// billions of units below its terrain. `surfaceY` is therefore bounded in the implementation and
// pinned by ExtremeSurfaceYCannotOverflowIntoAFloatingSpan. Unreachable from real terrain (peaks
// are ~384 above sea level) — but "no arithmetic path" has to mean none.
//
// ⚑SCOPE, stated plainly: this handles water OPEN TO THE SKY only. Cave lakes and water under
// overhangs are genuinely multiple runs per column and need the column's full solidity profile, not
// just its topmost solid voxel. That is a later increment; this function deliberately cannot express
// them rather than expressing them wrongly.
bool buildOpenWaterSpan(int surfaceY, float bodyLevel, WaterSpan& out);

// ── EXTENT: which columns a body actually reaches (docs/Water.md §5.5) ────────────
//
// ⚑THIS IS THE DOMINANT DEFECT, and it is the opposite of the one buildOpenWaterSpan fixes.
// Measured over one region: only **71 of 17,265** baked-wet columns (0.41%) have ground standing
// above the water — so "water drawn on top of land" is rare. But **606 of 606** rim columns leak:
// ground that sits BELOW the waterline, marked dry by the bake, getting no water. The visible
// result is dry lake bed 30 voxels beneath a 148.9 waterline and a sheet cut off at a 128 m cell
// edge. The body's EXTENT is wrong, not its containment.
//
// The bake decides extent on a 128 m grid; the shoreline is a per-voxel contour. Refining it means
// answering, per column: *is this column part of that body?* — which is a connectivity question.
//
// ⚑SEAM-FREEDOM BY DEFINITION, NOT BY WINDOW. This is phrased as a property of the WORLD — "is
// there a path from this column to baked water, every step of which lies below the body's surface,
// within `maxSteps`" — so the answer depends only on (terrain, bake, maxSteps). Two chunks
// generated in different orders, or a query from a different window, must agree, or shorelines tear
// at chunk borders. A chunk-local flood would NOT have this property.
//
// ⚑BOUNDED ON PURPose. A true global flood is ~1e9 columns over a 32 km world. `maxSteps` caps the
// search; a column further than that from baked water keeps the bake's answer. That is a real
// limitation — a very gently sloping shore can extend beyond the bound — and the scan probe reports
// what is still unresolved rather than hiding it.
struct ColumnTerrain {
    // Real topmost solid voxel at a world column — the generator's own surfaceY, not a coarse model.
    std::function<int(int, int)> groundY;
    // The bake's proposed surface for a column, or HydrologyMap::NO_WATER when it proposes nothing.
    std::function<float(int, int)> bakedLevel;
};

// ── THE BASIN IS THE PRECONDITION (docs/Water.md §5.0) ────────────────────────────
//
// USER DIRECTIVE: *"It should be impossible to just add water without a basin to put it in."*
//
// ⚑WHY THIS EXISTS, AND WHAT IT FIXES ABOUT MY OWN EARLIER API. `buildOpenWaterSpan` takes the
// surface level as a PARAMETER. It refuses to put water below ground, so water cannot float — but
// nothing stops a caller passing level = 1000 over a flat plain and getting a 984-deep span.
// "Water rests on terrain" is a weaker statement than "terrain HOLDS water", and only the second
// one is what a body of water actually is. Passing a level in is the loophole.
//
// So the level is not an input here. It is DERIVED from the container: the height at which the
// terrain around this column stops holding — its spill point. Flat ground has no spill above its
// own surface, so flat ground yields NO WATER, by construction rather than by a check.
//
// This is a local Priority-Flood, the same idea the coarse bake uses, run against REAL per-column
// terrain: expand outward keeping, for each path, the highest ground it had to cross; the first
// escape from the search bound gives the lowest such barrier, which IS the spill height.
struct BasinFill {
    // The surface the terrain supports — the basin's spill height. Water fills to exactly here.
    float level = 0.0f;
    // The seed's own ground. `level - ground` is the depth; equal means a basin that holds nothing.
    float groundY = 0.0f;
    // The flood escaped the search bound without being enclosed: the basin is larger than the
    // budget and its true spill is unknown. Reported rather than guessed at.
    bool  unresolved = false;
    float depth() const { return level - groundY; }
    bool  holdsWater() const { return !unresolved && depth() >= kMinSpanDepthFwd; }
};

// Find what the terrain at this column actually holds. Returns false when there is no container —
// flat ground, a slope, or a rim lower than the seed. `maxSteps` bounds the search.
bool fillBasinAt(int worldX, int worldZ, const ColumnTerrain& terrain, int maxSteps, BasinFill& out);

// The surface of the body this column belongs to, or NO_BODY when it belongs to none.
// A column qualifies when its own ground is below that surface AND a path of equally-submerged
// columns links it to a column the bake already calls wet.
inline constexpr float kNoBody = -1e30f;
float connectedBodyLevel(int worldX, int worldZ, const ColumnTerrain& terrain, int maxSteps);

// ── BATCH: one flood for a whole block of columns ────────────────────────────────────────────
//
// ⚑WHY THIS EXISTS: `connectedBodyLevel` answers one column and re-walks the terrain every time.
// Measured at **3.4 ms per column**, which is ~1000x too slow for generation — a chunk is 1,024
// columns, so ~3.5 s of flood per chunk. The waste is structural, not constant-factor: 1,024
// separate floods over the same neighbourhood re-sample the same ground thousands of times.
//
// Inverting it fixes that. Seed a SINGLE multi-source flood from every baked-wet column in the
// block at once and let it expand into submerged ground; every column is then resolved in one
// sweep, and each terrain height is sampled exactly once. Same answer, O(area) instead of
// O(area x flood).
//
// Operates on PRE-SAMPLED GRIDS rather than callbacks, deliberately: the caller (generation)
// already has these heights, and passing raw arrays keeps the hot loop free of std::function
// indirection. All grids are row-major, index = z*w + x.
//
//   groundTop  — the column's real surface, as a TOP FACE height (surfaceY + 1).
//   bakedLevel — the bake's proposed surface, or kNoBody where it proposes nothing.
//   outLevel   — receives each column's body surface, or kNoBody where the column holds no water.
//
// The rule is connectedBodyLevel's: a column joins a body when its ground lies below that body's
// surface AND a chain of equally-submerged columns links it to baked water within `maxSteps`.
// Nearest body wins, because the flood expands breadth-first from all seeds simultaneously.
//
// ⚑`maxSteps` IS A CORRECTNESS REQUIREMENT, NOT A PERFORMANCE KNOB. An unbounded flood spreads to
// whatever the sampled grid happens to contain, which makes a column's answer depend on the SIZE OF
// THE BLOCK it was computed in — so two chunks resolve their shared shoreline differently and tear
// at the seam. Bounding it makes the result a function of (terrain, bake, maxSteps) only, which is
// the seam-freedom property §5.5 demands. This was a real defect in the first version of this
// function, caught by comparing against the per-column query it replaces.
//
// ⚑EDGE COLUMNS ARE UNDER-RESOLVED BY CONSTRUCTION. A body whose baked seed lies outside the grid
// cannot be reached, so columns within `maxSteps` of the border may read dry when the world says
// otherwise. Callers must pad the region they care about by at least `maxSteps` and discard the
// padding's answers — with that padding, the two properties above combine to give an answer
// identical to a whole-world flood bounded the same way.
void floodBodiesOverGrid(int w, int d, const float* groundTop, const float* bakedLevel,
                         float* outLevel, int maxSteps);

// Minimum depth (world units) for a column to be considered water at all. Below this a "body" is a
// film thinner than the sim's own MIN_HOLD and would render as z-fighting shimmer on the ground
// rather than as water.
inline constexpr float kMinSpanDepth = 0.05f;

}  // namespace Phyxel
