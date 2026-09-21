#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel::Scene::Motion {

enum class MotionProvenance {
    Clip,
    MotionBricks,
    Fallback,
    Test
};

struct MotionIntent {
    glm::vec3 movementDirection{0.0f};
    glm::vec3 facingDirection{0.0f, 0.0f, 1.0f};
    float targetSpeed = 0.0f;
    bool hasWorldTarget = false;
    glm::vec3 worldTarget{0.0f};
    float targetHeadingRadians = 0.0f;
    std::string styleKey;
    std::uint64_t seed = 0;

    bool operator==(const MotionIntent& rhs) const;
};

struct LocalPoseFrame {
    double timeSeconds = 0.0;
    glm::vec3 rootTranslation{0.0f};
    std::vector<std::string> jointNames;
    std::vector<glm::quat> localRotations;
    MotionProvenance provenance = MotionProvenance::Fallback;
    bool discontinuity = false;

    bool structurallyValid() const;
};

enum class MotionSourceState {
    Disabled,
    Ready,
    Degraded,
    Error
};

struct MotionSourceStatus {
    std::string requestedProvider = "clips";
    std::string effectiveProvider = "clips";
    MotionSourceState state = MotionSourceState::Disabled;
    std::string backend;
    double queueAgeSeconds = 0.0;
    double bufferedSeconds = 0.0;
    double lastPlanLatencySeconds = 0.0;
    double planLatencyP50Seconds = 0.0;
    double planLatencyP95Seconds = 0.0;
    double planLatencyP99Seconds = 0.0;
    std::uint64_t completedPlans = 0;
    std::uint64_t underruns = 0;
    std::string error;
};

} // namespace Phyxel::Scene::Motion
