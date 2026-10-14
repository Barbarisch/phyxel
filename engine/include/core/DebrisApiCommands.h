#pragma once

#include "core/CommandRegistry.h"
#include <functional>
#include <string>

namespace Phyxel {
class ChunkManager;
class DebrisRuntime;
namespace Graphics { class RenderCoordinator; }
namespace Physics  { class PhysicsWorld; }
namespace Core     { class KinematicVoxelManager; class CoherentFragmentManager; }

// DebrisInteractionPlan Phase 5b: the debris API handlers, registered from ONE place by every host
// (the editor's Application and a shipped game's GameApiService), so the packaged binary answers
// apply_damage / settle_probe / gpu_physics exactly as the editor does - no hand-synced copy.
//
// Actions: apply_damage, physics_impulse, occupancy_diff, debris_events, debris_gather (6b),
// and the debug debris set
// (spawn_gpu_particle, spawn_gpu_lattice, particle_log, gpu_physics, gpu_kinematic_box,
// settle_probe, spawn_voxel_body, clear_voxel_bodies, clear_dynamics).
struct DebrisApiContext {
    ChunkManager*                  chunks    = nullptr;
    DebrisRuntime*                 debris    = nullptr;   // null or disabled: handlers say so
    Physics::PhysicsWorld*         physics   = nullptr;
    Graphics::RenderCoordinator*   renderer  = nullptr;   // occupancy_diff reads its pool
    Core::KinematicVoxelManager*   kvm       = nullptr;   // coherent collapse (optional)
    Core::CoherentFragmentManager* fragments = nullptr;   // coherent collapse (optional)
    /// debris_gather credits gathered rubble here (material, whole units). Null: not credited.
    std::function<void(const std::string& material, int count)> addToInventory;
};

/// `context` is called per command (the host's subsystems may be created after registration).
void registerDebrisCommands(Core::CommandRegistry& reg, std::function<DebrisApiContext()> context);

}  // namespace Phyxel
