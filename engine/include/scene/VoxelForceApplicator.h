#pragma once

#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {

// Forward declarations
class ChunkManager;
class ForceSystem;
class MouseVelocityTracker;
namespace Physics {
    class PhysicsWorld;
}
struct CubeLocation;

/**
 * VoxelForceApplicator - Handles force-based voxel breaking and propagation
 * 
 * EXTRACTED FROM VOXELINTERACTIONSYSTEM.CPP (November 2025):
 * Successfully extracted ~94 lines of force application logic including:
 * - Force-based cube breaking with mouse velocity (breakHoveredCubeWithForce)
 * - Direct position-based breaking (breakCubeAtPosition)
 * - Force propagation system integration
 * - Dynamic physics body creation and impulse application
 * 
 * METRICS:
 * - VoxelForceApplicator.h: 100 lines
 * - VoxelForceApplicator.cpp: 165 lines
 * - Total: 265 lines extracted
 * - VoxelInteractionSystem reduced: 985 → 891 lines (-94 lines, cumulative -384 from original)
 * 
 * DESIGN PATTERN:
 * - Uses callback functions to access dependencies without circular dependencies
 * - Handles physics body creation and force application
 * - Integrates with ForceSystem for propagation
 * - Material selection based on position
 * 
 * RESPONSIBILITIES:
 * - Force-based cube breaking with mouse velocity tracking (>500 threshold)
 * - Creating dynamic physics cubes with impulse forces
 * - Random force application for natural breaking effects
 * - Integration with force propagation system
 * - Material-based physics properties (stone, wood, metal, ice)
 */
class VoxelForceApplicator {
public:
    // Callback function types for accessing dependencies
    using ChunkManagerAccessFunc = std::function<ChunkManager*()>;
    using PhysicsWorldAccessFunc = std::function<Physics::PhysicsWorld*()>;
    using ForceSystemAccessFunc = std::function<ForceSystem*()>;
    using MouseVelocityAccessFunc = std::function<MouseVelocityTracker*()>;
    
    VoxelForceApplicator() = default;
    ~VoxelForceApplicator() = default;
    
    /**
     * Break the hovered cube using force propagation system
     * 
     * If mouse velocity is high (>500 units/sec), uses force propagation
     * to break multiple cubes. Otherwise, falls back to normal breaking.
     * 
     * @param cameraPos Current camera position
     * @param mouseX Mouse X coordinate
     * @param mouseY Mouse Y coordinate
     * @param currentHoveredLocation Current hovered cube location
     * @param hasHoveredCube Whether a cube is currently hovered
     * @param breakHoveredCubeCallback Callback to break single cube normally
     * @param breakHoveredSubcubeCallback Callback to break single subcube normally
     * @param getChunkManager Callback to access ChunkManager
     * @param getForceSystem Callback to access ForceSystem
     * @param getMouseVelocity Callback to access MouseVelocityTracker
     */
    void breakHoveredCubeWithForce(
        const glm::vec3& cameraPos,
        double mouseX, double mouseY,
        const CubeLocation& currentHoveredLocation,
        bool hasHoveredCube,
        std::function<void(const glm::vec3&)> breakHoveredCubeCallback,
        std::function<void()> breakHoveredSubcubeCallback,
        ChunkManagerAccessFunc getChunkManager,
        ForceSystemAccessFunc getForceSystem,
        MouseVelocityAccessFunc getMouseVelocity
    );
    
    /**
     * Break a cube at a specific world position
     * 
     * Used by force propagation system to break cubes at calculated positions.
     * Creates a dynamic physics cube with random impulse forces.
     * 
     * @param worldPos World position of cube to break
     * @param getChunkManager Callback to access ChunkManager
     * @param getPhysicsWorld Callback to access PhysicsWorld
     * @param disableBreakingForces Whether to disable force application
     */
    void breakCubeAtPosition(
        const glm::ivec3& worldPos,
        ChunkManagerAccessFunc getChunkManager,
        PhysicsWorldAccessFunc getPhysicsWorld,
        bool disableBreakingForces
    );
};

} // namespace Phyxel
