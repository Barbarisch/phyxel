#pragma once

#include "core/GpuParticlePhysics.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Phyxel {
class ChunkManager;
namespace Vulkan   { class VulkanDevice; }
namespace Graphics { class RenderCoordinator; }
namespace Physics  { class PhysicsWorld; }
namespace Scene    { class AnimatedVoxelCharacter; }
namespace Core     { class KinematicVoxelManager; }

// DebrisInteractionPlan Phase 5: everything a host (the editor's Application, or a shipped game)
// does to run GPU debris, in ONE place - so the editor and shipped games run the same code path
// instead of two hand-synced copies.
//   initialize   - create the solver unless disabled; wire it to the RenderCoordinator (compute,
//                  draw, shared static occupancy) and the ChunkManager (debris light sampler). A
//                  missing solver is LOUD: one ERROR and a reason, never a silent half-state.
//   beginFrame   - the solver's per-frame CPU work (slot tracking, staging upload).
//   feed*        - the movers (Phase 3), in the order the frame produces them:
//                  characters after the entity/NPC updates, kinematic objects (doors, animated
//                  parts, held items) before the CPU physics step, CPU bodies after it.
class DebrisRuntime {
public:
    struct Config {
        // game.json "debris.enabled" (shipped games) - default ON (user decision 2026-10-07).
        // PHYXEL_DISABLE_GPU_DEBRIS=1 / --disable-gpu-debris also turn it off, for tests.
        bool enabled = true;
    };

    DebrisRuntime() = default;
    ~DebrisRuntime();
    DebrisRuntime(const DebrisRuntime&) = delete;
    DebrisRuntime& operator=(const DebrisRuntime&) = delete;

    /// Returns true when GPU debris is live. False is a supported state (disabled or no GPU
    /// support): gpu() is null, disabledReason() says why, and every break refuses its pieces
    /// (DamageSystem::refusedDebrisTotal).
    bool initialize(Vulkan::VulkanDevice* device, Graphics::RenderCoordinator* renderer,
                    ChunkManager* chunks, Physics::PhysicsWorld* physics, const Config& cfg = {});

    GpuParticlePhysics* gpu() const { return m_gpu.get(); }
    bool enabled() const { return m_gpu != nullptr; }
    const std::string& disabledReason() const { return m_disabledReason; }

    void beginFrame(float dt);

    /// Every animated character this frame (any order, duplicates and nulls allowed): oriented
    /// limb boxes with per-limb velocity, nearest `eye` first (the MAX_KINEMATIC overflow drops
    /// the farthest). Call after all characters updated this frame.
    void feedCharacters(const std::vector<Scene::AnimatedVoxelCharacter*>& characters, const glm::vec3& eye);

    /// Doors / animated template parts / held items (KinematicVoxelObject.pushesDebris): runs
    /// KinematicVoxelManager::syncCollidersToPhysics(dt) (which also registers the CPU kinematic
    /// obstacles) and feeds the same boxes, nearest `eye` first, trimmed to the slots the limbs
    /// left. Call after every owner set its transform, before the CPU physics step. Works with
    /// debris disabled too (doors still block CPU bodies).
    void feedKinematicObjects(Core::KinematicVoxelManager* kvm, float dt, const glm::vec3& eye);

    /// CPU rigid bodies (VoxelDynamicsWorld) at their post-step pose, whole bodies only, in the
    /// slots left. Call after the CPU physics step.
    void feedRigidBodies(const glm::vec3& eye);

private:
    std::unique_ptr<GpuParticlePhysics> m_gpu;
    std::string             m_disabledReason;
    Physics::PhysicsWorld*  m_physics = nullptr;
    bool m_loggedObjectSkip = false;
    bool m_loggedBodySkip   = false;
};

}  // namespace Phyxel
