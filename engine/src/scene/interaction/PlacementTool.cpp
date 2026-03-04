#include "scene/interaction/PlacementTool.h"
#include "scene/VoxelManipulationSystem.h"
#include "utils/Logger.h"

namespace VulkanCube {

// Helper function to calculate subcube grid position (0-2) based on hit point on a cube face
// hitPoint: world position where ray hit the cube face
// cubeWorldPos: world position of the cube
// hitFace: which face was hit (0-5)
// Returns: subcube position (0-2 on each axis) that aligns with the hit point
glm::ivec3 PlacementTool::calculateSubcubePositionFromHitPoint(const glm::vec3& hitPoint, const glm::ivec3& cubeWorldPos, int hitFace) {
    // Get local hit position within the cube (0.0 to 1.0 range on each axis)
    glm::vec3 localHit = hitPoint - glm::vec3(cubeWorldPos);
    
    LOG_INFO_FMT("PlacementTool", "[SUBCUBE CALC] hitPoint=(" << hitPoint.x << "," << hitPoint.y << "," << hitPoint.z 
                  << ") cubeWorldPos=(" << cubeWorldPos.x << "," << cubeWorldPos.y << "," << cubeWorldPos.z 
                  << ") localHit=(" << localHit.x << "," << localHit.y << "," << localHit.z << ")");
    
    // Clamp to ensure we're within the cube bounds
    localHit = glm::clamp(localHit, glm::vec3(0.0f), glm::vec3(1.0f));
    
    LOG_INFO_FMT("PlacementTool", "[SUBCUBE CALC] After clamp: localHit=(" << localHit.x << "," << localHit.y << "," << localHit.z << ")");
    
    // Convert to subcube grid coordinates (0, 1, or 2)
    glm::ivec3 subcubePos;
    subcubePos.x = glm::clamp(static_cast<int>(localHit.x * 3.0f), 0, 2);
    subcubePos.y = glm::clamp(static_cast<int>(localHit.y * 3.0f), 0, 2);
    subcubePos.z = glm::clamp(static_cast<int>(localHit.z * 3.0f), 0, 2);
    
    LOG_INFO_FMT("PlacementTool", "[SUBCUBE CALC] Before face override: subcubePos=(" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ") hitFace=" << hitFace);
    
    // For the face we hit, we want to place at the edge closest to the original cube
    // So we override the coordinate perpendicular to the hit face
    switch (hitFace) {
        case 0: subcubePos.x = 2; break; // left (-X) -> rightmost in adjacent cube
        case 1: subcubePos.x = 0; break; // right (+X) -> leftmost in adjacent cube
        case 2: subcubePos.y = 2; break; // bottom (-Y) -> topmost in adjacent cube
        case 3: subcubePos.y = 0; break; // top (+Y) -> bottommost in adjacent cube
        case 4: subcubePos.z = 2; break; // back (-Z) -> frontmost in adjacent cube
        case 5: subcubePos.z = 0; break; // front (+Z) -> backmost in adjacent cube
    }
    
    LOG_INFO_FMT("PlacementTool", "[SUBCUBE CALC] Final subcubePos=(" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
    
    return subcubePos;
}

PlacementTool::PlacementTool(VoxelManipulationSystem* manipulator)
    : m_manipulator(manipulator) {}

bool PlacementTool::placeVoxel(const InteractionContext& context) {
    if (!context.isValid()) {
        LOG_DEBUG("PlacementTool", "[PLACE VOXEL] No voxel is currently hovered");
        return false;
    }

    LOG_INFO_FMT("PlacementTool", "[PLACE VOXEL] Input: worldPos=(" 
              << context.hoveredLocation.worldPos.x << "," 
              << context.hoveredLocation.worldPos.y << "," 
              << context.hoveredLocation.worldPos.z << ") hitFace=" 
              << context.hoveredLocation.hitFace);

    // Calculate placement position based on hit face
    glm::ivec3 placementPos = context.hoveredLocation.getAdjacentPlacementPosition();
    
    LOG_INFO_FMT("PlacementTool", "[PLACE VOXEL] Output: placementPos=(" 
              << placementPos.x << "," << placementPos.y << "," << placementPos.z << ")");
    
    // Get default placement color (greenish)
    glm::vec3 color(0.2f, 0.7f, 0.2f);
    
    // Place the cube
    bool success = m_manipulator->placeCube(placementPos, color);
    
    if (success) {
        LOG_INFO_FMT("PlacementTool", "[PLACE VOXEL] Placed cube at (" 
                  << placementPos.x << "," << placementPos.y << "," << placementPos.z << ")");
    }
    return success;
}

PlacementTool::PlacementResult PlacementTool::calculatePlacement(const glm::vec3& hitPoint, const glm::vec3& hitNormal, const glm::ivec3& hitCubePos) {
    // Use a small epsilon to push the point into the target voxel
    glm::vec3 targetPoint = hitPoint + hitNormal * 0.001f;
    
    // Calculate the cube containing the target point
    glm::ivec3 cubePos = glm::ivec3(glm::floor(targetPoint));
    
    // Calculate local position within that cube
    glm::vec3 localPoint = targetPoint - glm::vec3(cubePos);
    
    // Calculate subcube index (0-2)
    glm::ivec3 subcubePos;
    subcubePos.x = glm::clamp(static_cast<int>(localPoint.x * 3.0f), 0, 2);
    subcubePos.y = glm::clamp(static_cast<int>(localPoint.y * 3.0f), 0, 2);
    subcubePos.z = glm::clamp(static_cast<int>(localPoint.z * 3.0f), 0, 2);
    
    return {cubePos, subcubePos};
}

bool PlacementTool::placeSubcube(const InteractionContext& context) {
    if (!context.isValid()) {
        LOG_DEBUG("PlacementTool", "[PLACE SUBCUBE] No voxel is currently hovered");
        return false;
    }
    
    // Use robust placement calculation
    PlacementResult result = calculatePlacement(
        context.hoveredLocation.hitPoint,
        context.hoveredLocation.hitNormal,
        context.hoveredLocation.worldPos
    );
    
    LOG_INFO_FMT("PlacementTool", "[PLACE SUBCUBE] Placed subcube at cube (" 
              << result.cubePos.x << "," << result.cubePos.y << "," << result.cubePos.z 
              << ") subcube (" << result.subcubePos.x << "," << result.subcubePos.y << "," << result.subcubePos.z << ")");
    
    glm::vec3 color(0.2f, 0.7f, 0.2f);
    return m_manipulator->placeSubcube(result.cubePos, result.subcubePos, color);
}

bool PlacementTool::placeMicrocube(const InteractionContext& context) {
    if (!context.isValid()) {
        LOG_DEBUG("PlacementTool", "[PLACE MICROCUBE] No voxel is currently hovered");
        return false;
    }
    
    // Use robust placement calculation for subcube level
    PlacementResult result = calculatePlacement(
        context.hoveredLocation.hitPoint,
        context.hoveredLocation.hitNormal,
        context.hoveredLocation.worldPos
    );
    
    // For microcubes, we need one more level of precision
    // Calculate target point again
    glm::vec3 targetPoint = context.hoveredLocation.hitPoint + context.hoveredLocation.hitNormal * 0.001f;
    glm::vec3 localPoint = targetPoint - glm::vec3(result.cubePos);
    
    // Calculate position within subcube (0.0-1.0)
    glm::vec3 subcubeLocal = glm::fract(localPoint * 3.0f);
    
    glm::ivec3 microcubePos;
    microcubePos.x = glm::clamp(static_cast<int>(subcubeLocal.x * 3.0f), 0, 2);
    microcubePos.y = glm::clamp(static_cast<int>(subcubeLocal.y * 3.0f), 0, 2);
    microcubePos.z = glm::clamp(static_cast<int>(subcubeLocal.z * 3.0f), 0, 2);
    
    glm::vec3 color(0.2f, 0.7f, 0.2f);
    bool success = m_manipulator->placeMicrocube(result.cubePos, result.subcubePos, microcubePos, color);
    
    if (success) {
        LOG_INFO_FMT("PlacementTool", "[PLACE MICROCUBE] Placed microcube");
    }
    return success;
}

}
