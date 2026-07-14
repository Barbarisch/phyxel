#pragma once

#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include "core/KinematicVoxelManager.h"   // KinematicVoxel

namespace Phyxel {
namespace Core {

/// One box of a compound rigid-body fragment (a greedy-merged run of voxels).
struct FragmentBox {
    glm::vec3 center;       ///< center in the fragment's local space
    glm::vec3 halfExtents;
    float     mass;         ///< aggregate mass of the voxels this box covers
};

/// Budget governing coherent fragments (docs/DestructionSystemV2.md §8, §5.G).
/// The numeric ceilings here are PLACEHOLDERS until the Phase-1 Release benchmark
/// (P1.3) measures the real CPU-body limit — do NOT treat them as grounded.
struct FragmentBudget {
    int maxActiveBodies  = 32;      ///< max concurrently-ACTIVE coherent bodies (retirement frees slots)
    int maxVoxelsPerBody = 4000;    ///< a bigger severed set falls back to particle scatter
};

/// Shared machinery for turning a connected set of voxels into a coherent falling
/// rigid body (docs/DestructionSystemV2.md §5.B). Generalized out of
/// DynamicFurnitureManager::shatter so furniture fracture AND world collapse share
/// ONE implementation. This first slice (P1.1) is the pure GEOMETRY half — the
/// greedy box merge — with a caller-supplied mass model (H4: furniture and world
/// weigh voxels differently, so mass must not be a baked-in constant).
class CoherentFragmentService {
public:
    /// Greedy-merge local-space voxels (mixed cube / subcube / microcube — each
    /// carries its own scale) into a minimal set of boxes for a compound rigid
    /// body. `voxelMass` returns the TOTAL mass of one voxel; the caller picks the
    /// model:
    ///   - furniture: material.mass * 0.05  (a light game fudge)
    ///   - world    : material.mass * voxelVolume  (physically weighted)
    /// The per-voxel mass is distributed over the grid cells the voxel covers and
    /// re-summed per merged box, so total mass is conserved.
    ///
    /// Pure geometry: no physics-world / registry dependency (the mass fn injects
    /// any material lookup). Deterministic. Empty input → empty output.
    static std::vector<FragmentBox> mergeVoxelsToBoxes(
        const std::vector<KinematicVoxel>& voxels,
        const std::function<float(const KinematicVoxel&)>& voxelMass);
};

} // namespace Core
} // namespace Phyxel
