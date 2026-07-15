#pragma once

#include <vector>
#include <functional>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "core/KinematicVoxelManager.h"   // KinematicVoxel, KinematicVoxelManager

namespace Phyxel {

namespace Physics {
    class VoxelDynamicsWorld;
    class VoxelRigidBody;
}

namespace Core {

/// One box of a compound rigid-body fragment (a greedy-merged run of voxels).
struct FragmentBox {
    glm::vec3 center;       ///< center in the fragment's local space
    glm::vec3 halfExtents;
    float     mass;         ///< aggregate mass of the voxels this box covers
};

/// Handle to a fragment that has been turned into a live falling rigid body.
struct PhysicalizedFragment {
    Physics::VoxelRigidBody* body = nullptr;   ///< the compound rigid body (owned by VoxelDynamicsWorld)
    std::string              kineticObjId;      ///< KinematicVoxelManager render id ("" if no renderer)
    glm::mat4                transform{1.0f};   ///< world transform placed at the fragment COM
    bool ok() const { return body != nullptr; }
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

    /// Turn a connected set of local-space voxels into a coherent FALLING rigid body:
    /// greedy-merge (above) -> compound VoxelRigidBody in `voxelWorld` -> optional
    /// KinematicVoxelObject in `kinematic` for rendering. The body is re-centered on
    /// its mass-weighted COM; `objectTransform` maps the voxels' local frame to world.
    ///
    ///   voxelMass          — per-voxel mass model (furniture: material.mass*0.05;
    ///                        world: material.mass*voxelVolume). See H4.
    ///   finalizeTotalMass  — optional raw->final total-mass remap applied after merge
    ///                        (furniture clamps clamp(raw*0.05, 0.5, 10); world passes
    ///                        nullptr = identity). Per-box mass is renormalized to it.
    ///
    /// Returns {body, kineticObjId, transform}; body == nullptr on failure (empty
    /// input, no world, or createBody failure). Does NOT enforce FragmentBudget — the
    /// caller decides budget/fallback before calling.
    static PhysicalizedFragment physicalize(
        Physics::VoxelDynamicsWorld* voxelWorld,
        KinematicVoxelManager* kinematic,
        const std::string& idHint,
        std::vector<KinematicVoxel> voxels,
        const glm::mat4& objectTransform,
        const glm::vec3& initialLinVel,
        const glm::vec3& initialAngVel,
        const std::function<float(const KinematicVoxel&)>& voxelMass,
        const std::function<float(float)>& finalizeTotalMass = nullptr,
        float restitution = 0.2f,
        float friction    = 0.6f,
        float linearDamp  = 0.4f,
        float angularDamp = 0.5f);

    /// F2 variant: SEPARATE collision proxy. `renderVoxels` (full fine fidelity) drive
    /// the kinematic render; `collisionVoxels` (typically one unit box per world CELL of
    /// the component's wood) drive the rigid body's boxes — so a big fell is tens of
    /// merged boxes, not thousands of per-voxel boxes (the 2005-box pine that tanked the
    /// demo). COM comes from the collision boxes; the render voxels are re-centered on
    /// the same COM so they stay aligned. Canopy cargo is render-only (no collision).
    static PhysicalizedFragment physicalize(
        Physics::VoxelDynamicsWorld* voxelWorld,
        KinematicVoxelManager* kinematic,
        const std::string& idHint,
        std::vector<KinematicVoxel> renderVoxels,
        const std::vector<KinematicVoxel>& collisionVoxels,
        const glm::mat4& objectTransform,
        const glm::vec3& initialLinVel,
        const glm::vec3& initialAngVel,
        const std::function<float(const KinematicVoxel&)>& voxelMass,
        const std::function<float(float)>& finalizeTotalMass = nullptr,
        float restitution = 0.2f,
        float friction    = 0.6f,
        float linearDamp  = 0.4f,
        float angularDamp = 0.5f);
};

} // namespace Core
} // namespace Phyxel
