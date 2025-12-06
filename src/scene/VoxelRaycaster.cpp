#include "scene/VoxelRaycaster.h"
#include "core/IChunkManager.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "ui/WindowManager.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <iostream>

namespace VulkanCube {

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
    // Set maximum ray distance (prevents infinite loops and improves performance)
    float maxDistance = 200.0f;
    
    // Discretize ray origin to integer voxel coordinates (floor to get containing voxel)
    glm::ivec3 voxel = glm::ivec3(glm::floor(rayOrigin));
    
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
            
            LOG_INFO_FMT("VoxelRaycaster", "[FACE DETECT] lastStepAxis=" << lastStepAxis 
                       << " step=(" << step.x << "," << step.y << "," << step.z << ")"
                       << " voxel=(" << voxel.x << "," << voxel.y << "," << voxel.z << ")"
                       << " rayDir=(" << rayDirection.x << "," << rayDirection.y << "," << rayDirection.z << ")");
            
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
            
            LOG_INFO_FMT("VoxelRaycaster", "[FACE DETECT] Computed hitFace=" << hitFace 
                       << " hitNormal=(" << hitNormal.x << "," << hitNormal.y << "," << hitNormal.z << ")");
            
            location.hitFace = hitFace;
            location.hitNormal = hitNormal;
            
            // Capture hit data for debug visualization
            if (m_debugCaptureEnabled) {
                m_lastDebugData.hasHit = true;
                m_lastDebugData.hitFace = hitFace;
                m_lastDebugData.hitNormal = hitNormal;
                // Calculate approximate hit point
                m_lastDebugData.hitPoint = glm::vec3(voxel) + glm::vec3(0.5f);
                m_lastDebugData.hitLocation = location;
            }
            
            // If subdivided, resolve specific subcube
            if (location.type == VoxelLocation::SUBDIVIDED) {
                LOG_TRACE_FMT("VoxelRaycaster", "[PICK] Found SUBDIVIDED voxel at " << voxel.x << "," << voxel.y << "," << voxel.z << " - resolving...");
                VoxelLocation resolved = resolveSubcubeInVoxel(rayOrigin, rayDirection, location);
                if (resolved.isValid()) {
                    return resolved; // Found a subcube/microcube
                }
                // Ray passed through empty space in subdivided voxel - continue DDA to next voxel
                LOG_TRACE_FMT("VoxelRaycaster", "[PICK] Ray passed through subdivided voxel without hitting - continuing to next voxel");
            } else {
                return location; // Regular cube
            }
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

VoxelLocation VoxelRaycaster::resolveSubcubeInVoxel(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const VoxelLocation& voxelHit
) const {
    // PERFORMANCE FIX: Early exit if ray doesn't intersect the parent voxel at all
    glm::vec3 voxelMin = glm::vec3(voxelHit.worldPos);
    glm::vec3 voxelMax = voxelMin + glm::vec3(1.0f);
    float voxelIntersectionDist;
    if (!rayAABBIntersect(rayOrigin, rayDirection, voxelMin, voxelMax, voxelIntersectionDist)) {
        LOG_DEBUG("VoxelRaycaster", "[RESOLVE] Ray doesn't intersect subdivided voxel bounds - returning empty");
        return VoxelLocation(); // Ray doesn't hit this voxel at all
    }
    
    // Get all existing subcubes at this voxel position
    std::vector<Subcube*> existingSubcubes = voxelHit.chunk->getSubcubesAt(voxelHit.localPos);
    
    LOG_TRACE_FMT("VoxelRaycaster", "[RESOLVE] Checking subdivided voxel - found " << existingSubcubes.size() << " subcubes");
    
    // CRITICAL FIX: Always check for BOTH microcubes AND subcubes, then return the closest hit
    float closestDistance = std::numeric_limits<float>::max();
    VoxelLocation closestHit = VoxelLocation(); // Invalid by default
    
    // First, check for microcubes at all possible subcube positions
    LOG_DEBUG("VoxelRaycaster", "[RESOLVE] Checking for microcubes...");
    int totalMicrocubesChecked = 0;
    for (int sx = 0; sx < 3; ++sx) {
        for (int sy = 0; sy < 3; ++sy) {
            for (int sz = 0; sz < 3; ++sz) {
                glm::ivec3 subcubePos(sx, sy, sz);
                std::vector<Microcube*> microcubes = voxelHit.chunk->getMicrocubesAt(voxelHit.localPos, subcubePos);
                
                if (!microcubes.empty()) {
                    totalMicrocubesChecked += microcubes.size();
                    LOG_TRACE_FMT("VoxelRaycaster", "[RESOLVE] Found " << microcubes.size() << " microcubes at subcube pos " << sx << "," << sy << "," << sz);
                    
                    for (Microcube* microcube : microcubes) {
                        if (!microcube || !microcube->isVisible()) continue;
                        
                        glm::ivec3 microcubeLocalPos = microcube->getMicrocubeLocalPosition();
                        
                        // Calculate microcube's world bounding box (1/9 scale)
                        float microcubeSize = 1.0f / 9.0f;
                        float subcubeSize = 1.0f / 3.0f;
                        glm::vec3 subcubeOffset = glm::vec3(subcubePos) * subcubeSize;
                        glm::vec3 microcubeOffset = glm::vec3(microcubeLocalPos) * microcubeSize;
                        glm::vec3 microcubeMin = glm::vec3(voxelHit.worldPos) + subcubeOffset + microcubeOffset;
                        glm::vec3 microcubeMax = microcubeMin + glm::vec3(microcubeSize);
                        
                        // Ray-AABB intersection test
                        float intersectionDistance;
                        if (rayAABBIntersect(rayOrigin, rayDirection, microcubeMin, microcubeMax, intersectionDistance)) {
                            if (intersectionDistance >= 0.0f && intersectionDistance < closestDistance) {
                                closestDistance = intersectionDistance;
                                closestHit = voxelHit;
                                closestHit.subcubePos = microcube->getSubcubeLocalPosition();
                                closestHit.microcubePos = microcube->getMicrocubeLocalPosition();
                                LOG_TRACE_FMT("VoxelRaycaster", "[RESOLVE] Found closer microcube at distance " << intersectionDistance);
                                
                                // Capture debug data for microcube hit
                                if (m_debugCaptureEnabled) {
                                    glm::vec3 hitPoint = rayOrigin + rayDirection * intersectionDistance;
                                    m_lastDebugData.hitPoint = hitPoint;
                                    m_lastDebugData.hasHit = true;
                                    
                                    // Determine which face was hit based on hit point position within AABB
                                    glm::vec3 center = (microcubeMin + microcubeMax) * 0.5f;
                                    glm::vec3 halfExtents = (microcubeMax - microcubeMin) * 0.5f;
                                    glm::vec3 localHit = hitPoint - center;
                                    
                                    // Find which axis has the largest relative value (that's the hit face)
                                    glm::vec3 absLocal = glm::abs(localHit / halfExtents);
                                    if (absLocal.x > absLocal.y && absLocal.x > absLocal.z) {
                                        m_lastDebugData.hitNormal = glm::vec3(localHit.x > 0 ? 1.0f : -1.0f, 0, 0);
                                        m_lastDebugData.hitFace = localHit.x > 0 ? 0 : 1;
                                    } else if (absLocal.y > absLocal.z) {
                                        m_lastDebugData.hitNormal = glm::vec3(0, localHit.y > 0 ? 1.0f : -1.0f, 0);
                                        m_lastDebugData.hitFace = localHit.y > 0 ? 2 : 3;
                                    } else {
                                        m_lastDebugData.hitNormal = glm::vec3(0, 0, localHit.z > 0 ? 1.0f : -1.0f);
                                        m_lastDebugData.hitFace = localHit.z > 0 ? 4 : 5;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    if (totalMicrocubesChecked > 0) {
        LOG_DEBUG_FMT("VoxelRaycaster", "[RESOLVE] Checked " << totalMicrocubesChecked << " total microcubes");
    }
    
    // Second, test ray intersection against each existing subcube's bounding box
    LOG_DEBUG_FMT("VoxelRaycaster", "[RESOLVE] Checking " << existingSubcubes.size() << " subcubes...");
    for (Subcube* subcube : existingSubcubes) {
        if (!subcube || !subcube->isVisible()) continue;
        
        glm::ivec3 subcubeLocalPos = subcube->getLocalPosition();
        
        // Calculate subcube's world bounding box (each subcube is 1/3 scale within the parent cube)
        float subcubeSize = 1.0f / 3.0f;
        glm::vec3 subcubeMin = glm::vec3(voxelHit.worldPos) + glm::vec3(subcubeLocalPos) * subcubeSize;
        glm::vec3 subcubeMax = subcubeMin + glm::vec3(subcubeSize);
        
        // Ray-AABB intersection test
        float intersectionDistance;
        if (rayAABBIntersect(rayOrigin, rayDirection, subcubeMin, subcubeMax, intersectionDistance)) {
            // Check if this is the closest intersection so far
            if (intersectionDistance >= 0.0f && intersectionDistance < closestDistance) {
                closestDistance = intersectionDistance;
                closestHit = voxelHit;
                closestHit.subcubePos = subcube->getLocalPosition();
                closestHit.microcubePos = glm::ivec3(-1); // Not a microcube
                LOG_TRACE_FMT("VoxelRaycaster", "[RESOLVE] Found closer subcube at distance " << intersectionDistance);
                
                // Capture debug data for subcube hit
                if (m_debugCaptureEnabled) {
                    glm::vec3 hitPoint = rayOrigin + rayDirection * intersectionDistance;
                    m_lastDebugData.hitPoint = hitPoint;
                    m_lastDebugData.hasHit = true;
                    
                    // Determine which face was hit based on hit point position within AABB
                    glm::vec3 center = (subcubeMin + subcubeMax) * 0.5f;
                    glm::vec3 halfExtents = (subcubeMax - subcubeMin) * 0.5f;
                    glm::vec3 localHit = hitPoint - center;
                    
                    // Find which axis has the largest relative value (that's the hit face)
                    glm::vec3 absLocal = glm::abs(localHit / halfExtents);
                    if (absLocal.x > absLocal.y && absLocal.x > absLocal.z) {
                        m_lastDebugData.hitNormal = glm::vec3(localHit.x > 0 ? 1.0f : -1.0f, 0, 0);
                        m_lastDebugData.hitFace = localHit.x > 0 ? 0 : 1;
                    } else if (absLocal.y > absLocal.z) {
                        m_lastDebugData.hitNormal = glm::vec3(0, localHit.y > 0 ? 1.0f : -1.0f, 0);
                        m_lastDebugData.hitFace = localHit.y > 0 ? 2 : 3;
                    } else {
                        m_lastDebugData.hitNormal = glm::vec3(0, 0, localHit.z > 0 ? 1.0f : -1.0f);
                        m_lastDebugData.hitFace = localHit.z > 0 ? 4 : 5;
                    }
                }
            }
        }
    }
    
    // Return the closest hit (microcube or subcube), or invalid if nothing was hit
    if (closestHit.isValid()) {
        if (closestHit.isMicrocube()) {
            LOG_ERROR_FMT("VoxelRaycaster", "[RESOLVE] *** RETURNING MICROCUBE *** at subcube (" 
                      << closestHit.subcubePos.x << "," << closestHit.subcubePos.y << "," << closestHit.subcubePos.z
                      << ") micro (" << closestHit.microcubePos.x << "," << closestHit.microcubePos.y << "," << closestHit.microcubePos.z
                      << ") distance: " << closestDistance);
        } else if (closestHit.isSubcube()) {
            LOG_ERROR_FMT("VoxelRaycaster", "[RESOLVE] *** RETURNING SUBCUBE *** at pos (" 
                      << closestHit.subcubePos.x << "," << closestHit.subcubePos.y << "," << closestHit.subcubePos.z
                      << ") distance: " << closestDistance);
        } else {
            LOG_ERROR_FMT("VoxelRaycaster", "[RESOLVE] *** RETURNING REGULAR CUBE *** distance: " << closestDistance);
        }
        return closestHit;
    }
    
    // Ray passed through subdivided voxel without hitting any subcube/microcube
    if (totalMicrocubesChecked > 0 || existingSubcubes.size() > 0) {
        LOG_WARN_FMT("VoxelRaycaster", "[RESOLVE] Ray passed through subdivided voxel at (" 
                  << voxelHit.worldPos.x << "," << voxelHit.worldPos.y << "," << voxelHit.worldPos.z 
                  << ") with " << totalMicrocubesChecked << " microcubes and " << existingSubcubes.size() 
                  << " subcubes, but didn't hit any - position should probably be marked EMPTY");
    }
    
    LOG_DEBUG("VoxelRaycaster", "[RESOLVE] No subcubes or microcubes intersected (ray passed through empty space)");
    return VoxelLocation(); // No subcube or microcube intersected
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
    
    // Convert to eye space
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

} // namespace VulkanCube
