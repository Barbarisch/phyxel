#pragma once

#include "core/Types.h"
#include <glm/glm.hpp>
#include <memory>

namespace VulkanCube {

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
class Chunk;

// Structure for backward compatibility with existing chunk-based system
struct CubeLocation {
    Chunk* chunk;
    glm::ivec3 localPos;        // Local position within chunk
    glm::ivec3 worldPos;        // World position
    bool isSubcube;             // True if this location refers to a subcube
    glm::ivec3 subcubePos;      // Local position within parent cube (0-2 for each axis)
    
    // Face information for cube placement
    int hitFace;                // Which face was hit: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    glm::vec3 hitNormal;        // Surface normal of hit face
    glm::vec3 hitPoint;         // Exact hit point on the cube surface
    
    CubeLocation() : chunk(nullptr), localPos(-1), worldPos(-1), isSubcube(false), 
                     subcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
    CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world) 
        : chunk(c), localPos(local), worldPos(world), isSubcube(false), 
          subcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
    CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world, const glm::ivec3& sub) 
        : chunk(c), localPos(local), worldPos(world), isSubcube(true), 
          subcubePos(sub), hitFace(-1), hitNormal(0), hitPoint(0) {}
    
    bool isValid() const { return chunk != nullptr; }
    
    // Get the world position where a new cube should be placed adjacent to this face
    glm::ivec3 getAdjacentPlacementPosition() const;
};

/**
 * VoxelInteractionSystem
 * 
 * Manages all voxel/cube interactions including hover detection, breaking, and subdivision.
 * Uses optimized O(1) raycasting for hover detection and handles both regular cubes and subcubes.
 */
class VoxelInteractionSystem {
public:
    VoxelInteractionSystem(ChunkManager* chunkManager, 
                          Physics::PhysicsWorld* physicsWorld,
                          MouseVelocityTracker* mouseVelocityTracker,
                          UI::WindowManager* windowManager,
                          ForceSystem* forceSystem);
    ~VoxelInteractionSystem() = default;

    // Main update function - performs hover detection each frame
    void updateMouseHover(const glm::vec3& cameraPos, const glm::vec3& cameraFront, 
                         const glm::vec3& cameraUp, double mouseX, double mouseY,
                         bool isMouseCaptured);
    
    // Cube manipulation functions
    void removeHoveredCube();           // Remove the currently hovered cube
    void subdivideHoveredCube();        // Subdivide the currently hovered cube into 27 subcubes
    void breakHoveredCube(const glm::vec3& cameraPos);            // Break the currently hovered cube into a dynamic cube with physics
    void breakHoveredSubcube();         // Break the currently hovered subcube into a dynamic subcube with physics
    void breakHoveredCubeWithForce(const glm::vec3& cameraPos, double mouseX, double mouseY);   // Break cube(s) using force propagation system
    void breakCubeAtPosition(const glm::ivec3& worldPos); // Helper: Break a single cube at world position
    
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

private:
    // Dependencies
    ChunkManager* m_chunkManager;
    Physics::PhysicsWorld* m_physicsWorld;
    MouseVelocityTracker* m_mouseVelocityTracker;
    UI::WindowManager* m_windowManager;
    ForceSystem* m_forceSystem;
    
    // Hover state
    bool m_hasHoveredCube;
    CubeLocation m_currentHoveredLocation;
    int m_lastHoveredCube;
    glm::vec3 m_originalHoveredColor;
    
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
    
    // Optimized O(1) raycasting functions
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
    
    // Utility functions
    bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                         const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                         float& distance) const;
    glm::vec3 screenToWorldRay(double mouseX, double mouseY, const glm::vec3& cameraPos,
                              const glm::vec3& cameraFront, const glm::vec3& cameraUp) const;
    glm::vec3 calculateLighterColor(const glm::vec3& originalColor) const;
};

} // namespace VulkanCube
