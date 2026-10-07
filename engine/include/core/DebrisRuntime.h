#pragma once

#include "core/DamageSystem.h"
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
namespace Core     { class KinematicVoxelManager; struct SpellDefinition; }

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

    /// Command-line switch shared by the editor and every game main: `--disable-gpu-debris` sets
    /// PHYXEL_DISABLE_GPU_DEBRIS=1 (launchers pass arguments, not environment). Returns true when
    /// `arg` was the switch.
    static bool handleArg(const std::string& arg);

    DebrisRuntime() = default;
    ~DebrisRuntime();
    DebrisRuntime(const DebrisRuntime&) = delete;
    DebrisRuntime& operator=(const DebrisRuntime&) = delete;

    /// Returns true when GPU debris is live. False is a supported state (disabled or no GPU
    /// support): gpu() is null, disabledReason() says why, and every break refuses its pieces
    /// (DamageSystem::refusedDebrisTotal).
    bool initialize(Vulkan::VulkanDevice* device, Graphics::RenderCoordinator* renderer,
                    ChunkManager* chunks, Physics::PhysicsWorld* physics, const Config& cfg = {});

    /// Release the solver while the Vulkan device is still alive (a shipped game calls this first
    /// in onShutdown; GameShell's member would otherwise outlive the device). Detaches it from the
    /// RenderCoordinator and ChunkManager it was wired to. Safe to call twice.
    void shutdown();

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

    /// The game-code break entry (Phase 5, user decision 2026-10-07): a voxel blast through the
    /// same DamageSystem the editor uses - breaks static voxels into GPU debris and pushes what
    /// already moves (Phase 4). With debris disabled the pieces are refused and counted.
    DamageResult applyDamage(const glm::vec3& center, float radius, float energy,
                             const glm::vec3& direction = glm::vec3(0.0f));

    /// How a spell's impact blasts the voxels it lands on (the scaffold's spell hook). Spells that
    /// deal no damage (heals, buffs) do not blast. Radius: the area spell's size (feet -> m), else
    /// 1 m for a single-target bolt. Energy: SPELL_ENERGY_PER_DAMAGE x the average base damage -
    /// a fireball (8d6, average 28) lands at ~336, the scale of the editor's test-spell blast
    /// (cast_test_spell default 350 at power 1).
    struct SpellBlast { bool blast = false; float radius = 0.0f; float energy = 0.0f; };
    static constexpr float SPELL_ENERGY_PER_DAMAGE = 12.0f;
    static constexpr float SPELL_SINGLE_TARGET_RADIUS = 1.0f;
    static SpellBlast spellBlast(const Core::SpellDefinition& spell);

private:
    std::unique_ptr<GpuParticlePhysics> m_gpu;
    std::string             m_disabledReason;
    Physics::PhysicsWorld*  m_physics = nullptr;
    Graphics::RenderCoordinator* m_renderer = nullptr;
    ChunkManager*           m_chunks = nullptr;
    bool m_loggedObjectSkip = false;
    bool m_loggedBodySkip   = false;
};

}  // namespace Phyxel
