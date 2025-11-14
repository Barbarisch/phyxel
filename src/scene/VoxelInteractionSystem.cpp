#include "scene/VoxelInteractionSystem.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "ui/WindowManager.h"
#include "core/ForceSystem.h"
#include "utils/CoordinateUtils.h"
#include "physics/Material.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <iomanip>
#include <limits>

namespace VulkanCube {

VoxelInteractionSystem::VoxelInteractionSystem(ChunkManager* chunkManager,
                                             Physics::PhysicsWorld* physicsWorld,
                                             MouseVelocityTracker* mouseVelocityTracker,
                                             UI::WindowManager* windowManager,
                                             ForceSystem* forceSystem)
    : m_chunkManager(chunkManager)
    , m_physicsWorld(physicsWorld)
    , m_mouseVelocityTracker(mouseVelocityTracker)
    , m_windowManager(windowManager)
    , m_forceSystem(forceSystem)
    , m_hasHoveredCube(false)
    , m_lastHoveredCube(-1)
    , m_originalHoveredColor(0.0f)
    , m_hoverDetectionTimeMs(0.0)
    , m_avgHoverDetectionTimeMs(0.0)
    , m_hoverDetectionSamples(0)
{
    LOG_INFO("VoxelInteractionSystem", "VoxelInteractionSystem initialized");
}

// Main hover detection function
void VoxelInteractionSystem::updateMouseHover(const glm::vec3& cameraPos, const glm::vec3& cameraFront,
                                             const glm::vec3& cameraUp, double mouseX, double mouseY,
                                             bool isMouseCaptured) {
    // Skip hover detection if right mouse button is pressed (camera mode)
    if (isMouseCaptured) {
        return;
    }
    
    // Performance timing
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create ray from mouse position
    glm::vec3 rayDirection = screenToWorldRay(mouseX, mouseY, cameraPos, cameraFront, cameraUp);
    
    // NEW: Use optimized O(1) VoxelLocation-based hover detection
    VoxelLocation voxelLocation = pickVoxelOptimized(cameraPos, rayDirection);
    
    // Debug logging for hover detection
    static int logCounter = 0;
    if (voxelLocation.isValid() && (++logCounter % 60 == 0)) { // Log once per second at 60fps
        if (voxelLocation.isMicrocube()) {
            LOG_DEBUG_FMT("VoxelInteraction", "[HOVER] Detected MICROCUBE at world " << voxelLocation.worldPos.x << "," << voxelLocation.worldPos.y << "," << voxelLocation.worldPos.z 
                      << " subcube " << voxelLocation.subcubePos.x << "," << voxelLocation.subcubePos.y << "," << voxelLocation.subcubePos.z
                      << " microcube " << voxelLocation.microcubePos.x << "," << voxelLocation.microcubePos.y << "," << voxelLocation.microcubePos.z);
        } else if (voxelLocation.isSubcube()) {
            LOG_DEBUG_FMT("VoxelInteraction", "[HOVER] Detected SUBCUBE at world " << voxelLocation.worldPos.x << "," << voxelLocation.worldPos.y << "," << voxelLocation.worldPos.z
                      << " subcube " << voxelLocation.subcubePos.x << "," << voxelLocation.subcubePos.y << "," << voxelLocation.subcubePos.z);
        } else if (voxelLocation.isCube()) {
            LOG_DEBUG_FMT("VoxelInteraction", "[HOVER] Detected CUBE at world " << voxelLocation.worldPos.x << "," << voxelLocation.worldPos.y << "," << voxelLocation.worldPos.z);
        }
    }
    
    // Convert to CubeLocation for backward compatibility with existing hover system
    CubeLocation hoveredLocation = voxelLocationToCubeLocation(voxelLocation);
    
    // Performance timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    m_hoverDetectionTimeMs = duration.count() / 1000.0;
    
    // Update rolling average
    m_hoverDetectionSamples++;
    m_avgHoverDetectionTimeMs = (m_avgHoverDetectionTimeMs * (m_hoverDetectionSamples - 1) + m_hoverDetectionTimeMs) / m_hoverDetectionSamples;
    
    // Log performance periodically (every 300 frames to avoid spam)
    if (m_hoverDetectionSamples % 300 == 0) {
        LOG_TRACE_FMT("Performance", "[HOVER PERF] Current: " << std::fixed << std::setprecision(3) 
                  << m_hoverDetectionTimeMs << "ms, Average: " << m_avgHoverDetectionTimeMs 
                  << "ms (over " << m_hoverDetectionSamples << " samples)");
    }
    
    // Convert location to a simple index for tracking (use a hash or simple approach)
    int hoveredCube = -1;
    if (hoveredLocation.isValid()) {
        // Enhanced hash for tracking: include subcube position for proper subcube hover detection
        // Use smaller multipliers to avoid integer overflow for subcube coordinates
        hoveredCube = hoveredLocation.worldPos.x + hoveredLocation.worldPos.y * 1000 + hoveredLocation.worldPos.z * 1000000;
        
        // If it's a microcube, include both subcube and microcube positions in hash
        if (hoveredLocation.isMicrocube) {
            hoveredCube += hoveredLocation.subcubePos.x * 27 + 
                          hoveredLocation.subcubePos.y * 9 + 
                          hoveredLocation.subcubePos.z * 3;
            hoveredCube += hoveredLocation.microcubePos.x * 729 + 
                          hoveredLocation.microcubePos.y * 243 + 
                          hoveredLocation.microcubePos.z * 81;
        }
        // If it's a subcube, include subcube position in hash to distinguish between subcubes of same parent
        else if (hoveredLocation.isSubcube) {
            hoveredCube += hoveredLocation.subcubePos.x * 27 + 
                          hoveredLocation.subcubePos.y * 9 + 
                          hoveredLocation.subcubePos.z * 3;
        }
    }
    
    // Update hover state if changed
    if (hoveredCube != m_lastHoveredCube) {
        if (m_lastHoveredCube >= 0) {
            clearHoveredCubeInChunksOptimized();
        }
        
        if (hoveredCube >= 0) {
            setHoveredCubeInChunksOptimized(hoveredLocation);
        }
        
        m_lastHoveredCube = hoveredCube;
    }
}

void VoxelInteractionSystem::removeHoveredCube() {
    // Check if we have a valid hovered cube
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE REMOVAL] No cube is currently being hovered - cannot remove");
        return;
    }
    
    // Get the chunk and remove the cube using the stored location
    Chunk* chunk = m_currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[CUBE REMOVAL] ERROR: Invalid chunk pointer");
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        return;
    }
    
    bool removed = false;
    
    if (m_currentHoveredLocation.isSubcube) {
        // Remove a specific subcube
        removed = chunk->removeSubcube(m_currentHoveredLocation.localPos, m_currentHoveredLocation.subcubePos);
        if (removed) {
            LOG_DEBUG_FMT("Application", "[SUBCUBE REMOVAL] Successfully removed subcube at world pos: (" 
                      << m_currentHoveredLocation.worldPos.x << "," 
                      << m_currentHoveredLocation.worldPos.y << "," 
                      << m_currentHoveredLocation.worldPos.z << ") subcube: ("
                      << m_currentHoveredLocation.subcubePos.x << ","
                      << m_currentHoveredLocation.subcubePos.y << ","
                      << m_currentHoveredLocation.subcubePos.z << ")");
                      
            // Check if this was the last subcube - if so, restore the parent cube
            auto remainingSubcubes = chunk->getSubcubesAt(m_currentHoveredLocation.localPos);
            if (remainingSubcubes.empty()) {
                // No more subcubes left, restore the parent cube
                Cube* parentCube = chunk->getCubeAt(m_currentHoveredLocation.localPos);
                if (parentCube) {
                    parentCube->show(); // Make parent cube visible again
                    LOG_DEBUG("Application", "[CUBE RESTORATION] Restored parent cube as no subcubes remain");
                }
            }
        }
    } else {
        // Remove a regular cube (this will also remove all its subcubes if subdivided)
        Chunk* chunk = m_currentHoveredLocation.chunk;
        std::vector<Subcube*> subcubes = chunk->getSubcubesAt(m_currentHoveredLocation.localPos);
        if (!subcubes.empty()) {
            // First clear all subcubes
            chunk->clearSubdivisionAt(m_currentHoveredLocation.localPos);
            LOG_DEBUG("Application", "[SUBDIVISION REMOVAL] Cleared all subcubes for cube removal");
        }
        
        // Remove the cube itself
        removed = chunk->removeCube(m_currentHoveredLocation.localPos);
        if (removed) {
            LOG_DEBUG_FMT("Application", "[CUBE REMOVAL] Successfully removed cube at world pos: (" 
                      << m_currentHoveredLocation.worldPos.x << "," 
                      << m_currentHoveredLocation.worldPos.y << "," 
                      << m_currentHoveredLocation.worldPos.z << ")");
        }
    }
    
    if (removed) {
        // No need to mark chunk dirty - removal methods now immediately update GPU buffer
        
        // Clear hover state since the object no longer exists
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1; // Reset the hover tracking
        
    } else {
        LOG_WARN("Application", "[REMOVAL] WARNING: Failed to remove object - it may not exist");
    }
}

void VoxelInteractionSystem::subdivideHoveredCube() {
    // Check if we have a valid hovered cube
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE SUBDIVISION] No cube is currently being hovered - cannot subdivide");
        return;
    }

    // Only subdivide regular cubes (not subcubes)
    if (m_currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[CUBE SUBDIVISION] Cannot subdivide individual subcubes - use left click to break subcubes");
        return;
    }

    // Get the chunk using the stored location
    Chunk* chunk = m_currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[CUBE SUBDIVISION] ERROR: Invalid chunk pointer");
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        return;
    }

    // Check if cube is already subdivided
    if (chunk->getSubcubesAt(m_currentHoveredLocation.localPos).size() > 0) {
        LOG_DEBUG_FMT("Application", "[CUBE SUBDIVISION] Cube at world pos (" 
                  << m_currentHoveredLocation.worldPos.x << "," 
                  << m_currentHoveredLocation.worldPos.y << "," 
                  << m_currentHoveredLocation.worldPos.z << ") is already subdivided");
        return;
    }

    // Subdivide the cube into 27 static subcubes
    bool subdivided = chunk->subdivideAt(m_currentHoveredLocation.localPos);
    if (subdivided) {
        LOG_INFO_FMT("Application", "[CUBE SUBDIVISION] Successfully subdivided cube at world pos: (" 
                  << m_currentHoveredLocation.worldPos.x << "," 
                  << m_currentHoveredLocation.worldPos.y << "," 
                  << m_currentHoveredLocation.worldPos.z << ") into 27 static subcubes");
    } else {
        LOG_WARN("Application", "[CUBE SUBDIVISION] WARNING: Failed to subdivide cube - cube may not exist");
    }

    // Use efficient selective update instead of marking entire chunk dirty
    if (m_chunkManager) {
        m_chunkManager->updateAfterCubeSubdivision(m_currentHoveredLocation.worldPos);
    }

    // Clear hover state
    m_hasHoveredCube = false;
    m_currentHoveredLocation = CubeLocation();
    m_lastHoveredCube = -1;
}

void VoxelInteractionSystem::subdivideHoveredSubcube() {
    // Check if we have a valid hovered cube
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[SUBCUBE SUBDIVISION] No voxel is currently being hovered - cannot subdivide");
        return;
    }

    // Only subdivide subcubes (not regular cubes or microcubes)
    if (!m_currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[SUBCUBE SUBDIVISION] Cannot subdivide regular cubes with this action - use Ctrl+Click to subdivide cubes");
        return;
    }

    // Get the chunk using the stored location
    Chunk* chunk = m_currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[SUBCUBE SUBDIVISION] ERROR: Invalid chunk pointer");
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        return;
    }

    // Check if subcube is already subdivided into microcubes
    if (chunk->getMicrocubesAt(m_currentHoveredLocation.localPos, m_currentHoveredLocation.subcubePos).size() > 0) {
        LOG_DEBUG_FMT("Application", "[SUBCUBE SUBDIVISION] Subcube at cube world pos (" 
                  << m_currentHoveredLocation.worldPos.x << "," 
                  << m_currentHoveredLocation.worldPos.y << "," 
                  << m_currentHoveredLocation.worldPos.z << ") subcube pos ("
                  << m_currentHoveredLocation.subcubePos.x << ","
                  << m_currentHoveredLocation.subcubePos.y << ","
                  << m_currentHoveredLocation.subcubePos.z << ") is already subdivided");
        return;
    }

    // Subdivide the subcube into 27 static microcubes
    bool subdivided = chunk->subdivideSubcubeAt(m_currentHoveredLocation.localPos, m_currentHoveredLocation.subcubePos);
    if (subdivided) {
        LOG_INFO_FMT("Application", "[SUBCUBE SUBDIVISION] Successfully subdivided subcube at cube world pos: (" 
                  << m_currentHoveredLocation.worldPos.x << "," 
                  << m_currentHoveredLocation.worldPos.y << "," 
                  << m_currentHoveredLocation.worldPos.z << ") subcube pos ("
                  << m_currentHoveredLocation.subcubePos.x << ","
                  << m_currentHoveredLocation.subcubePos.y << ","
                  << m_currentHoveredLocation.subcubePos.z << ") into 27 static microcubes");
    } else {
        LOG_WARN("Application", "[SUBCUBE SUBDIVISION] WARNING: Failed to subdivide subcube - subcube may not exist");
    }

    // Use efficient selective update instead of marking entire chunk dirty
    if (m_chunkManager) {
        m_chunkManager->updateAfterCubeSubdivision(m_currentHoveredLocation.worldPos);
    }

    // Clear hover state
    m_hasHoveredCube = false;
    m_currentHoveredLocation = CubeLocation();
    m_lastHoveredCube = -1;
}

void VoxelInteractionSystem::breakHoveredCube(const glm::vec3& cameraPos) {
    // Check if we have a valid hovered cube
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE BREAKING] No cube is currently being hovered - cannot break");
        return;
    }
    
    // Only break regular cubes (not subcubes)
    if (m_currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[CUBE BREAKING] Use breakHoveredSubcube() for subcubes");
        return;
    }
    
    // Get the chunk using the stored location
    Chunk* chunk = m_currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[CUBE BREAKING] ERROR: Invalid chunk pointer");
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        return;
    }
    
    // Get the cube's original color before removing it
    const Cube* originalCube = chunk->getCubeAt(m_currentHoveredLocation.localPos);
    if (!originalCube) {
        LOG_DEBUG("Application", "[CUBE BREAKING] No cube exists at this location");
        return;
    }
    
    glm::vec3 originalColor = originalCube->getOriginalColor();
    glm::vec3 cubeWorldPos = glm::vec3(m_currentHoveredLocation.worldPos);
    
    // Remove the cube from the chunk
    bool removed = chunk->removeCube(m_currentHoveredLocation.localPos);
    if (!removed) {
        LOG_WARN("Application", "[CUBE BREAKING] WARNING: Failed to remove cube from chunk");
        return;
    }
    
    // Create a dynamic cube at the position
    glm::vec3 cubeCornerPos = cubeWorldPos;
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f);
    
    // Select material based on position
    std::vector<std::string> materials = {"Wood", "Metal", "Glass", "Rubber", "Stone", "Ice", "Cork"};
    int materialIndex = (abs(static_cast<int>(cubeWorldPos.x) + static_cast<int>(cubeWorldPos.z))) % materials.size();
    std::string selectedMaterial = materials[materialIndex];
    
    auto dynamicCube = std::make_unique<DynamicCube>(cubeCornerPos, originalColor, selectedMaterial);
    
    // Create physics body
    glm::vec3 cubeSize(1.0f);
    btRigidBody* rigidBody = m_physicsWorld->createBreakawaCube(physicsCenterPos, cubeSize, selectedMaterial);
    dynamicCube->setRigidBody(rigidBody);
    dynamicCube->setPhysicsPosition(physicsCenterPos);
    
    // Calculate and apply impulse force
    if (rigidBody && !m_debugFlags.disableBreakingForces) {
        glm::vec3 forceDirection = normalize(cubeWorldPos - cameraPos);
        glm::vec3 impulseForce = forceDirection * 1.5f + glm::vec3(0.0f, 2.5f, 0.0f);
        
        btVector3 btImpulse(impulseForce.x, impulseForce.y, impulseForce.z);
        rigidBody->applyCentralImpulse(btImpulse);
        
        // Add random angular velocity for tumbling effect
        btVector3 angularVelocity(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
        );
        rigidBody->setAngularVelocity(angularVelocity);
        rigidBody->setGravity(btVector3(0, -9.81f, 0));
    } else if (rigidBody) {
        rigidBody->setGravity(btVector3(0, -9.81f, 0));
    }
    
    // Mark as broken
    dynamicCube->breakApart();
    
    // Add to global dynamic cubes system
    m_chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    
    // Update affected chunks
    m_chunkManager->updateAfterCubeBreak(m_currentHoveredLocation.worldPos);
    
    LOG_INFO_FMT("Application", "[CUBE BREAKING] Successfully broke cube at world pos: (" 
              << m_currentHoveredLocation.worldPos.x << "," 
              << m_currentHoveredLocation.worldPos.y << "," 
              << m_currentHoveredLocation.worldPos.z << ")");
    
    // Clear hover state
    m_hasHoveredCube = false;
    m_currentHoveredLocation = CubeLocation();
    m_lastHoveredCube = -1;
}

void VoxelInteractionSystem::breakHoveredSubcube() {
    // Check if we have a valid hovered subcube
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[SUBCUBE BREAKING] No subcube is currently being hovered - cannot break");
        return;
    }

    // Only break subcubes (not regular cubes)
    if (!m_currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[SUBCUBE BREAKING] Hovered object is not a subcube - use left click to break regular cubes");
        return;
    }

    // Get the chunk using the stored location
    Chunk* chunk = m_currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[SUBCUBE BREAKING] ERROR: Invalid chunk pointer");
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        return;
    }

    LOG_DEBUG("Application", "[SUBCUBE BREAKING] Breaking subcube without forces (gentle removal)");
    
    // Break subcube WITHOUT any impulse forces (as requested)
    glm::vec3 noForce(0.0f, 0.0f, 0.0f); // No forces applied
    
    bool broken = chunk->breakSubcube(m_currentHoveredLocation.localPos, m_currentHoveredLocation.subcubePos, 
                                     m_physicsWorld, m_chunkManager, noForce);
    if (broken) {
        LOG_INFO_FMT("Application", "[SUBCUBE BREAKING] Successfully broke subcube (no forces) and transferred to global system at world pos: (" 
                  << m_currentHoveredLocation.worldPos.x << "," 
                  << m_currentHoveredLocation.worldPos.y << "," 
                  << m_currentHoveredLocation.worldPos.z << ") subcube: ("
                  << m_currentHoveredLocation.subcubePos.x << ","
                  << m_currentHoveredLocation.subcubePos.y << ","
                  << m_currentHoveredLocation.subcubePos.z << ")");
                  
        // Use efficient selective update for subcube breaking
        if (m_chunkManager) {
            m_chunkManager->updateAfterSubcubeBreak(m_currentHoveredLocation.worldPos, m_currentHoveredLocation.subcubePos);
        }
    } else {
        LOG_WARN("Application", "[SUBCUBE BREAKING] WARNING: Failed to break subcube");
    }

    // Clear hover state
    m_hasHoveredCube = false;
    m_currentHoveredLocation = CubeLocation();
    m_lastHoveredCube = -1;
}

void VoxelInteractionSystem::breakHoveredMicrocube() {
    LOG_INFO("Application", "[MICROCUBE BREAKING] breakHoveredMicrocube() called");
    
    // Check if we have a valid hovered microcube
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) {
        LOG_DEBUG_FMT("Application", "[MICROCUBE BREAKING] No microcube is currently being hovered - hasHovered=" << m_hasHoveredCube << " isValid=" << m_currentHoveredLocation.isValid());
        return;
    }

    LOG_INFO_FMT("Application", "[MICROCUBE BREAKING] Current location: isMicrocube=" << m_currentHoveredLocation.isMicrocube 
              << " isSubcube=" << m_currentHoveredLocation.isSubcube);
    
    // Only break microcubes
    if (!m_currentHoveredLocation.isMicrocube) {
        LOG_DEBUG("Application", "[MICROCUBE BREAKING] Hovered object is not a microcube");
        return;
    }

    // Get the chunk using the stored location
    Chunk* chunk = m_currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[MICROCUBE BREAKING] ERROR: Invalid chunk pointer");
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        return;
    }

    LOG_DEBUG("Application", "[MICROCUBE BREAKING] Removing microcube (simple removal - no physics yet)");
    
    // Remove the microcube (for now, just delete it - physics support can be added later)
    bool removed = chunk->removeMicrocube(m_currentHoveredLocation.localPos, 
                                         m_currentHoveredLocation.subcubePos,
                                         m_currentHoveredLocation.microcubePos);
    if (removed) {
        LOG_INFO_FMT("Application", "[MICROCUBE BREAKING] Successfully removed microcube at world pos: (" 
                  << m_currentHoveredLocation.worldPos.x << "," 
                  << m_currentHoveredLocation.worldPos.y << "," 
                  << m_currentHoveredLocation.worldPos.z << ") subcube: ("
                  << m_currentHoveredLocation.subcubePos.x << ","
                  << m_currentHoveredLocation.subcubePos.y << ","
                  << m_currentHoveredLocation.subcubePos.z << ") microcube: ("
                  << m_currentHoveredLocation.microcubePos.x << ","
                  << m_currentHoveredLocation.microcubePos.y << ","
                  << m_currentHoveredLocation.microcubePos.z << ")");
                  
        // Rebuild faces for this chunk
        if (m_chunkManager) {
            m_chunkManager->updateAfterCubeSubdivision(m_currentHoveredLocation.worldPos);
        }
    } else {
        LOG_WARN("Application", "[MICROCUBE BREAKING] WARNING: Failed to remove microcube");
    }

    // Clear hover state
    m_hasHoveredCube = false;
    m_currentHoveredLocation = CubeLocation();
    m_lastHoveredCube = -1;
}

void VoxelInteractionSystem::breakHoveredCubeWithForce(const glm::vec3& cameraPos, double mouseX, double mouseY) {
    if (!m_hasHoveredCube || m_currentHoveredLocation.isSubcube) {
        // Just break normally if not applicable for force system
        if (m_currentHoveredLocation.isSubcube) {
            breakHoveredSubcube();
        } else {
            breakHoveredCube(cameraPos);
        }
        return;
    }
    
    // Get mouse velocity
    glm::vec2 mouseVelocity = m_mouseVelocityTracker->getVelocity();
    float speed = glm::length(mouseVelocity);
    
    // If high speed, use force propagation system
    if (speed > 500.0f && m_forceSystem) {
        LOG_INFO_FMT("Application", "[FORCE] High mouse speed detected: " << speed << " - propagating force");
        
        // Calculate click force
        glm::vec3 rayOrigin = cameraPos;
        glm::vec3 cubeCenter = glm::vec3(m_currentHoveredLocation.worldPos) + glm::vec3(0.5f);
        glm::vec3 rayDirection = glm::normalize(cubeCenter - rayOrigin);
        glm::vec3 hitPoint = cubeCenter;
        
        ForceSystem::ClickForce clickForce = m_forceSystem->calculateClickForce(
            mouseVelocity,
            rayOrigin,
            rayDirection,
            hitPoint
        );
        
        // Propagate force through chunk system
        ForceSystem::PropagationResult result = m_forceSystem->propagateForce(
            clickForce,
            m_currentHoveredLocation.worldPos,
            m_chunkManager
        );
        
        LOG_INFO_FMT("Application", "[FORCE] Broke " << result.brokenCubes.size() 
                  << " cubes, damaged " << result.damagedCubes.size() << " cubes");
    } else {
        // Normal break
        breakHoveredCube(cameraPos);
    }
}

void VoxelInteractionSystem::breakCubeAtPosition(const glm::ivec3& worldPos) {
    Chunk* chunk = m_chunkManager->getChunkAt(worldPos);
    if (!chunk) {
        LOG_WARN_FMT("Application", "[FORCE BREAKING] No chunk found for position (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return;
    }
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    const Cube* originalCube = chunk->getCubeAt(localPos);
    if (!originalCube) {
        LOG_WARN_FMT("Application", "[FORCE BREAKING] No cube found at position (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return;
    }
    
    // Get the cube's original color before removing it
    glm::vec3 originalColor = originalCube->getOriginalColor();
    
    // Remove cube from chunk
    bool removed = chunk->removeCube(localPos);
    if (!removed) {
        LOG_WARN("Application", "[FORCE BREAKING] Failed to remove cube from chunk");
        return;
    }
    
    // Create dynamic cube for physics
    glm::vec3 cubeCornerPos = glm::vec3(worldPos);
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f);
    
    // Select material based on position
    std::vector<std::string> materials = {"stone", "wood", "metal", "ice"};
    int materialIndex = (abs(worldPos.x) + abs(worldPos.z)) % materials.size();
    std::string selectedMaterial = materials[materialIndex];
    
    auto dynamicCube = std::make_unique<DynamicCube>(cubeCornerPos, originalColor, selectedMaterial);
    
    // Create physics body
    glm::vec3 cubeSize(1.0f);
    btRigidBody* rigidBody = m_physicsWorld->createBreakawaCube(physicsCenterPos, cubeSize, selectedMaterial);
    dynamicCube->setRigidBody(rigidBody);
    
    // Apply a small random force for natural breaking effect
    if (rigidBody && !m_debugFlags.disableBreakingForces) {
        glm::vec3 randomForce(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f,
            100.0f + (static_cast<float>(rand()) / RAND_MAX) * 100.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f
        );
        
        btVector3 btImpulse(randomForce.x, randomForce.y, randomForce.z);
        rigidBody->applyCentralImpulse(btImpulse);
        
        // Add random angular velocity for tumbling effect
        btVector3 angularVelocity(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
        );
        rigidBody->setAngularVelocity(angularVelocity);
        rigidBody->setGravity(btVector3(0, -9.81f, 0));
    }
    
    // Mark as broken and add to global system
    dynamicCube->breakApart();
    m_chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    
    // Update affected chunks
    m_chunkManager->updateAfterCubeBreak(worldPos);
}

VoxelLocation VoxelInteractionSystem::pickVoxelOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    if (!m_chunkManager) {
        return VoxelLocation(); // Invalid location
    }
    
    // DDA algorithm for efficient voxel traversal
    float maxDistance = 200.0f;
    glm::ivec3 voxel = glm::ivec3(glm::floor(rayOrigin));
    
    glm::ivec3 step = glm::ivec3(
        rayDirection.x > 0 ? 1 : (rayDirection.x < 0 ? -1 : 0),
        rayDirection.y > 0 ? 1 : (rayDirection.y < 0 ? -1 : 0),
        rayDirection.z > 0 ? 1 : (rayDirection.z < 0 ? -1 : 0)
    );
    
    glm::vec3 deltaDist = glm::vec3(
        rayDirection.x != 0 ? glm::abs(1.0f / rayDirection.x) : std::numeric_limits<float>::max(),
        rayDirection.y != 0 ? glm::abs(1.0f / rayDirection.y) : std::numeric_limits<float>::max(),
        rayDirection.z != 0 ? glm::abs(1.0f / rayDirection.z) : std::numeric_limits<float>::max()
    );
    
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
    
    int maxSteps = 500;
    int lastStepAxis = -1;
    
    for (int step_count = 0; step_count < maxSteps; ++step_count) {
        // O(1) voxel resolution using the VoxelLocation system
        VoxelLocation location = m_chunkManager->resolveGlobalPosition(voxel);
        
        if (location.isValid()) {
            LOG_TRACE_FMT("VoxelInteraction", "[PICK] Found valid voxel at " << voxel.x << "," << voxel.y << "," << voxel.z 
                      << " type=" << (int)location.type);
            // Calculate face information from DDA step
            int hitFace = -1;
            glm::vec3 hitNormal(0);
            
            if (lastStepAxis >= 0) {
                if (lastStepAxis == 0) { // X-axis step
                    hitFace = (step.x > 0) ? 1 : 0;
                    hitNormal = (step.x > 0) ? glm::vec3(-1,0,0) : glm::vec3(1,0,0);
                } else if (lastStepAxis == 1) { // Y-axis step
                    hitFace = (step.y > 0) ? 3 : 2;
                    hitNormal = (step.y > 0) ? glm::vec3(0,-1,0) : glm::vec3(0,1,0);
                } else if (lastStepAxis == 2) { // Z-axis step
                    hitFace = (step.z > 0) ? 5 : 4;
                    hitNormal = (step.z > 0) ? glm::vec3(0,0,-1) : glm::vec3(0,0,1);
                }
            }
            
            location.hitFace = hitFace;
            location.hitNormal = hitNormal;
            
            // If subdivided, resolve specific subcube
            if (location.type == VoxelLocation::SUBDIVIDED) {
                LOG_TRACE_FMT("VoxelInteraction", "[PICK] Found SUBDIVIDED voxel at " << voxel.x << "," << voxel.y << "," << voxel.z << " - resolving...");
                return resolveSubcubeInVoxel(rayOrigin, rayDirection, location);
            } else {
                return location; // Regular cube
            }
        }
        
        // DDA step
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            lastStepAxis = 0;
            sideDist.x += deltaDist.x;
            voxel.x += step.x;
        } else if (sideDist.y < sideDist.z) {
            lastStepAxis = 1;
            sideDist.y += deltaDist.y;
            voxel.y += step.y;
        } else {
            lastStepAxis = 2;
            sideDist.z += deltaDist.z;
            voxel.z += step.z;
        }
        
        // Distance check
        glm::vec3 currentPos = glm::vec3(voxel);
        if (glm::length(currentPos - rayOrigin) > maxDistance) {
            break;
        }
    }
    
    return VoxelLocation(); // No voxel found
}

VoxelLocation VoxelInteractionSystem::resolveSubcubeInVoxel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, 
                                                           const VoxelLocation& voxelHit) const {
    // Get all existing subcubes at this voxel position
    std::vector<Subcube*> existingSubcubes = voxelHit.chunk->getSubcubesAt(voxelHit.localPos);
    
    LOG_TRACE_FMT("VoxelInteraction", "[RESOLVE] Checking subdivided voxel - found " << existingSubcubes.size() << " subcubes");
    
    // CRITICAL FIX: Always check for BOTH microcubes AND subcubes, then return the closest hit
    // This handles cases where some subcubes are subdivided into microcubes while others remain as subcubes
    
    float closestDistance = std::numeric_limits<float>::max();
    VoxelLocation closestHit = VoxelLocation(); // Invalid by default
    
    // First, check for microcubes at all possible subcube positions
    LOG_DEBUG("VoxelInteraction", "[RESOLVE] Checking for microcubes...");
    for (int sx = 0; sx < 3; ++sx) {
        for (int sy = 0; sy < 3; ++sy) {
            for (int sz = 0; sz < 3; ++sz) {
                glm::ivec3 subcubePos(sx, sy, sz);
                std::vector<Microcube*> microcubes = voxelHit.chunk->getMicrocubesAt(voxelHit.localPos, subcubePos);
                
                if (!microcubes.empty()) {
                    LOG_INFO_FMT("VoxelInteraction", "[RESOLVE] Found " << microcubes.size() << " microcubes at subcube pos " << sx << "," << sy << "," << sz);
                    
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
                                LOG_INFO_FMT("VoxelInteraction", "[RESOLVE] Found closer microcube at distance " << intersectionDistance);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Second, test ray intersection against each existing subcube's bounding box
    LOG_DEBUG_FMT("VoxelInteraction", "[RESOLVE] Checking " << existingSubcubes.size() << " subcubes...");
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
                LOG_INFO_FMT("VoxelInteraction", "[RESOLVE] Found closer subcube at distance " << intersectionDistance);
            }
        }
    }
    
    // Return the closest hit (microcube or subcube), or invalid if nothing was hit
    if (closestHit.isValid()) {
        if (closestHit.isMicrocube()) {
            LOG_INFO_FMT("VoxelInteraction", "[RESOLVE] Returning MICROCUBE at subcube (" 
                      << closestHit.subcubePos.x << "," << closestHit.subcubePos.y << "," << closestHit.subcubePos.z
                      << ") micro (" << closestHit.microcubePos.x << "," << closestHit.microcubePos.y << "," << closestHit.microcubePos.z
                      << ") distance: " << closestDistance);
        } else {
            LOG_INFO_FMT("VoxelInteraction", "[RESOLVE] Returning SUBCUBE at pos (" 
                      << closestHit.subcubePos.x << "," << closestHit.subcubePos.y << "," << closestHit.subcubePos.z
                      << ") distance: " << closestDistance);
        }
        return closestHit;
    }
    
    LOG_DEBUG("VoxelInteraction", "[RESOLVE] No subcubes or microcubes intersected (ray passed through empty space)");
    return VoxelLocation(); // No subcube or microcube intersected
}

CubeLocation VoxelInteractionSystem::voxelLocationToCubeLocation(const VoxelLocation& voxelLoc) const {
    if (!voxelLoc.isValid()) {
        return CubeLocation(); // Invalid location
    }
    
    CubeLocation cubeLocation;
    cubeLocation.chunk = voxelLoc.chunk;
    cubeLocation.localPos = voxelLoc.localPos;
    cubeLocation.worldPos = voxelLoc.worldPos;
    cubeLocation.hitFace = voxelLoc.hitFace;
    cubeLocation.hitNormal = voxelLoc.hitNormal;
    
    if (voxelLoc.isMicrocube()) {
        cubeLocation.isMicrocube = true;
        cubeLocation.isSubcube = false;
        cubeLocation.subcubePos = voxelLoc.subcubePos;
        cubeLocation.microcubePos = voxelLoc.microcubePos;
        LOG_TRACE_FMT("VoxelInteraction", "[CONVERSION] VoxelLocation->CubeLocation: MICROCUBE");
    } else if (voxelLoc.isSubcube()) {
        cubeLocation.isSubcube = true;
        cubeLocation.isMicrocube = false;
        cubeLocation.subcubePos = voxelLoc.subcubePos;
        cubeLocation.microcubePos = glm::ivec3(-1);
        LOG_TRACE_FMT("VoxelInteraction", "[CONVERSION] VoxelLocation->CubeLocation: SUBCUBE");
    } else {
        cubeLocation.isSubcube = false;
        cubeLocation.isMicrocube = false;
        cubeLocation.subcubePos = glm::ivec3(-1);
        cubeLocation.microcubePos = glm::ivec3(-1);
        LOG_TRACE_FMT("VoxelInteraction", "[CONVERSION] VoxelLocation->CubeLocation: CUBE");
    }
    
    return cubeLocation;
}

CubeLocation VoxelInteractionSystem::findExistingSubcubeHit(Chunk* chunk, const glm::ivec3& localPos, 
                                                           const glm::ivec3& cubeWorldPos, 
                                                           const glm::vec3& rayOrigin, 
                                                           const glm::vec3& rayDirection) const {
    // Get all existing subcubes at this cube position
    std::vector<Subcube*> existingSubcubes = chunk->getSubcubesAt(localPos);
    if (existingSubcubes.empty()) {
        return CubeLocation(); // No subcubes exist
    }
    
    // Test ray intersection against each existing subcube's bounding box
    float closestDistance = std::numeric_limits<float>::max();
    Subcube* closestSubcube = nullptr;
    
    for (Subcube* subcube : existingSubcubes) {
        if (!subcube || !subcube->isVisible()) continue;
        
        glm::ivec3 subcubeLocalPos = subcube->getLocalPosition();
        
        // Calculate subcube's world bounding box (each subcube is 1/3 scale within the parent cube)
        float subcubeSize = 1.0f / 3.0f;
        glm::vec3 subcubeMin = glm::vec3(cubeWorldPos) + glm::vec3(subcubeLocalPos) * subcubeSize;
        glm::vec3 subcubeMax = subcubeMin + glm::vec3(subcubeSize);
        
        // Ray-AABB intersection test
        glm::vec3 invDir = 1.0f / rayDirection;
        glm::vec3 t1 = (subcubeMin - rayOrigin) * invDir;
        glm::vec3 t2 = (subcubeMax - rayOrigin) * invDir;
        
        glm::vec3 tMin = glm::min(t1, t2);
        glm::vec3 tMax = glm::max(t1, t2);
        
        float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
        float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
        
        // Check if ray intersects this subcube and if it's the closest so far
        if (tNear <= tFar && tFar >= 0.0f && tNear < closestDistance) {
            closestDistance = tNear;
            closestSubcube = subcube;
        }
    }
    
    // Return the closest subcube hit, if any
    if (closestSubcube) {
        return CubeLocation(chunk, localPos, cubeWorldPos, closestSubcube->getLocalPosition());
    }
    
    return CubeLocation(); // No subcube intersected
}

void VoxelInteractionSystem::setHoveredCubeInChunksOptimized(const CubeLocation& location) {
    if (!location.isValid()) return;
    
    // Debug: Show what type of location we're hovering
    LOG_INFO_FMT("HoverDetection", "Hovering - isMicrocube: " << location.isMicrocube << " isSubcube: " << location.isSubcube 
              << ", world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")"
              << (location.isMicrocube ? ", microcube pos: (" + std::to_string(location.microcubePos.x) + "," + 
                  std::to_string(location.microcubePos.y) + "," + std::to_string(location.microcubePos.z) + ")" : "")
              << (location.isSubcube ? ", subcube pos: (" + std::to_string(location.subcubePos.x) + "," + 
                  std::to_string(location.subcubePos.y) + "," + std::to_string(location.subcubePos.z) + ")" : ""));
    
    // Clear previous hover
    clearHoveredCubeInChunksOptimized();
    
    // Handle microcube vs subcube vs regular cube hover differently
    if (location.isMicrocube) {
        LOG_INFO("HoverDetection", "Setting hover on MICROCUBE");
        // For now, just store the location - we can add visual feedback later
        m_currentHoveredLocation = location;
        m_hasHoveredCube = true;
        return;
    }
    
    // Handle subcube vs regular cube hover differently
    if (location.isSubcube) {
        // Hovering over a specific subcube
        Chunk* chunk = location.chunk;
        std::vector<Subcube*> subcubes = chunk->getSubcubesAt(location.localPos);
        if (subcubes.empty()) return;
        
        Subcube* subcube = chunk->getSubcubeAt(location.localPos, location.subcubePos);
        if (!subcube) return;
        
        // Store original subcube color and location for later restoration
        m_originalHoveredColor = subcube->getOriginalColor(); // Use original color, not current color
        m_currentHoveredLocation = location;
        m_hasHoveredCube = true;
        
        LOG_DEBUG_FMT("HoverDetection", "Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
                  << ") original color: (" << m_originalHoveredColor.x << "," << m_originalHoveredColor.y << "," << m_originalHoveredColor.z << ")");
        
        // Set hover color (lighten the original color for subtle highlighting)
        glm::vec3 hoverColor = calculateLighterColor(m_originalHoveredColor);
        
        // Use efficient subcube color update
        if (m_chunkManager) {
            m_chunkManager->setSubcubeColorEfficient(location.worldPos, location.subcubePos, hoverColor);
        }
    } else {
        // Hovering over a regular cube (not subdivided)
        Cube* cube = location.chunk->getCubeAt(location.localPos);
        if (!cube) return;
        
        // CRITICAL: Don't apply hover to subdivided cubes (they should be handled as individual subcubes)
        if (!cube->isVisible()) {
            LOG_TRACE_FMT("Application", "[CUBE HOVER] Skipping hover on hidden/subdivided cube at world pos: (" 
                      << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")");
            return;
        }
        
        // Store original color and location for later restoration
        m_originalHoveredColor = cube->getColor();
        m_currentHoveredLocation = location;
        m_hasHoveredCube = true;
        
        LOG_TRACE_FMT("Application", "[CUBE HOVER] Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") original color: (" << m_originalHoveredColor.x << "," << m_originalHoveredColor.y << "," << m_originalHoveredColor.z << ")");
        
        // Set hover color (lighten the original color for subtle highlighting)
        glm::vec3 hoverColor = calculateLighterColor(m_originalHoveredColor);
        
        // Use efficient partial update instead of marking chunk dirty
        if (m_chunkManager) {
            m_chunkManager->setCubeColorEfficient(location.worldPos, hoverColor);
        }
    }
}

void VoxelInteractionSystem::clearHoveredCubeInChunksOptimized() {
    if (m_hasHoveredCube && m_currentHoveredLocation.isValid()) {
        LOG_TRACE_FMT("HoverDetection", "Clearing hover - was: isMicrocube=" << m_currentHoveredLocation.isMicrocube 
                  << " isSubcube=" << m_currentHoveredLocation.isSubcube);
        // Restore original color based on whether it's a subcube or regular cube
        if (m_chunkManager) {
            if (m_currentHoveredLocation.isMicrocube) {
                // For microcubes, just clear the state - no color restore needed yet
                LOG_TRACE("HoverDetection", "Clearing microcube hover");
            } else if (m_currentHoveredLocation.isSubcube) {
                // Restore subcube color
                m_chunkManager->setSubcubeColorEfficient(m_currentHoveredLocation.worldPos, m_currentHoveredLocation.subcubePos, m_originalHoveredColor);
                LOG_TRACE_FMT("Application", "[SUBCUBE HOVER] Cleared hover for subcube at world pos: (" << m_currentHoveredLocation.worldPos.x << "," << m_currentHoveredLocation.worldPos.y << "," << m_currentHoveredLocation.worldPos.z 
                          << ") subcube: (" << m_currentHoveredLocation.subcubePos.x << "," << m_currentHoveredLocation.subcubePos.y << "," << m_currentHoveredLocation.subcubePos.z << ")");
            } else {
                // Restore regular cube color
                m_chunkManager->setCubeColorEfficient(m_currentHoveredLocation.worldPos, m_originalHoveredColor);
                LOG_TRACE_FMT("Application", "[CUBE HOVER] Cleared hover for cube at world pos: (" << m_currentHoveredLocation.worldPos.x << "," << m_currentHoveredLocation.worldPos.y << "," << m_currentHoveredLocation.worldPos.z << ")");
            }
        }
        
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation(); // Reset to invalid state
    }
}

bool VoxelInteractionSystem::rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                                             const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                                             float& distance) const {
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

glm::vec3 VoxelInteractionSystem::screenToWorldRay(double mouseX, double mouseY, const glm::vec3& cameraPos,
                                                   const glm::vec3& cameraFront, const glm::vec3& cameraUp) const {
    // Convert mouse coordinates to normalized device coordinates
    float x = (2.0f * mouseX) / m_windowManager->getWidth() - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / m_windowManager->getHeight(); // Flip Y coordinate
    
    // Create clip space coordinates - flip Y again for Vulkan coordinate system
    glm::vec4 rayClip = glm::vec4(x, -y, -1.0f, 1.0f);
    
    // Convert to eye space
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        (float)m_windowManager->getWidth() / (float)m_windowManager->getHeight(), 
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

glm::vec3 VoxelInteractionSystem::calculateLighterColor(const glm::vec3& originalColor) const {
    // Create a lighter version of the color by mixing with white
    // This approach maintains the color's hue while making it brighter
    const float lightenAmount = 0.4f; // How much lighter (0.0 = no change, 1.0 = pure white)
    
    // Mix the original color with white
    glm::vec3 lighterColor = glm::mix(originalColor, glm::vec3(1.0f), lightenAmount);
    
    // Ensure we don't exceed maximum color values
    lighterColor = glm::min(lighterColor, glm::vec3(1.0f));
    
    return lighterColor;
}

glm::ivec3 CubeLocation::getAdjacentPlacementPosition() const {
    if (hitFace < 0) return worldPos; // No face data available
    
    // Calculate offset based on which face was hit
    glm::ivec3 offset(0);
    switch (hitFace) {
        case 0: offset = glm::ivec3(1, 0, 0); break;  // +X face
        case 1: offset = glm::ivec3(-1, 0, 0); break; // -X face
        case 2: offset = glm::ivec3(0, 1, 0); break;  // +Y face
        case 3: offset = glm::ivec3(0, -1, 0); break; // -Y face
        case 4: offset = glm::ivec3(0, 0, 1); break;  // +Z face
        case 5: offset = glm::ivec3(0, 0, -1); break; // -Z face
    }
    
    return worldPos + offset;
}

} // namespace VulkanCube
