#pragma once

#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>
#include "core/KinematicVoxelManager.h"   // KinematicVoxel, KinematicVoxelManager

namespace Phyxel {

namespace Physics { class VoxelDynamicsWorld; }

namespace Core {

/// Persistent owner + per-frame driver for coherent world fragments
/// (docs/DestructionSystemV2.md §5.G, H12). physicalize() (in CoherentFragmentService)
/// makes the falling compound rigid body + render object; THIS ticks the body's
/// transform into the renderer every frame, and reaps fragments whose body the physics
/// world has removed. Unlike DynamicFurnitureManager it NEVER re-staticizes on settle —
/// the 2026-07-14 decision keeps settled fells in dynamic space (no grid re-bake).
///
/// Minimal Phase-1b slice: own + tick + persist. The freeze / lazy-reactivate
/// retirement tier (§5.G) grows on top of this later.
class CoherentFragmentManager {
public:
    void setDeps(Physics::VoxelDynamicsWorld* world, KinematicVoxelManager* kinematic) {
        m_world = world; m_kinematic = kinematic;
    }
    bool ready() const { return m_world != nullptr && m_kinematic != nullptr; }

    /// Spawn a persistent coherent fragment from local-space voxels. Uses the WORLD
    /// mass model (caller-supplied voxelMass, no clamp) and pins the body to never
    /// time out. Returns the rigid-body id, or 0 on failure.
    uint32_t spawn(const std::string& idHint,
                   std::vector<KinematicVoxel> voxels,
                   const glm::mat4& objectTransform,
                   const glm::vec3& initialLinVel,
                   const glm::vec3& initialAngVel,
                   const std::function<float(const KinematicVoxel&)>& voxelMass);

    /// F2 variant: separate collision proxy (see CoherentFragmentService::physicalize) —
    /// render keeps full fine fidelity while the rigid body's boxes come from the coarse
    /// collision list (bounded box count for big fells).
    uint32_t spawn(const std::string& idHint,
                   std::vector<KinematicVoxel> renderVoxels,
                   const std::vector<KinematicVoxel>& collisionVoxels,
                   const glm::mat4& objectTransform,
                   const glm::vec3& initialLinVel,
                   const glm::vec3& initialAngVel,
                   const std::function<float(const KinematicVoxel&)>& voxelMass);

    /// Sync each fragment's rigid-body transform into the renderer; drop fragments
    /// whose body has been removed by the physics world.
    void update(float dt);

    size_t count() const { return m_frags.size(); }
    void clear();

private:
    struct Frag {
        uint32_t    bodyId;   ///< VoxelRigidBody id (looked up each tick; robust to removal)
        std::string kinId;    ///< KinematicVoxelManager render id
        glm::vec3   prevVel{0.0f};   ///< last tick's linear velocity (U6 impact detection)
        bool        primed = false;  ///< prevVel valid yet? (skip fracture on the spawn frame)
        int         gen    = 0;      ///< fracture generation (bounds re-fracture recursion)
    };
    std::vector<Frag> m_frags;

    // U6 impact fracture: on a hard landing, split a fragment at overloaded cross-sections.
    void tryImpactFracture(size_t index, Physics::VoxelRigidBody* b, float impulse);
    // Tuning (needs live calibration — see docs/DestructionSystemV2.md §15.3 U6).
    static constexpr float kImpactImpulse   = 150.0f;  // min impulse (Δv × mass) to consider fracture
    static constexpr float kFractureBreakK  = 1.6f;    // strength multiplier (higher = harder to break)
    static constexpr int   kMaxFractureGen  = 1;       // a chunk may re-fracture at most this deep
    static constexpr int   kMaxCutsPerHit   = 2;       // ≤ this many cuts per impact (→ ≤ 3 chunks)

    Physics::VoxelDynamicsWorld* m_world     = nullptr;
    KinematicVoxelManager*       m_kinematic = nullptr;
    float                        m_logTimer  = 0.0f;   ///< throttles the diagnostic state log
};

} // namespace Core
} // namespace Phyxel
