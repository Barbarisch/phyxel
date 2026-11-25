#pragma once

#include "InteractionContext.h"
#include <memory>

namespace Phyxel {

// Placement geometry (pure). The tool's placeVoxel, which wrote today's Chunk store, went with D4.
class PlacementTool {
public:
    // Helper to calculate subcube position from hit point
    static glm::ivec3 calculateSubcubePositionFromHitPoint(const glm::vec3& hitPoint, const glm::ivec3& cubeWorldPos, int hitFace);

    struct PlacementResult {
        glm::ivec3 cubePos;
        glm::ivec3 subcubePos;
    };

    // New robust placement calculation using epsilon shift
    static PlacementResult calculatePlacement(const glm::vec3& hitPoint, const glm::vec3& hitNormal, const glm::ivec3& hitCubePos);
};

}
