#include "scene/VoxelRaycaster.h"
#include "core/IChunkManager.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "ui/WindowManager.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <iostream>

namespace Phyxel {

/**
 * Pick a voxel using optimized DDA (Digital Differential Analyzer) raycasting
 * 
 * ALGORITHM: DDA Voxel Traversal
 * Instead of checking every voxel in the world (O(n) brute force), DDA efficiently
 * steps through only the voxels that the ray passes through (O(distance)).
 * 
 * HOW IT WORKS:
 * 1. Start at rayOrigin, discretize to integer voxel coordinates
 * 2. Calculate step direction (+1 or -1) for each axis based on ray direction
 * 3. Calculate deltaDist: distance the ray travels to cross one voxel on each axis
 * 4. Calculate sideDist: distance to next voxel boundary on each axis
 * 5. Loop: Always step along the axis with smallest sideDist
 * 6. Check voxel at each step for solid geometry
 * 7. Early exit on first hit (nearest voxel)
 * 
 * PERFORMANCE:
 * - O(distance) instead of O(total_voxels)
 * - Typical: 10-50 steps for max distance of 200 units
 * - Worst case: 500 steps (hard limit to prevent infinite loops)
 * 
 * HIERARCHICAL RESOLUTION:
 * - If voxel is SUBDIVIDED (contains subcubes/microcubes), call resolveSubcubeInVoxel
 * - If ray misses all subcubes, continue DDA to next voxel
 * - This handles hollow subdivided voxels (some subcubes removed)
 * 
 * @param rayOrigin Starting point of ray (camera position)
 * @param rayDirection Normalized direction vector
 * @param getChunkManager Callback to access ChunkManager for voxel queries
 * @return VoxelLocation with hit information, or invalid if no hit
 */
VoxelLocation VoxelRaycaster::pickVoxel(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    ChunkManagerAccessFunc getChunkManager
) const {
    // Initialize debug data capture
    if (m_debugCaptureEnabled) {
        m_lastDebugData.rayOrigin = rayOrigin;
        m_lastDebugData.rayDirection = rayDirection;
        m_lastDebugData.traversedVoxels.clear();
        m_lastDebugData.hasHit = false;
        m_lastDebugData.hitFace = -1;
    }

    IChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        return VoxelLocation(); // Invalid location
    }
    
    // DDA INITIALIZATION:
    // Set maximum ray distance (prevents infinite loops and improves performance).
    // Long enough to hover voxels across larger / streamed worlds (was 200, too short).
    float maxDistance = 400.0f;
    
    // Discretize ray origin to integer voxel coordinates (floor to get containing voxel)
    glm::ivec3 voxel = glm::ivec3(glm::floor(rayOrigin));
    
    // FIX: Handle boundary cases where ray starts exactly on a voxel face
    // If moving in POSITIVE direction and on boundary, we should start in the previous voxel
    // to ensure we "enter" the current voxel and register a face hit.
    // If moving in NEGATIVE direction, floor() already puts us in the current voxel,
    // and the first step will take us to the next voxel (negative), registering a face hit.
    const float EPSILON = 1e-5f;
    glm::vec3 fractPos = rayOrigin - glm::floor(rayOrigin);
    
    if (rayDirection.x > 0 && fractPos.x < EPSILON) voxel.x--;
    if (rayDirection.y > 0 && fractPos.y < EPSILON) voxel.y--;
    if (rayDirection.z > 0 && fractPos.z < EPSILON) voxel.z--;

    if (rayDirection.x < 0 && fractPos.x > 1.0f - EPSILON) voxel.x++;
    if (rayDirection.y < 0 && fractPos.y > 1.0f - EPSILON) voxel.y++;
    if (rayDirection.z < 0 && fractPos.z > 1.0f - EPSILON) voxel.z++;
    
    // STEP DIRECTION:
    // Determine which direction to step on each axis (+1, -1, or 0)
    // Example: If ray goes right (+X), step.x = 1; if left (-X), step.x = -1
    glm::ivec3 step = glm::ivec3(
        rayDirection.x > 0 ? 1 : (rayDirection.x < 0 ? -1 : 0),
        rayDirection.y > 0 ? 1 : (rayDirection.y < 0 ? -1 : 0),
        rayDirection.z > 0 ? 1 : (rayDirection.z < 0 ? -1 : 0)
    );
    
    // DELTA DISTANCE:
    // How far the ray must travel to cross one voxel on each axis
    // deltaDist = 1 / |rayDirection| for each component
    // If ray is parallel to axis (direction = 0), set to infinity (never crosses)
    glm::vec3 deltaDist = glm::vec3(
        rayDirection.x != 0 ? glm::abs(1.0f / rayDirection.x) : std::numeric_limits<float>::max(),
        rayDirection.y != 0 ? glm::abs(1.0f / rayDirection.y) : std::numeric_limits<float>::max(),
        rayDirection.z != 0 ? glm::abs(1.0f / rayDirection.z) : std::numeric_limits<float>::max()
    );
    
    // SIDE DISTANCE:
    // Distance from ray origin to the NEXT voxel boundary on each axis
    // Depends on which direction we're stepping and where we are in the current voxel
    // 
    // If stepping negative (rayDirection < 0):
    //   sideDist = (rayOrigin - voxel) * deltaDist  (distance to left/bottom/back boundary)
    // If stepping positive (rayDirection > 0):
    //   sideDist = (voxel + 1 - rayOrigin) * deltaDist  (distance to right/top/front boundary)
    glm::vec3 sideDist;
    if (rayDirection.x < 0) {
        sideDist.x = (rayOrigin.x - voxel.x) * deltaDist.x;
    } else {
        sideDist.x = (voxel.x + 1.0f - rayOrigin.x) * deltaDist.x;
    }
    if (rayDirection.y < 0) {
        sideDist.y = (rayOrigin.y - voxel.y) * deltaDist.y;
    } else {
        sideDist.y = (voxel.y + 1.0f - rayOrigin.y) * deltaDist.y;
    }
    if (rayDirection.z < 0) {
        sideDist.z = (rayOrigin.z - voxel.z) * deltaDist.z;
    } else {
        sideDist.z = (voxel.z + 1.0f - rayOrigin.z) * deltaDist.z;
    }
    
    // DDA LOOP PARAMETERS:
    int maxSteps = 500;        // Hard limit to prevent infinite loops (200 units / 1 voxel = ~200 steps typical)
    int lastStepAxis = -1;     // Track which axis we stepped on (for face normal calculation)
    
    // DDA MAIN LOOP:
    // Step through voxels along the ray until we hit something or exceed max distance
    for (int step_count = 0; step_count < maxSteps; ++step_count) {
        // Capture traversed voxels for debug visualization
        if (m_debugCaptureEnabled) {
            m_lastDebugData.traversedVoxels.push_back(voxel);
        }

        // O(1) VOXEL QUERY:
        // ChunkManager.resolveGlobalPosition uses hash maps for instant lookup
        // Returns VoxelLocation with type (CUBE, SUBDIVIDED, EMPTY) and chunk/position info
        VoxelLocation location = chunkManager->resolveGlobalPosition(voxel);
        
        if (location.isValid()) {
            LOG_TRACE_FMT("VoxelRaycaster", "[PICK] Found valid voxel at " << voxel.x << "," << voxel.y << "," << voxel.z 
                      << " type=" << (int)location.type);
            
            // FACE NORMAL CALCULATION:
            // Determine which face we hit based on which axis we last stepped on
            // Face numbering: 0=left(-X), 1=right(+X), 2=bottom(-Y), 3=top(+Y), 4=back(-Z), 5=front(+Z)
            // Normal points OUTWARD from the face (opposite to ray direction on that axis)
            int hitFace = -1;
            glm::vec3 hitNormal(0);
            
            if (lastStepAxis >= 0) {
                if (lastStepAxis == 0) { // X-axis step
                    hitFace = (step.x > 0) ? 0 : 1;
                    hitNormal = (step.x > 0) ? glm::vec3(-1,0,0) : glm::vec3(1,0,0);
                } else if (lastStepAxis == 1) { // Y-axis step
                    hitFace = (step.y > 0) ? 2 : 3;
                    hitNormal = (step.y > 0) ? glm::vec3(0,-1,0) : glm::vec3(0,1,0);
                } else if (lastStepAxis == 2) { // Z-axis step
                    hitFace = (step.z > 0) ? 4 : 5;
                    hitNormal = (step.z > 0) ? glm::vec3(0,0,-1) : glm::vec3(0,0,1);
                }
            }
            
            // Calculate precise hit point based on which face was hit
            glm::vec3 hitPoint;
            if (lastStepAxis == 0) {
                // Hit on X face - use sideDist.x to go back to the hit point
                float hitDistance = sideDist.x - deltaDist.x;
                hitPoint = rayOrigin + rayDirection * hitDistance;
            } else if (lastStepAxis == 1) {
                // Hit on Y face
                float hitDistance = sideDist.y - deltaDist.y;
                hitPoint = rayOrigin + rayDirection * hitDistance;
            } else if (lastStepAxis == 2) {
                // Hit on Z face
                float hitDistance = sideDist.z - deltaDist.z;
                hitPoint = rayOrigin + rayDirection * hitDistance;
            } else {
                // Fallback to ray origin if we started inside the voxel
                hitPoint = rayOrigin;
            }
            
            location.hitFace = hitFace;
            location.hitNormal = hitNormal;
            location.hitPoint = hitPoint;
            
            // Capture hit data for debug visualization
            if (m_debugCaptureEnabled) {
                m_lastDebugData.hasHit = true;
                m_lastDebugData.hitFace = hitFace;
                m_lastDebugData.hitNormal = hitNormal;
                m_lastDebugData.hitPoint = hitPoint;  // Use precise hit point, not voxel center
                m_lastDebugData.hitLocation = location;
            }
            
            return location;   // a cube (a Chunk holds no subdivided cells since D3c-5)
        }
        
        // DDA STEP:
        // Always step along the axis with the smallest sideDist (closest boundary)
        // This ensures we check every voxel the ray passes through in order
        // After stepping, update sideDist for that axis by adding deltaDist
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            lastStepAxis = 0;           // Stepped on X axis
            sideDist.x += deltaDist.x;  // Move to next X boundary
            voxel.x += step.x;          // Increment voxel coordinate
        } else if (sideDist.y < sideDist.z) {
            lastStepAxis = 1;           // Stepped on Y axis
            sideDist.y += deltaDist.y;  // Move to next Y boundary
            voxel.y += step.y;          // Increment voxel coordinate
        } else {
            lastStepAxis = 2;           // Stepped on Z axis
            sideDist.z += deltaDist.z;  // Move to next Z boundary
            voxel.z += step.z;          // Increment voxel coordinate
        }
        
        // DISTANCE CHECK:
        // Early exit if we've traveled beyond max ray distance
        // Prevents wasting time checking distant voxels
        glm::vec3 currentPos = glm::vec3(voxel);
        if (glm::length(currentPos - rayOrigin) > maxDistance) {
            break;
        }
    }
    
    return VoxelLocation(); // No voxel found
}

bool VoxelRaycaster::rayAABBIntersect(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax,
    float& distance
) const {
    // Standard ray-AABB intersection test
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t1 = (aabbMin - rayOrigin) * invDir;
    glm::vec3 t2 = (aabbMax - rayOrigin) * invDir;
    
    glm::vec3 tMin = glm::min(t1, t2);
    glm::vec3 tMax = glm::max(t1, t2);
    
    float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
    float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
    
    if (tNear > tFar || tFar < 0.0f) {
        return false; // No intersection
    }
    
    distance = tNear > 0.0f ? tNear : tFar;
    return true;
}

glm::vec3 VoxelRaycaster::screenToWorldRay(
    double mouseX,
    double mouseY,
    const glm::vec3& cameraPos,
    const glm::vec3& cameraFront,
    const glm::vec3& cameraUp,
    WindowManagerAccessFunc getWindowManager
) const {
    UI::WindowManager* windowManager = getWindowManager();
    if (!windowManager) {
        return glm::vec3(0); // Invalid ray
    }
    
    // Convert mouse coordinates to normalized device coordinates
    float x = (2.0f * mouseX) / windowManager->getWidth() - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / windowManager->getHeight(); // Flip Y coordinate
    
    // Create clip space coordinates - flip Y again for Vulkan coordinate system
    glm::vec4 rayClip = glm::vec4(x, -y, -1.0f, 1.0f);
    
    // Convert to eye space.
    //
    // DELIBERATELY NOT the reverse-Z projection (graphics/DepthConvention.h), and this is the one
    // place in the engine where that is fine: the result is used only for a ray DIRECTION — the
    // next line overwrites z and w outright — so nothing here depends on the depth mapping. The
    // only terms that survive are m[0][0] and m[1][1], which are identical in both conventions.
    // The near/far values below are therefore inert.
    // If this is ever changed to reconstruct a DEPTH or a world-space POSITION, it must switch to
    // Camera::getProjectionMatrix or it will be silently wrong.
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f),
        (float)windowManager->getWidth() / (float)windowManager->getHeight(),
        0.1f,
        200.0f
    );
    proj[1][1] *= -1; // Flip Y for Vulkan
    
    glm::vec4 rayEye = glm::inverse(proj) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    
    // Convert to world space
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    
    return glm::normalize(rayWorld);
}

} // namespace Phyxel
