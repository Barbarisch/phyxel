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
    , m_hoverDetectionTimeMs(0.0)
    , m_avgHoverDetectionTimeMs(0.0)
    , m_hoverDetectionSamples(0)
{
    // Configure VoxelManipulationSystem callbacks
    m_manipulator.setCallbacks(
        [this]() -> ChunkManager* { return m_chunkManager; },
        [this]() -> Physics::PhysicsWorld* { return m_physicsWorld; }
    );
    
    // Initialize tools
    m_placementTool = std::make_unique<PlacementTool>(&m_manipulator);
    m_destructionTool = std::make_unique<DestructionTool>(&m_manipulator);
    
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
    
    // Cache camera state for interaction tools
    m_lastCameraPos = cameraPos;
    m_lastCameraFront = cameraFront;
    m_lastCameraUp = cameraUp;
    
    // Performance timing
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create ray from mouse position (delegated to VoxelRaycaster)
    glm::vec3 rayDirection = m_raycaster.screenToWorldRay(
        mouseX, mouseY, cameraPos, cameraFront, cameraUp,
        [this]() -> UI::WindowManager* { return m_windowManager; }
    );
    
    // NEW: Use optimized O(1) VoxelLocation-based hover detection (delegated to VoxelRaycaster)
    VoxelLocation voxelLocation = m_raycaster.pickVoxel(
        cameraPos, rayDirection,
        [this]() -> ChunkManager* { return m_chunkManager; }
    );
    
    // NEW: Calculate and visualize placement target
    if (voxelLocation.isValid()) {
        // Use robust placement calculation
        PlacementTool::PlacementResult result = PlacementTool::calculatePlacement(
            voxelLocation.hitPoint,
            voxelLocation.hitNormal,
            voxelLocation.worldPos
        );
        
        // Calculate world space center of the target subcube
        // Subcube size is 1/3 of a unit
        float subcubeSize = 1.0f / 3.0f;
        glm::vec3 subcubeCenter = glm::vec3(result.cubePos) + 
                                 glm::vec3(result.subcubePos) * subcubeSize + 
                                 glm::vec3(subcubeSize * 0.5f);
                                 
        m_raycaster.setDebugTarget(subcubeCenter);
    } else {
        m_raycaster.clearDebugTarget();
    }
    
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
    
    // Update hover state if changed OR if hitFace changed (same cube, different face)
    bool locationChanged = (hoveredCube != m_lastHoveredCube);
    bool hitFaceChanged = (hoveredLocation.isValid() && m_currentHoveredLocation.isValid() && 
                          hoveredLocation.worldPos == m_currentHoveredLocation.worldPos &&
                          hoveredLocation.hitFace != m_currentHoveredLocation.hitFace);
    
    if (locationChanged || hitFaceChanged) {
        if (m_lastHoveredCube >= 0 && locationChanged) {
            clearHoveredCubeInChunksOptimized();
        }
        
        if (hoveredCube >= 0) {
            setHoveredCubeInChunksOptimized(hoveredLocation);
        }
        
        m_lastHoveredCube = hoveredCube;
    }
}

InteractionContext VoxelInteractionSystem::createContext() const {
    InteractionContext context;
    context.hoveredLocation = m_currentHoveredLocation;
    context.hasHovered = m_hasHoveredCube;
    context.cameraPosition = m_lastCameraPos;
    context.cameraFront = m_lastCameraFront;
    context.cameraUp = m_lastCameraUp;
    
    LOG_INFO_FMT("VoxelInteraction", "[CREATE CONTEXT] m_currentHoveredLocation: worldPos=(" << m_currentHoveredLocation.worldPos.x << "," << m_currentHoveredLocation.worldPos.y << "," << m_currentHoveredLocation.worldPos.z << ") hitFace=" << m_currentHoveredLocation.hitFace << " hitPoint=(" << m_currentHoveredLocation.hitPoint.x << "," << m_currentHoveredLocation.hitPoint.y << "," << m_currentHoveredLocation.hitPoint.z << ")");
    
    return context;
}

void VoxelInteractionSystem::removeHoveredCube() {
    if (m_hasHoveredCube && m_currentHoveredLocation.isValid()) {
        m_manipulator.removeVoxel(m_currentHoveredLocation);
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1;
    }
}

void VoxelInteractionSystem::subdivideHoveredCube() {
    if (m_destructionTool->subdivideCube(createContext())) {
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1;
    }
}

void VoxelInteractionSystem::subdivideHoveredSubcube() {
    if (m_destructionTool->subdivideSubcube(createContext())) {
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1;
    }
}

void VoxelInteractionSystem::breakHoveredCube(const glm::vec3& cameraPos) {
    if (m_destructionTool->breakCube(createContext())) {
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1;
    }
}

void VoxelInteractionSystem::breakHoveredSubcube() {
    if (m_destructionTool->breakSubcube(createContext())) {
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1;
    }
}

void VoxelInteractionSystem::breakHoveredMicrocube() {
    if (m_destructionTool->breakMicrocube(createContext())) {
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation();
        m_lastHoveredCube = -1;
    }
}

void VoxelInteractionSystem::breakHoveredCubeWithForce(const glm::vec3& cameraPos, double mouseX, double mouseY) {
    // Delegate to VoxelForceApplicator
    m_forceApplicator.breakHoveredCubeWithForce(
        cameraPos, mouseX, mouseY,
        m_currentHoveredLocation,
        m_hasHoveredCube,
        [this](const glm::vec3& camPos) { breakHoveredCube(camPos); },
        [this]() { breakHoveredSubcube(); },
        [this]() -> ChunkManager* { return m_chunkManager; },
        [this]() -> ForceSystem* { return m_forceSystem; },
        [this]() -> MouseVelocityTracker* { return m_mouseVelocityTracker; }
    );
}

void VoxelInteractionSystem::breakCubeAtPosition(const glm::ivec3& worldPos) {
    // Delegate to manipulation system
    m_manipulator.breakCubeAtPosition(worldPos, m_debugFlags.disableBreakingForces);
}

void VoxelInteractionSystem::placeVoxelAtHover() {
    m_placementTool->placeVoxel(createContext());
}

void VoxelInteractionSystem::placeSubcubeAtHover() {
    m_placementTool->placeSubcube(createContext());
}

void VoxelInteractionSystem::placeMicrocubeAtHover() {
    m_placementTool->placeMicrocube(createContext());
}

VoxelLocation VoxelInteractionSystem::pickVoxelOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    // Delegate to VoxelRaycaster
    return m_raycaster.pickVoxel(
        rayOrigin, rayDirection,
        [this]() -> ChunkManager* { return m_chunkManager; }
    );
}

VoxelLocation VoxelInteractionSystem::resolveSubcubeInVoxel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, 
                                                           const VoxelLocation& voxelHit) const {
    // Delegate to VoxelRaycaster (note: this is actually a private method in VoxelRaycaster, called internally by pickVoxel)
    // This wrapper exists for backward compatibility but is not typically called directly
    return VoxelLocation(); // Empty - functionality integrated into pickVoxel
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
    cubeLocation.hitPoint = voxelLoc.hitPoint;  // Copy hitPoint for subcube/microcube placement
    
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
        
        m_currentHoveredLocation = location;
        m_hasHoveredCube = true;
        
        LOG_DEBUG_FMT("HoverDetection", "Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z << ")");
        
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
        
        m_currentHoveredLocation = location;
        m_hasHoveredCube = true;
        
        LOG_INFO_FMT("Application", "[CUBE HOVER] Setting m_currentHoveredLocation: worldPos=(" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ") hitFace=" << location.hitFace);
    }
}

void VoxelInteractionSystem::clearHoveredCubeInChunksOptimized() {
    if (m_hasHoveredCube && m_currentHoveredLocation.isValid()) {
        LOG_TRACE_FMT("HoverDetection", "Clearing hover - was: isMicrocube=" << m_currentHoveredLocation.isMicrocube 
                  << " isSubcube=" << m_currentHoveredLocation.isSubcube);
        
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation(); // Reset to invalid state
    }
}

bool VoxelInteractionSystem::rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                                             const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                                             float& distance) const {
    // Delegate to VoxelRaycaster
    return m_raycaster.rayAABBIntersect(rayOrigin, rayDir, aabbMin, aabbMax, distance);
}

glm::vec3 VoxelInteractionSystem::screenToWorldRay(double mouseX, double mouseY, const glm::vec3& cameraPos,
                                                   const glm::vec3& cameraFront, const glm::vec3& cameraUp) const {
    // Delegate to VoxelRaycaster
    return m_raycaster.screenToWorldRay(
        mouseX, mouseY, cameraPos, cameraFront, cameraUp,
        [this]() -> UI::WindowManager* { return m_windowManager; }
    );
}

glm::ivec3 CubeLocation::getAdjacentPlacementPosition() const {
    if (hitFace < 0) return worldPos; // No face data available
    
    // Calculate offset based on which face was hit
    // Face numbering matches raycaster: 0=left(-X), 1=right(+X), 2=bottom(-Y), 3=top(+Y), 4=back(-Z), 5=front(+Z)
    glm::ivec3 offset(0);
    switch (hitFace) {
        case 0: offset = glm::ivec3(-1, 0, 0); break;  // left (-X face)
        case 1: offset = glm::ivec3(1, 0, 0); break;   // right (+X face)
        case 2: offset = glm::ivec3(0, -1, 0); break;  // bottom (-Y face)
        case 3: offset = glm::ivec3(0, 1, 0); break;   // top (+Y face)
        case 4: offset = glm::ivec3(0, 0, -1); break;  // back (-Z face)
        case 5: offset = glm::ivec3(0, 0, 1); break;   // front (+Z face)
    }
    
    glm::ivec3 placementPos = worldPos + offset;
    LOG_DEBUG_FMT("VoxelInteraction", "[FACE DEBUG] Hit face " << hitFace << " on cube at (" 
              << worldPos.x << "," << worldPos.y << "," << worldPos.z 
              << "), placing at (" << placementPos.x << "," << placementPos.y << "," << placementPos.z << ")");
    
    return placementPos;
}

} // namespace VulkanCube
