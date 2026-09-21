#include "scene/motion/MotionSource.h"

#include <cmath>
#include <utility>

namespace Phyxel::Scene::Motion {

namespace {
bool equalVec3(const glm::vec3& a, const glm::vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
}

bool MotionIntent::operator==(const MotionIntent& rhs) const {
    return equalVec3(movementDirection, rhs.movementDirection) &&
           equalVec3(facingDirection, rhs.facingDirection) &&
           targetSpeed == rhs.targetSpeed &&
           hasWorldTarget == rhs.hasWorldTarget &&
           equalVec3(worldTarget, rhs.worldTarget) &&
           targetHeadingRadians == rhs.targetHeadingRadians &&
           styleKey == rhs.styleKey && seed == rhs.seed;
}

bool LocalPoseFrame::structurallyValid() const {
    if (!std::isfinite(timeSeconds) ||
        !std::isfinite(rootTranslation.x) ||
        !std::isfinite(rootTranslation.y) ||
        !std::isfinite(rootTranslation.z) ||
        jointNames.empty() || jointNames.size() != localRotations.size()) {
        return false;
    }
    for (const glm::quat& q : localRotations) {
        const float length2 = glm::dot(q, q);
        if (!std::isfinite(q.w) || !std::isfinite(q.x) ||
            !std::isfinite(q.y) || !std::isfinite(q.z) ||
            !std::isfinite(length2) || length2 < 1.0e-12f) {
            return false;
        }
    }
    return true;
}

DeterministicMotionSource::DeterministicMotionSource(
    std::vector<std::string> jointNames,
    std::vector<glm::quat> localRotations)
    : m_jointNames(std::move(jointNames)),
      m_localRotations(std::move(localRotations)) {}

void DeterministicMotionSource::submitIntent(const MotionIntent& intent) {
    m_intent = intent;
}

bool DeterministicMotionSource::sample(double timeSeconds, LocalPoseFrame& output) {
    if (!m_available || m_jointNames.empty() ||
        m_jointNames.size() != m_localRotations.size()) {
        return false;
    }
    output.timeSeconds = timeSeconds;
    output.rootTranslation = glm::vec3(0.0f);
    output.jointNames = m_jointNames;
    output.localRotations = m_localRotations;
    output.provenance = MotionProvenance::Test;
    output.discontinuity = false;
    return output.structurallyValid();
}

void DeterministicMotionSource::reset() {
    m_intent = MotionIntent{};
}

MotionSourceStatus DeterministicMotionSource::status() const {
    MotionSourceStatus result;
    result.requestedProvider = "deterministic_test";
    result.effectiveProvider = m_available ? "deterministic_test" : "clips";
    result.state = m_available ? MotionSourceState::Ready : MotionSourceState::Degraded;
    if (!m_available) result.error = "deterministic source unavailable";
    return result;
}

LocalPoseFrame providerOrFallback(const LocalPoseFrame* providerFrame,
                                  const LocalPoseFrame& clipFrame) {
    if (providerFrame && providerFrame->structurallyValid()) return *providerFrame;
    LocalPoseFrame result = clipFrame;
    result.provenance = MotionProvenance::Fallback;
    return result;
}

} // namespace Phyxel::Scene::Motion

