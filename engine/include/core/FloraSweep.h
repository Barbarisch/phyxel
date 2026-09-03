#pragma once

// ============================================================================
// FloraSweep — the ORPHANED-CANOPY sweep (user find 2026-08-27: "leftover
// foliage and pieces of trees are floating in the air after placing things").
//
// Every site-prep pass in the settlement pipeline clears flora inside its OWN
// band: the parcel clearer wipes plot boxes, the street grader clears the road
// corridor's headroom, building pads cut their footprint. A tree whose TRUNK
// stood inside one of those bands loses the trunk and keeps everything that
// reached outside it — a canopy hanging in the air with nothing under it.
//
// This is the general fix: after clearing, find tree matter that can no longer
// reach the ground THROUGH tree matter, and remove it. Pure and world-free —
// the caller supplies the probes, so the rule is unit-testable on a synthetic
// lattice and the same code runs against live chunks.
//
// CONSERVATIVE BY CONSTRUCTION (the false-positive that matters): a component
// touching the scan box's boundary is LEFT ALONE — its support may simply lie
// outside the box (a healthy neighbouring tree's overhang reaching in). Only
// components proven fully enclosed AND unsupported are swept. Widen the box to
// sweep more; never trade a real tree for a floating branch.
// ============================================================================

#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

/// Inclusive cube-coordinate scan box.
struct SweepBounds {
    glm::ivec3 min{0};
    glm::ivec3 max{0};
    bool contains(const glm::ivec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
    /// A cell on the box's outer shell — connectivity may continue past it unseen.
    bool onBoundary(const glm::ivec3& p) const {
        return p.x == min.x || p.x == max.x || p.y == min.y || p.y == max.y ||
               p.z == min.z || p.z == max.z;
    }
};

/// Cells of tree matter inside `bounds` that no longer reach support, as one flat
/// list (caller removes them).
///
/// `isFlora(p)`  — p holds tree matter (Log*/Leaf*, any resolution).
/// `isSolid(p)`  — p holds ANY solid content (terrain, structure, flora).
///
/// SUPPORTED means: some cell of the component has a non-flora solid cell directly
/// beneath it (it stands on ground or on a building), OR the component reaches the
/// box boundary (support unknown — never guessed away). Everything else is orphaned
/// canopy and is returned. Deterministic: results come back in scan order.
std::vector<glm::ivec3> planOrphanedFloraSweep(
    const SweepBounds& bounds,
    const std::function<bool(const glm::ivec3&)>& isFlora,
    const std::function<bool(const glm::ivec3&)>& isSolid);

} // namespace Core
} // namespace Phyxel
