#pragma once

#include "core/Types.h"
#include "scene/CubeLocation.h"
#include "scene/VoxelRaycaster.h"
#include "scene/VoxelForceApplicator.h"
#include "scene/VoxelManipulationSystem.h"
#include "scene/interaction/PlacementTool.h"
#include "scene/interaction/DestructionTool.h"
#include "core/AudioSystem.h"
#include <glm/glm.hpp>
#include <functional>
#include <memory>

namespace Phyxel {

// Forward declarations
class ChunkManager;
namespace Physics {
    class PhysicsWorld;
}
class MouseVelocityTracker;
namespace UI {
    class WindowManager;
}
class ForceSystem;

/**
 * VoxelInteractionSystem
 * 
 * Manages all voxel/cube interactions including hover detection, breaking, and subdivision.
 * Uses optimized O(1) raycasting for hover detection and handles both regular cubes and subcubes.
 * 
 * REFACTORING STATUS (November 2025):
 * ✓ Phase 1 Complete: Raycasting extracted to VoxelRaycaster (~290 lines)
 *   - DDA raycasting algorithm
 *   - Subcube/microcube intersection testing  
 *   - Ray-AABB intersection calculations
 *   - Screen-to-world ray conversion
 * ✓ Phase 2 Complete: Force application extracted to VoxelForceApplicator (~140 lines)
 *   - Force-based breaking with mouse velocity
 *   - Direct position-based breaking
 *   - Force propagation system integration
 *   - Dynamic physics body creation
 * ✓ Phase 3 Complete: Manipulation extracted to VoxelManipulationSystem (~480 lines)
 *   - Voxel removal (cubes, subcubes, microcubes)
 *   - Voxel subdivision (cubes → subcubes, subcubes → microcubes)
 *   - Voxel breaking with physics (static → dynamic conversion)
 *   - Material selection and physics body creation
 * 
 * Size Reduction:
 * - Original: 1,275 lines
 * - After VoxelRaycaster: 985 lines (-290 lines, -23%)
 * - After VoxelForceApplicator: ~850 lines (-425 cumulative, -33%)
 * - After VoxelManipulationSystem: ~370 lines (-905 cumulative, -71%)
 * 
 * Current responsibilities:
 * - Hover state management and visual feedback
 * - Integration with ChunkManager and PhysicsWorld
 * - Coordination of raycasting, manipulation, and force subsystems
 */
class VoxelInteractionSystem {
public:
    VoxelInteractionSystem(ChunkManager* chunkManager, 
                          Physics::PhysicsWorld* physicsWorld,
                          MouseVelocityTracker* mouseVelocityTracker,
                          UI::WindowManager* windowManager,
                          ForceSystem* forceSystem,
                          Core::AudioSystem* audioSystem = nullptr);
    ~VoxelInteractionSystem() = default;

    // Main update function - performs hover detection each frame
    void updateMouseHover(const glm::vec3& cameraPos, const glm::vec3& cameraFront, 
                         const glm::vec3& cameraUp, double mouseX, double mouseY,
                         bool isMouseCaptured);
    
    // Cube manipulation functions
    void removeHoveredCube();           // Remove the currently hovered cube
    void subdivideHoveredCube();        // Subdivide the currently hovered cube into 27 subcubes
    void subdivideHoveredSubcube();     // Subdivide the currently hovered subcube into 27 microcubes
    void breakHoveredCube(const glm::vec3& cameraPos);            // Break the currently hovered cube into a dynamic cube with physics
    void breakHoveredSubcube();         // Break the currently hovered subcube into a dynamic subcube with physics
    void breakHoveredMicrocube();       // Break the currently hovered microcube (simple removal for now)
    void breakHoveredCubeWithForce(const glm::vec3& cameraPos, double mouseX, double mouseY);   // Break cube(s) using force propagation system
    void breakCubeAtPosition(const glm::ivec3& worldPos); // Helper: Break a single cube at world position
    
    // Placement functions
    void placeVoxelAtHover();           // Place a voxel adjacent to the currently hovered face
    void placeSubcubeAtHover();         // Place a subcube adjacent to the currently hovered face
    void placeMicrocubeAtHover();       // Place a microcube adjacent to the currently hovered face
    
    // Target mode management
    void setTargetMode(TargetMode mode) { m_targetMode = mode; }
    TargetMode getTargetMode() const { return m_targetMode; }
    void cycleTargetMode();

    // Hover state accessors
    bool hasHoveredCube() const { return m_hasHoveredCube; }
    const CubeLocation& getCurrentHoveredLocation() const { return m_currentHoveredLocation; }
    
    // Performance metrics
    double getLastHoverDetectionTimeMs() const { return m_hoverDetectionTimeMs; }
    double getAvgHoverDetectionTimeMs() const { return m_avgHoverDetectionTimeMs; }
    
    // Debug flags access
    void setDebugFlags(bool hoverDetection, bool disableBreakingForces, 
                      bool showForceSystemDebug, float manualForceValue) {
        m_debugFlags.hoverDetection = hoverDetection;
        m_debugFlags.disableBreakingForces = disableBreakingForces;
        m_debugFlags.showForceSystemDebug = showForceSystemDebug;
        m_debugFlags.manualForceValue = manualForceValue;
    }

    // Raycast debug data access
    const VoxelRaycaster::RaycastDebugData& getLastRaycastDebugData() const {
        return m_raycaster.getLastRaycastDebugData();
    }
    
    void setRaycastDebugCaptureEnabled(bool enabled) {
        m_raycaster.setDebugCaptureEnabled(enabled);
    }

    /// Called after any interactive voxel add/remove with the affected world position.
    void setVoxelChangeCallback(std::function<void(const glm::ivec3&)> cb) {
        m_onVoxelChanged = std::move(cb);
    }

private:
    // Dependencies
    ChunkManager* m_chunkManager;
    Physics::PhysicsWorld* m_physicsWorld;
    MouseVelocityTracker* m_mouseVelocityTracker;
    UI::WindowManager* m_windowManager;
    ForceSystem* m_forceSystem;
    
    // Subsystems
    VoxelRaycaster m_raycaster;             // Handles all raycasting operations
    VoxelForceApplicator m_forceApplicator; // Handles force-based breaking and propagation
    VoxelManipulationSystem m_manipulator;  // Handles voxel removal, subdivision, and breaking
    
    // Interaction Tools
    std::unique_ptr<PlacementTool> m_placementTool;
    std::unique_ptr<DestructionTool> m_destructionTool;
    
    // Hover state
    bool m_hasHoveredCube;
    CubeLocation m_currentHoveredLocation;
    int m_lastHoveredCube;
    
    // Camera state (cached from updateMouseHover)
    glm::vec3 m_lastCameraPos;
    glm::vec3 m_lastCameraFront;
    glm::vec3 m_lastCameraUp;
    
    // Performance metrics
    double m_hoverDetectionTimeMs;
    double m_avgHoverDetectionTimeMs;
    int m_hoverDetectionSamples;
    
    // Debug flags (passed from Application)
    struct DebugFlags {
        bool hoverDetection = false;
        bool disableBreakingForces = false;
        bool showForceSystemDebug = false;
        float manualForceValue = 500.0f;
    } m_debugFlags;
    
    TargetMode m_targetMode = TargetMode::Cube;

    // Audio System
    Core::AudioSystem* m_audioSystem;

    // NavGrid / NPC callback — fired after any interactive voxel add/remove
    std::function<void(const glm::ivec3&)> m_onVoxelChanged;

    // Helper to create interaction context
    InteractionContext createContext() const;

    // Optimized O(1) raycasting functions (delegated to VoxelRaycaster)
    VoxelLocation pickVoxelOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    VoxelLocation resolveSubcubeInVoxel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, 
                                       const VoxelLocation& voxelHit) const;
    
    // Conversion and compatibility functions
    CubeLocation voxelLocationToCubeLocation(const VoxelLocation& voxelLoc) const;
    CubeLocation findExistingSubcubeHit(Chunk* chunk, const glm::ivec3& localPos, 
                                       const glm::ivec3& cubeWorldPos, 
                                       const glm::vec3& rayOrigin, 
                                       const glm::vec3& rayDirection) const;
    
    // Chunk-based hover management (optimized)
    void setHoveredCubeInChunksOptimized(const CubeLocation& location);
    void clearHoveredCubeInChunksOptimized();
    
    // Utility functions (delegated to VoxelRaycaster)
    bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                         const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                         float& distance) const;
    glm::vec3 screenToWorldRay(double mouseX, double mouseY, const glm::vec3& cameraPos,
                              const glm::vec3& cameraFront, const glm::vec3& cameraUp) const;
};

} // namespace Phyxel
