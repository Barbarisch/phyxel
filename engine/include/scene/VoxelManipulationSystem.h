#pragma once

#include "core/Types.h"
#include "scene/CubeLocation.h"
#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {

// Forward declarations
class ChunkManager;
class GpuParticlePhysics;
namespace Physics {
    class PhysicsWorld;
}

/**
 * VoxelManipulationSystem - Handles voxel modification operations
 * 
 * Extracted from VoxelInteractionSystem to separate manipulation logic
 * from hover detection and rendering.
 * 
 * Responsibilities:
 * - Breaking voxels with physics (converting static → dynamic)
 * 
 * Uses callback pattern for accessing external systems (ChunkManager, PhysicsWorld)
 */
class VoxelManipulationSystem {
public:
    VoxelManipulationSystem() = default;
    ~VoxelManipulationSystem() = default;

    // Callback types for accessing external systems
    using GetChunkManagerFunc = std::function<ChunkManager*()>;
    using GetPhysicsWorldFunc = std::function<Physics::PhysicsWorld*()>;
    
    /**
     * Configure callbacks for system access
     */
    void setCallbacks(
        GetChunkManagerFunc chunkManagerFunc,
        GetPhysicsWorldFunc physicsWorldFunc
    );
    
    /// Set a callback that returns the material name to use when placing voxels.
    /// Called at placement time, so the provider always returns the current selection.
    /// An empty string or unset provider means no material (uses addCube default).
    void setMaterialProvider(std::function<std::string()> provider) {
        m_materialProvider = std::move(provider);
    }

    /// Where broken pieces go (DebrisInteractionPlan 1d): every break spawns GPU debris through
    /// DamageSystem::spawnBreakDebris. A provider, not a pointer, because the GPU solver is created
    /// after this system (and can be torn down). Unset/null = pieces are refused and counted.
    void setGpuDebrisProvider(std::function<GpuParticlePhysics*()> provider) {
        m_gpuDebris = std::move(provider);
    }

private:
    // Callback functions
    GetChunkManagerFunc getChunkManager;
    GetPhysicsWorldFunc getPhysicsWorld;

    // Optional material provider — called at placement time to get the active material
    std::function<std::string()> m_materialProvider;
public:
    /// F7.4: the material placing uses now ("Stone" when no provider is set or it returns "").
    std::string currentMaterial() const {
        const std::string m = m_materialProvider ? m_materialProvider() : std::string();
        return m.empty() ? std::string("Stone") : m;
    }
private:
    std::function<GpuParticlePhysics*()> m_gpuDebris;
public:
    /// The GPU debris solver broken pieces go to (null = none wired).
    GpuParticlePhysics* gpuDebris() const { return m_gpuDebris ? m_gpuDebris() : nullptr; }
};

} // namespace Phyxel
