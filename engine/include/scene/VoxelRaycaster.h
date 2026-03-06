#pragma once

#include "core/Types.h"
#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {

// Forward declarations
class IChunkManager;
class ChunkManager;
class Chunk;
namespace UI {
    class WindowManager;
}

/**
 * VoxelRaycaster - Handles all raycasting operations for voxel selection
 * 
 * EXTRACTED FROM VOXELINTERACTIONSYSTEM.CPP (November 2025):
 * Successfully extracted ~290 lines of raycasting logic including:
 * - DDA raycasting algorithm for voxel traversal (pickVoxel)
 * - Subcube and microcube intersection testing (resolveSubcubeInVoxel)
 * - Ray-AABB intersection calculations (rayAABBIntersect)
 * - Screen-to-world ray conversion (screenToWorldRay)
 * 
 * METRICS:
 * - VoxelRaycaster.h: 118 lines
 * - VoxelRaycaster.cpp: 309 lines
 * - Total: 427 lines extracted
 * - VoxelInteractionSystem reduced: 1,275 → 985 lines (-23%)
 * 
 * DESIGN PATTERN:
 * - Uses callback functions to access ChunkManager without circular dependencies
 * - Pure raycasting logic with no state management
 * - Optimized O(1) voxel lookups via DDA algorithm
 * - Handles full voxel hierarchy (cubes → subcubes → microcubes)
 * 
 * PERFORMANCE:
 * - DDA algorithm: O(distance) instead of O(n) brute force
 * - Early exit on first voxel hit
 * - Efficient AABB intersection tests
 * - Max ray distance: 200 units
 */
class VoxelRaycaster {
public:
    // Callback function types
    using ChunkManagerAccessFunc = std::function<IChunkManager*()>;
    using WindowManagerAccessFunc = std::function<UI::WindowManager*()>;

    VoxelRaycaster() = default;
    ~VoxelRaycaster() = default;

    /**
     * Perform optimized raycasting to find voxel intersection
     * Uses DDA algorithm for efficient voxel traversal
     * 
     * @param rayOrigin Starting point of the ray (camera position)
     * @param rayDirection Normalized direction vector
     * @param getChunkManager Callback to access ChunkManager
     * @return VoxelLocation of first intersected voxel, or invalid if no hit
     */
    VoxelLocation pickVoxel(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        ChunkManagerAccessFunc getChunkManager
    ) const;

    /**
     * Convert screen coordinates to world-space ray direction
     * 
     * @param mouseX Screen X coordinate
     * @param mouseY Screen Y coordinate
     * @param cameraPos Camera position in world space
     * @param cameraFront Camera forward vector
     * @param cameraUp Camera up vector
     * @param getWindowManager Callback to access WindowManager for screen dimensions
     * @return Normalized ray direction in world space
     */
    glm::vec3 screenToWorldRay(
        double mouseX,
        double mouseY,
        const glm::vec3& cameraPos,
        const glm::vec3& cameraFront,
        const glm::vec3& cameraUp,
        WindowManagerAccessFunc getWindowManager
    ) const;

    /**
     * Test ray-AABB intersection
     * 
     * @param rayOrigin Ray starting point
     * @param rayDir Ray direction (should be normalized)
     * @param aabbMin AABB minimum corner
     * @param aabbMax AABB maximum corner
     * @param distance Output: distance to intersection point
     * @return true if intersection exists, false otherwise
     */
    bool rayAABBIntersect(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        const glm::vec3& aabbMin,
        const glm::vec3& aabbMax,
        float& distance
    ) const;

    /**
     * Get the last raycast debug data for visualization
     * Contains traversal path, hit info, etc.
     */
    struct RaycastDebugData {
        glm::vec3 rayOrigin;
        glm::vec3 rayDirection;
        std::vector<glm::ivec3> traversedVoxels;
        glm::vec3 hitPoint;
        glm::vec3 hitNormal;
        bool hasHit;
        int hitFace;
        VoxelLocation hitLocation;
        
        // Target placement visualization
        glm::vec3 targetSubcubeCenter;
        bool hasTarget = false;
    };

    const RaycastDebugData& getLastRaycastDebugData() const { return m_lastDebugData; }
    void setDebugCaptureEnabled(bool enabled) { m_debugCaptureEnabled = enabled; }
    
    // Allow external systems (like VoxelInteractionSystem) to augment debug data
    void setDebugTarget(const glm::vec3& target) const {
        if (m_debugCaptureEnabled) {
            m_lastDebugData.targetSubcubeCenter = target;
            m_lastDebugData.hasTarget = true;
        }
    }
    
    void clearDebugTarget() const {
        if (m_debugCaptureEnabled) {
            m_lastDebugData.hasTarget = false;
        }
    }

private:
    /**
     * Resolve subcube or microcube intersection within a voxel
     * Called after pickVoxel finds a cube, to determine if a subcube/microcube was hit
     * 
     * @param rayOrigin Ray starting point
     * @param rayDirection Ray direction
     * @param voxelHit The voxel that was hit by pickVoxel
     * @return Updated VoxelLocation with subcube/microcube information
     */
    VoxelLocation resolveSubcubeInVoxel(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        const VoxelLocation& voxelHit
    ) const;

    /**
     * Find which existing subcube (if any) was hit by the ray
     * Helper method for resolveSubcubeInVoxel
     */
    VoxelLocation findExistingSubcubeHit(
        Chunk* chunk,
        const glm::ivec3& localPos,
        const glm::ivec3& cubeWorldPos,
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection
    ) const;

    // Debug data capture
    mutable RaycastDebugData m_lastDebugData;
    mutable bool m_debugCaptureEnabled = false;
};

} // namespace Phyxel
