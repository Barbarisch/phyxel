#include "scene/interaction/PlacementTool.h"
#include "scene/VoxelManipulationSystem.h"
#include "utils/Logger.h"

namespace VulkanCube {

PlacementTool::PlacementTool(VoxelManipulationSystem* manipulator)
    : m_manipulator(manipulator) {}

bool PlacementTool::placeVoxel(const InteractionContext& context) {
    if (!context.isValid()) {
        LOG_DEBUG("PlacementTool", "[PLACE VOXEL] No voxel is currently hovered");
        return false;
    }

    // Calculate placement position based on hit face
    glm::ivec3 placementPos = context.hoveredLocation.getAdjacentPlacementPosition();
    
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

bool PlacementTool::placeSubcube(const InteractionContext& context) {
    if (!context.isValid()) {
        LOG_DEBUG("PlacementTool", "[PLACE SUBCUBE] No voxel is currently hovered");
        return false;
    }
    
    glm::ivec3 cubePos = context.hoveredLocation.worldPos;
    glm::ivec3 subcubePos(1, 1, 1); // Default to center
    
    if (context.hoveredLocation.hitFace >= 0) {
        switch (context.hoveredLocation.hitFace) {
            case 0: subcubePos.x = 0; break; // left (-X face) -> leftmost subcube
            case 1: subcubePos.x = 2; break; // right (+X face) -> rightmost subcube
            case 2: subcubePos.y = 0; break; // bottom (-Y face) -> bottommost subcube
            case 3: subcubePos.y = 2; break; // top (+Y face) -> topmost subcube
            case 4: subcubePos.z = 0; break; // back (-Z face) -> backmost subcube
            case 5: subcubePos.z = 2; break; // front (+Z face) -> frontmost subcube
        }
    }
    
    glm::vec3 color(0.2f, 0.7f, 0.2f);
    bool success = m_manipulator->placeSubcube(cubePos, subcubePos, color);
    
    if (success) {
        LOG_INFO_FMT("PlacementTool", "[PLACE SUBCUBE] Placed subcube at cube (" 
                  << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
    }
    return success;
}

bool PlacementTool::placeMicrocube(const InteractionContext& context) {
    if (!context.isValid()) {
        LOG_DEBUG("PlacementTool", "[PLACE MICROCUBE] No voxel is currently hovered");
        return false;
    }
    
    glm::ivec3 cubePos = context.hoveredLocation.worldPos;
    glm::ivec3 subcubePos(1, 1, 1);
    glm::ivec3 microcubePos(1, 1, 1);
    
    if (context.hoveredLocation.hitFace >= 0) {
        switch (context.hoveredLocation.hitFace) {
            case 0: subcubePos.x = 2; microcubePos.x = 2; break; // +X
            case 1: subcubePos.x = 0; microcubePos.x = 0; break; // -X
            case 2: subcubePos.y = 2; microcubePos.y = 2; break; // +Y
            case 3: subcubePos.y = 0; microcubePos.y = 0; break; // -Y
            case 4: subcubePos.z = 2; microcubePos.z = 2; break; // +Z
            case 5: subcubePos.z = 0; microcubePos.z = 0; break; // -Z
        }
    }
    
    glm::vec3 color(0.2f, 0.7f, 0.2f);
    bool success = m_manipulator->placeMicrocube(cubePos, subcubePos, microcubePos, color);
    
    if (success) {
        LOG_INFO_FMT("PlacementTool", "[PLACE MICROCUBE] Placed microcube");
    }
    return success;
}

}
