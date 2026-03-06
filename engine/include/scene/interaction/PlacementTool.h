#pragma once

#include "InteractionContext.h"
#include <memory>

namespace Phyxel {

class VoxelManipulationSystem;

class PlacementTool {
public:
    PlacementTool(VoxelManipulationSystem* manipulator);
    ~PlacementTool() = default;

    bool placeVoxel(const InteractionContext& context);
    bool placeSubcube(const InteractionContext& context);
    bool placeMicrocube(const InteractionContext& context);

    // Helper to calculate subcube position from hit point
    static glm::ivec3 calculateSubcubePositionFromHitPoint(const glm::vec3& hitPoint, const glm::ivec3& cubeWorldPos, int hitFace);

    struct PlacementResult {
        glm::ivec3 cubePos;
        glm::ivec3 subcubePos;
    };

    // New robust placement calculation using epsilon shift
    static PlacementResult calculatePlacement(const glm::vec3& hitPoint, const glm::vec3& hitNormal, const glm::ivec3& hitCubePos);

private:
    VoxelManipulationSystem* m_manipulator;
};

}
