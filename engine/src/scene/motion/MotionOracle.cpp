#include "scene/motion/MotionOracle.h"

#include "scene/motion/HumanoidRetargeter.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Scene::Motion {
namespace {
float rotationDelta(const glm::quat& a, const glm::quat& b) {
    const float dot = std::clamp(std::abs(glm::dot(glm::normalize(a), glm::normalize(b))),
                                 0.0f, 1.0f);
    return 2.0f * std::acos(dot);
}
bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

MotionOracleMetrics evaluateMotion(
    const std::vector<OracleFrame>& frames, float secondsPerFrame,
    const std::vector<std::pair<std::size_t, std::size_t>>& chainEdges) {
    MotionOracleMetrics result;
    if (frames.size() < 2 || !std::isfinite(secondsPerFrame) || secondsPerFrame <= 0.0f)
        return result;
    const std::size_t joints = frames.front().localRotations.size();
    if (joints == 0) return result;
    std::vector<float> previousVelocity(joints, 0.0f);
    std::vector<float> referenceLengths;
    for (const auto [parent, child] : chainEdges) {
        if (parent >= frames.front().worldJointPositions.size() ||
            child >= frames.front().worldJointPositions.size()) return result;
        referenceLengths.push_back(glm::distance(frames.front().worldJointPositions[parent],
                                                 frames.front().worldJointPositions[child]));
    }
    for (std::size_t f = 0; f < frames.size(); ++f) {
        const auto& frame = frames[f];
        if (frame.localRotations.size() != joints || !finite(frame.generatedRootVelocity) ||
            !finite(frame.capsuleVelocity)) return result;
        for (const auto& rotation : frame.localRotations)
            if (!isFiniteQuaternion(rotation) || glm::dot(rotation, rotation) < 1.0e-12f) return result;
        result.maxRootVelocityError = std::max(result.maxRootVelocityError,
            glm::length(frame.generatedRootVelocity - frame.capsuleVelocity));
        for (std::size_t edge = 0; edge < chainEdges.size(); ++edge) {
            const auto [parent, child] = chainEdges[edge];
            if (parent >= frame.worldJointPositions.size() || child >= frame.worldJointPositions.size())
                return result;
            result.maxChainLengthError = std::max(result.maxChainLengthError,
                std::abs(glm::distance(frame.worldJointPositions[parent],
                                       frame.worldJointPositions[child]) - referenceLengths[edge]));
        }
        if (f == 0) continue;
        for (std::size_t joint = 0; joint < joints; ++joint) {
            const float delta = rotationDelta(frames[f - 1].localRotations[joint],
                                              frame.localRotations[joint]);
            const float velocity = delta / secondsPerFrame;
            result.maxPoseDeltaRadians = std::max(result.maxPoseDeltaRadians, delta);
            result.maxAngularVelocity = std::max(result.maxAngularVelocity, velocity);
            if (f > 1) result.maxAngularAcceleration = std::max(result.maxAngularAcceleration,
                std::abs(velocity - previousVelocity[joint]) / secondsPerFrame);
            previousVelocity[joint] = velocity;
        }
        const std::size_t planted = std::min(frame.plantedJoints.size(), frame.worldJointPositions.size());
        for (std::size_t joint = 0; joint < planted; ++joint) {
            if (frame.plantedJoints[joint] && joint < frames[f - 1].worldJointPositions.size())
                result.maxPlantedJointSpeed = std::max(result.maxPlantedJointSpeed,
                    glm::distance(frame.worldJointPositions[joint],
                                  frames[f - 1].worldJointPositions[joint]) / secondsPerFrame);
        }
    }
    result.valid = true;
    return result;
}

} // namespace Phyxel::Scene::Motion
