#pragma once

// ============================================================================
// PlacedWorldDiff — close the CANVAS <-> WORLD seam.
//
// THE GAP THIS FILLS: the structure pipeline validates almost everything against
// the MicroCanvas -- the in-memory grid the realizer paints. Every L2 invariant
// (no overlap, continuity, clearance) and every L3 traversal probe reads the
// canvas. But the canvas is the PLAN. What the player walks around in is the
// STAMPED result of StructureGenerator::place(), and until now NOTHING compared
// the two. "L2 verified" has meant "the plan is correct", never "the artifact
// matches the plan".
//
// That seam is the root of a whole defect family: silent placement drops, the
// mixed-resolution overwrite problem (a finer voxel refusing to replace a coarser
// one, docs/MixedResolutionVoxelComposition.md), and every "OWED: an L2 scan of
// the PLACED chunk voxels" note left on the chimney, roof and signage placers.
// The world side was also unreadable at the right resolution: `scan_region` is
// CUBE-only, and `scan_region_micro` reports material COUNTS per cube, not which
// micro cells are occupied.
//
// The diff is deliberately ASYMMETRIC, because the two directions mean different
// things:
//   MISSING  - planned solid, world empty. A DROP: the artifact is less than the
//              plan. Always a defect.
//   EXTRA    - world solid, plan empty, inside the structure's own footprint.
//              Usually pre-existing terrain the build sits on/in, so the caller
//              supplies a Y floor (or ignores it); interesting mainly for spill.
//
// PURE: the world is injected as a micro-solidity predicate, so this is unit-
// testable headless against a synthetic world AND usable live against
// VoxelDynamicsWorld::anyStaticSolidInAABB (resolution-complete -- the same query
// the character collides with, and the one SpawnGate already relies on).
//
// Units: micro = 1/9 m. Canvas coords are structure-local; world coords are
// `originCubes * 9 + local`.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/MicroCanvas.h"

namespace Phyxel {
namespace Core {

/// solidMicroAt(x, y, z) -> is this WORLD micro cell solid? Must be resolution-
/// complete (cube + subcube + microcube), or the diff reports phantom drops for
/// every sub-cube voxel the query cannot see.
using SolidMicroFn = std::function<bool(int, int, int)>;

struct MicroCell {
    glm::ivec3 world{0};   ///< world micro coordinate
    glm::ivec3 local{0};   ///< canvas (structure-local) micro coordinate
};

struct PlacedDiff {
    std::vector<MicroCell> missing;  ///< planned solid, world empty (a DROP)
    std::vector<MicroCell> extra;    ///< world solid, plan empty (spill / pre-existing)

    long plannedCells = 0;           ///< occupied cells in the canvas
    long matchedCells = 0;           ///< planned cells found solid in the world
    bool truncated = false;          ///< a cap was hit; counts are partial (never silent)

    /// Fraction of the plan that actually made it into the world. 1.0 = nothing dropped.
    double fidelity() const {
        return plannedCells ? static_cast<double>(matchedCells) / plannedCells : 1.0;
    }
    bool ok() const { return missing.empty() && !truncated; }
    std::string summary() const;
};

/// Compare a realized canvas against the world it was stamped into.
///
/// `originCubes` is where the structure was placed (its canvas micro (0,0,0) maps to
/// `originCubes * 9`). `maxReported` caps each list so a catastrophically wrong build
/// cannot produce a gigabyte of JSON -- when hit, `truncated` is set and the counts say
/// so rather than quietly reporting a short list.
///
/// `checkExtra` is off by default: inside a building's footprint the world legitimately
/// contains the terrain the structure was seated on, so EXTRA is mostly noise unless the
/// caller has a reason to look (spill hunting).
PlacedDiff diffCanvasAgainstWorld(const MicroCanvas& canvas, const glm::ivec3& originCubes,
                                  const SolidMicroFn& solidMicroAt,
                                  size_t maxReported = 256, bool checkExtra = false);

/// Adapt an AABB solidity query (VoxelDynamicsWorld::anyStaticSolidInAABB) into a
/// per-micro-cell predicate. The cell [m, m+1) in micro units is [m/9, (m+1)/9) in
/// world units; the box is inset slightly so a cell only reads solid when geometry is
/// genuinely INSIDE it rather than merely touching its face.
SolidMicroFn microSolidityFromAABB(
    const std::function<bool(const glm::vec3&, const glm::vec3&)>& solidAABB);

}  // namespace Core
}  // namespace Phyxel
