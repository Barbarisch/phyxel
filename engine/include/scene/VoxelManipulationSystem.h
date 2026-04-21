#pragma once

#include "core/Types.h"
#include "scene/CubeLocation.h"
#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {

// Forward declarations
class ChunkManager;
class Chunk;
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
 * - Removing voxels (cubes, subcubes, microcubes)
 * - Subdividing voxels (cubes into subcubes, subcubes into microcubes)
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
    
    // =========================================================================
    // VOXEL REMOVAL OPERATIONS
    // =========================================================================
    
    /**
     * Remove a cube or subcube (does not create physics objects)
     * @param location CubeLocation identifying the voxel to remove
     * @return true if voxel was successfully removed
     */
    bool removeVoxel(const CubeLocation& location);
    
    // =========================================================================
    // VOXEL SUBDIVISION OPERATIONS
    // =========================================================================
    
    /**
     * Subdivide a cube into 27 static subcubes
     * @param location CubeLocation identifying the cube
     * @return true if subdivision was successful
     */
    bool subdivideCube(const CubeLocation& location);
    
    /**
     * Subdivide a subcube into 27 static microcubes
     * @param location CubeLocation identifying the subcube
     * @return true if subdivision was successful
     */
    bool subdivideSubcube(const CubeLocation& location);
    
    // =========================================================================
    // VOXEL BREAKING OPERATIONS (with physics)
    // =========================================================================
    
    /**
     * Break a cube and convert it to a dynamic physics object
     * @param location CubeLocation identifying the cube
     * @param cameraPos Camera position for calculating impulse direction
     * @param applyForce Whether to apply impulse forces (true for mouse breaking, false for gentle removal)
     * @return true if cube was successfully broken
     */
    bool breakCube(const CubeLocation& location, const glm::vec3& cameraPos, bool applyForce = true);
    
    /**
     * Break a subcube and convert it to a dynamic physics object
     * @param location CubeLocation identifying the subcube
     * @param applyForce Whether to apply impulse forces (usually false for gentle removal)
     * @return true if subcube was successfully broken
     */
    bool breakSubcube(const CubeLocation& location, bool applyForce = false);
    
    /**
     * Break a microcube and convert it to a dynamic physics object
     * @param location CubeLocation identifying the microcube
     * @param applyForce Whether to apply impulse forces (usually false for gentle removal)
     * @return true if microcube was successfully broken
     */
    bool breakMicrocube(const CubeLocation& location, bool applyForce = false);
    
    /**
     * Break a cube at a specific world position (convenience method)
     * @param worldPos World position of the cube
     * @param disableForces If true, don't apply breaking forces
     * @return true if cube was successfully broken
     */
    bool breakCubeAtPosition(const glm::ivec3& worldPos, bool disableForces = false);
    
    // =========================================================================
    // VOXEL PLACEMENT OPERATIONS
    // =========================================================================
    
    /**
     * Place a cube at the specified world position
     * Will auto-create chunk if it doesn't exist
     * Fails if position is already occupied
     * @param worldPos World position where cube should be placed
     * @param color Color/material of the cube
     * @return true if cube was successfully placed
     */
    bool placeCube(const glm::ivec3& worldPos, const glm::vec3& color);
    
    /**
     * Place a subcube at the specified position
     * Does NOT require parent cube (standalone placement)
     * Will auto-create chunk if it doesn't exist
     * @param worldPos World position of the parent cube grid
     * @param subcubePos Local position within 3x3x3 subcube grid (0-2 for each axis)
     * @param color Color/material of the subcube
     * @return true if subcube was successfully placed
     */
    bool placeSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos, const glm::vec3& color);
    
    /**
     * Place a microcube at the specified position
     * Does NOT require parent subcube (standalone placement)
     * Will auto-create chunk if it doesn't exist
     * @param parentCubePos World position of the parent cube grid
     * @param subcubePos Local position within 3x3x3 subcube grid (0-2 for each axis)
     * @param microcubePos Local position within 3x3x3 microcube grid (0-2 for each axis)
     * @param color Color/material of the microcube
     * @return true if microcube was successfully placed
     */
    bool placeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos,
                       const glm::ivec3& microcubePos, const glm::vec3& color);

    /// Set a callback that returns the material name to use when placing voxels.
    /// Called at placement time, so the provider always returns the current selection.
    /// An empty string or unset provider means no material (uses addCube default).
    void setMaterialProvider(std::function<std::string()> provider) {
        m_materialProvider = std::move(provider);
    }

private:
    // Callback functions
    GetChunkManagerFunc getChunkManager;
    GetPhysicsWorldFunc getPhysicsWorld;

    // Optional material provider — called at placement time to get the active material
    std::function<std::string()> m_materialProvider;
    
    // Helper methods for material selection
    std::string selectMaterialForCube(const glm::vec3& cubeWorldPos) const;
    
    // Helper methods for placement
    glm::vec3 getCurrentPlacementColor() const;
    bool ensureChunkExists(const glm::ivec3& worldPos);
};

} // namespace Phyxel
