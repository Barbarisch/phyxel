#pragma once

#include <cstddef>
#include <vector>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel::Scene::Motion {

struct OracleFrame {
    std::vector<glm::quat> localRotations;
    std::vector<glm::vec3> worldJointPositions;
    glm::vec3 generatedRootVelocity{0.0f};
    glm::vec3 capsuleVelocity{0.0f};
    std::vector<bool> plantedJoints;
};

struct MotionOracleMetrics {
    bool valid = false;
    float maxPoseDeltaRadians = 0.0f;
    float maxAngularVelocity = 0.0f;
    float maxAngularAcceleration = 0.0f;
    float maxRootVelocityError = 0.0f;
    float maxPlantedJointSpeed = 0.0f;
    float maxChainLengthError = 0.0f;
};

/// Provider-neutral animation quality metrics. chainEdges contains parent/child
/// joint indices; the first frame is the reference chain length.
MotionOracleMetrics evaluateMotion(const std::vector<OracleFrame>& frames,
                                   float secondsPerFrame,
                                   const std::vector<std::pair<std::size_t,
                                                               std::size_t>>& chainEdges = {});

} // namespace Phyxel::Scene::Motion
