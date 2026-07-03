#pragma once

// ============================================================================
// RealizedWorldValidator — POST-build quality gates that scan the REALIZED WORLD (placed fixtures +
// the raw voxels the generator stamped) rather than a single asset canvas. These catch the placement
// defects the user flagged that only exist once pieces are composed into a building/settlement:
//   V5  furniture-overlap    — two fixtures (or a fixture and a hearth) occupy the same space.
//   (more geometric world checks — chimney seating, grass-under-house, path-under-house — land here.)
// Each is a DETECTOR first: proven to FIRE on the current broken world (red) before any generator is
// touched. Methods take plain data (boxes / lambdas) so they unit-test without the engine and also run
// against the live world via the `validate_world` API. See docs/structure-generation/ValidationLedger.md.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ValidationReport.h"

namespace Phyxel {
namespace Core {

/// A placed fixture's world-space cube AABB (inclusive), with identity for reporting.
struct PlacedBox {
    std::string id;
    std::string type;    ///< fixture type: "bed", "chest", "fireplace", ...
    std::string parent;  ///< owning structure id (fixtures in different buildings can't overlap)
    glm::ivec3 min{0};
    glm::ivec3 max{0};
};

/// A realized structure's world AABB (the shell), for footprint-based world scans.
struct FootprintScan {
    std::string id;
    glm::ivec3 min{0};
    glm::ivec3 max{0};
};

/// Material lookup at a world CUBE coordinate — returns the material name, or "" if empty.
using MaterialAt = std::function<std::string(int, int, int)>;

/// Predicate: does world CUBE (x,y,z) contain `material` at ANY resolution (cube/subcube/microcube)?
/// (Structure walls/roofs/chimneys are sub/microcubes, invisible to a full-cube lookup.)
using CubeHasMaterial = std::function<bool(int, int, int, const std::string&)>;

/// Surface (highest solid terrain cube) Y at a world column, or INT_MIN if none found.
using SurfaceHeight = std::function<int(int, int)>;

/// A fence post cell: world position + the number of Log micro/subcubes filling that cube.
struct FencePost {
    glm::ivec3 pos{0};
    int logMicros = 0;
};

/// A placed chest: bbox center (floor y) + Y-rotation (0/90/180/270).
struct ChestPlacement {
    std::string id;
    glm::ivec3 center{0};
    int rotation = 0;
};

/// Predicate: is world cube (x,y,z) part of a WALL (structural, extends upward, not furniture)?
using WallAt = std::function<bool(int, int, int)>;

class RealizedWorldValidator {
public:
    // V5 + furniture overlap: flags any two NON-clutter fixtures whose world AABBs intersect. If either
    // is a hearth (fireplace/forge/oven) it is reported as `furniture_on_fireplace` (the user's
    // "furniture overlaps fireplaces"); otherwise `furniture_overlap` (e.g. a bed and a chest placed on
    // the same cell). Surface clutter (mug/bottle/plate) is excluded — it legitimately rests on
    // furniture (and sits a cube higher, so it doesn't share a cell anyway).
    static ValidationReport checkFurnitureOverlaps(const std::vector<PlacedBox>& items);

    // V10: grass under a house. When a building is placed the terrain grass beneath its floor must be
    // cleared — otherwise hidden grass blades emit inside/under the floor. Scans each structure's
    // footprint (x,z) for `Grass*` surface cubes in the `probeDepth` cube rows just below the floor
    // (structure min.y). Fires per structure that still has grass under it. (User: "grass should be
    // removed when covered by a house.")
    static ValidationReport checkGrassUnderFootprint(const std::vector<FootprintScan>& structures,
                                                     const MaterialAt& matAt, int probeDepth = 3);

    // V8: chimney must sit ON its hearth. For each fireplace/hearth fixture, checks that a Stone
    // chimney column rises directly above the hearth footprint (fp bbox x,z) within `rise` cubes of the
    // hearth top. Fires `chimney_offset_from_hearth` when Stone rises in the immediate neighborhood but
    // OUTSIDE the footprint (the observed (2,18,19) defect: hearth at x=1, stack at x=2), or
    // `chimney_missing` when no stack rises above the hearth at all. (User: "chimney doesn't always sit
    // on top of fireplace ... sometimes floating in the middle of a room.")
    static ValidationReport checkChimneyOverHearth(const std::vector<PlacedBox>& fireplaces,
                                                   const CubeHasMaterial& hasMat, int rise = 6);

    // V7: path under a house. A settlement path (Cobblestone) must route AROUND buildings, not through
    // them. Scans each structure's footprint INTERIOR (inset 1 from the walls, so a path merely meeting
    // the door at the perimeter is not flagged) for Cobblestone within `band` cubes of the floor. Fires
    // per structure with interior path cells. (User: "pathways sometimes go under houses.")
    static ValidationReport checkPathUnderFootprint(const std::vector<FootprintScan>& structures,
                                                    const CubeHasMaterial& hasMat, int band = 1);

    // V3: a house yard should be graded flat. Samples the terrain surface height in the ring around the
    // footprint (within `yardWidth` cubes, outside the footprint); fires when the sampled height span
    // exceeds `flatTol` cubes. (User: "yards for houses should probably also be flat.")
    static ValidationReport checkYardFlatness(const std::vector<FootprintScan>& structures,
                                              const SurfaceHeight& surfaceH, int yardWidth = 3,
                                              int flatTol = 2);

    // V6: fence corners must meet cleanly, sharing ONE corner post. At a corner (a post with fence
    // neighbours on BOTH axes) the two perpendicular runs must not each stamp a full picket section,
    // which doubles the Log micros there. Fires when a corner post's Log-micro count exceeds
    // `overlapFactor` x the average of its straight fence neighbours (self-calibrating, no magic
    // constant). Observed at (11,17,28): corner 94 micros vs ~45 on the straight runs.
    static ValidationReport checkFenceCornerOverlaps(const std::vector<FencePost>& posts,
                                                     double overlapFactor = 1.5);

    // NOTE: fence corner cleanliness (doubled/misaligned corner posts) is NOT validated by a world scan.
    // A picket fence's posts and infill slats are the SAME full-height column primitive, so per-cube Log
    // density can't distinguish a post from a slat (density-based detection is structurally unsound — it
    // desensitizes to noise and gives false confidence). The corner-cleanliness invariant is asserted
    // deterministically on the generator instead: Core::fencePostPositions (see FenceBuilderTest — posts
    // evenly spaced, never adjacent, shared corner via endPosts=false), plus a visual/screenshot check.

    // V1: fence floating over a path. Where a path crosses the fence line there must be a gate/gap, not
    // the fence sitting on top of it. Fires for any fence post with a Cobblestone (path) cell directly
    // below it (within `depth` cubes). Observed at (0,20,28): Log fence over a Cobblestone path at y19.
    // (User: "fences over paths look floating; we need a gate construct that straddles paths.")
    static ValidationReport checkFenceOverPath(const std::vector<FencePost>& posts,
                                               const CubeHasMaterial& hasMat, int depth = 2);

    // V2: fence along a terrain cliff. A fence should not run beside a steep terrain step — the cliff
    // is already the barrier, so a fence there is pointless. For each fence post, if an adjacent terrain
    // column is >= cliffTol cubes higher OR lower than the fence's ground, it's flagged. A 1-cube step
    // is a gentle slope (fences legitimately follow it); cliffTol=2 is a real cliff/rise. (User: "no
    // sense to have a fence along a cliff face, even if the fence is only 1 cube high.")
    static ValidationReport checkFenceAgainstRise(const std::vector<FencePost>& posts,
                                                  const SurfaceHeight& surfaceH, int cliffTol = 2);

    // V11: chest facing. The defect is a chest whose LID OPENS INTO A WALL. Clasp (front) direction from
    // rotation (rot0=+Z, 90=-X, 180=-Z, 270=+X, per the engine's rotateOffset transform). Fires only when
    // the clasp faces a wall (within `reach`) that is NEARER than the chest's back — i.e. the chest is
    // backwards, and flipping it would open into clearance. It does NOT fire on a chest that backs one
    // wall while a PERPENDICULAR wall sits alongside (a corner / narrow room): the clasp still opens into
    // the room. (The earlier "back must face the single nearest wall" rule mis-flagged those corners —
    // a chest inset against an exterior wall in its own perimeter cube reads as "back open" to an outward
    // scan while its clasp opens into the room.) (User: "chests seem to always be facing the wrong way" —
    // the root was the chest template's z-low clasp, since fixed so every chest opens +Z into the room.)
    static ValidationReport checkChestFacing(const std::vector<ChestPlacement>& chests,
                                             const WallAt& wallAt, int reach = 3);

    // Floor must be FLUSH with the yard — you should walk from the yard onto the floor without a step.
    // Compares each structure's floor cube level (bbox min.y) to the median yard terrain in the 1-ring
    // just outside the footprint; fires `floor_not_flush` when the step exceeds flushTol. (User: "you
    // shouldn't have to step down from yard terrain into a house; the top of the floor should align with
    // the top of the outside yard/terrain.") The terrace pass should grade the whole structure+yard unit
    // into the terrain so the floor and yard share one level.
    static ValidationReport checkFloorFlush(const std::vector<FootprintScan>& structures,
                                            const SurfaceHeight& surfaceH, int flushTol = 1);

    /// True for small surface props that legitimately rest on other furniture.
    static bool isClutter(const std::string& type);
    /// True for a vented hearth (fireplace/forge/oven) — furniture must not overlap it.
    static bool isHearth(const std::string& type);
    /// True for a terrain grass surface material (Grass / GrassForest / GrassSavanna).
    static bool isGrass(const std::string& material);
};

} // namespace Core
} // namespace Phyxel
