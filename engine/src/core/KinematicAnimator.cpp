#include "core/KinematicAnimator.h"
#include "core/KinematicVoxelManager.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace Core {

namespace {

constexpr float kSettleEpsilon = 1e-4f;

glm::vec3 axisVec(KinematicAnimator::Axis a) {
    switch (a) {
        case KinematicAnimator::Axis::X: return {1, 0, 0};
        case KinematicAnimator::Axis::Y: return {0, 1, 0};
        case KinematicAnimator::Axis::Z: return {0, 0, 1};
    }
    return {0, 1, 0};
}

// Smoothstep-style ease for the [0,1] progress fraction.
float applyEasing(float t, KinematicAnimator::Easing e) {
    if (e == KinematicAnimator::Easing::Linear) return t;
    // EaseInOut: 3t² − 2t³
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void KinematicAnimator::registerPart(const PartConfig& cfg) {
    PartState st;
    st.cfg = cfg;
    st.currentAngle = st.targetAngle = 0.0f;
    st.currentOffset = st.targetOffset = 0.0f;
    st.settled = true;
    m_parts[cfg.kinematicId] = std::move(st);

    // Push the initial (zero-track) transform so the voxel manager doesn't
    // render an identity matrix until the first animation tick.
    if (m_voxelManager) {
        m_voxelManager->setTransform(cfg.kinematicId, composeTransform(cfg, 0.0f, 0.0f));
    }
}

void KinematicAnimator::unregisterPart(const std::string& kinematicId) {
    m_parts.erase(kinematicId);
}

void KinematicAnimator::setTargetAngle(const std::string& kinematicId,
                                       float angleRad,
                                       float speedRadPerSec)
{
    auto it = m_parts.find(kinematicId);
    if (it == m_parts.end()) return;
    auto& st = it->second;
    st.targetAngle = angleRad;
    st.angleSpeed  = speedRadPerSec;
    if (std::abs(st.currentAngle - angleRad) > kSettleEpsilon) {
        st.settled = false;
    }
    if (speedRadPerSec <= 0.0f) {
        st.currentAngle = angleRad;  // snap
        st.settled = std::abs(st.currentOffset - st.targetOffset) <= kSettleEpsilon;
    }
}

void KinematicAnimator::setTargetOffset(const std::string& kinematicId,
                                        float offsetMeters,
                                        float speedMetersPerSec)
{
    auto it = m_parts.find(kinematicId);
    if (it == m_parts.end()) return;
    auto& st = it->second;
    st.targetOffset = offsetMeters;
    st.offsetSpeed  = speedMetersPerSec;
    if (std::abs(st.currentOffset - offsetMeters) > kSettleEpsilon) {
        st.settled = false;
    }
    if (speedMetersPerSec <= 0.0f) {
        st.currentOffset = offsetMeters;  // snap
        st.settled = std::abs(st.currentAngle - st.targetAngle) <= kSettleEpsilon;
    }
}

bool KinematicAnimator::integrate(float& current, float target, float speed,
                                  float dt, Easing easing)
{
    if (speed <= 0.0f) {
        current = target;
        return true;
    }
    float delta = target - current;
    if (std::abs(delta) <= kSettleEpsilon) {
        current = target;
        return true;
    }
    // Easing is applied to the *step* magnitude relative to remaining distance,
    // so that as we approach the target the per-frame step shrinks. This gives
    // an ease-out shape without needing to remember the original start angle —
    // important when interruptions retarget the track mid-flight.
    float maxStep = speed * dt;
    float remaining = std::abs(delta);
    float progress = std::clamp(maxStep / std::max(remaining, 1e-6f), 0.0f, 1.0f);
    float step = remaining * applyEasing(progress, easing);
    step = std::min(step, remaining);
    if (delta < 0.0f) step = -step;
    current += step;
    if (std::abs(target - current) <= kSettleEpsilon) {
        current = target;
        return true;
    }
    return false;
}

void KinematicAnimator::update(float dt) {
    if (!m_voxelManager || dt <= 0.0f) return;

    for (auto& [id, st] : m_parts) {
        bool angleSettled  = integrate(st.currentAngle,  st.targetAngle,
                                       st.angleSpeed,  dt, st.cfg.easing);
        bool offsetSettled = integrate(st.currentOffset, st.targetOffset,
                                       st.offsetSpeed, dt, st.cfg.easing);

        // Always push the transform — currentAngle/Offset may have changed.
        m_voxelManager->setTransform(id,
            composeTransform(st.cfg, st.currentAngle, st.currentOffset));

        const bool nowSettled = angleSettled && offsetSettled;
        if (nowSettled && !st.settled) {
            st.settled = true;
            if (m_onSettle) {
                m_onSettle(id, st.currentAngle, st.currentOffset);
            }
        } else if (!nowSettled) {
            st.settled = false;
        }
    }
}

bool KinematicAnimator::isSettled(const std::string& kinematicId) const {
    auto it = m_parts.find(kinematicId);
    return it == m_parts.end() ? true : it->second.settled;
}

float KinematicAnimator::currentAngle(const std::string& kinematicId) const {
    auto it = m_parts.find(kinematicId);
    return it == m_parts.end() ? 0.0f : it->second.currentAngle;
}

float KinematicAnimator::currentOffset(const std::string& kinematicId) const {
    auto it = m_parts.find(kinematicId);
    return it == m_parts.end() ? 0.0f : it->second.currentOffset;
}

glm::mat4 KinematicAnimator::composeTransform(const PartConfig& cfg,
                                              float angleRad,
                                              float offsetMeters)
{
    // World transform layout (read right-to-left):
    //   translate(hingeWorld) * rotateY(baseRotation) * rotate(angle,axis) * translate(slide)
    // The translate(slide) is in *local* space so drawers slide out along the
    // object's local axis regardless of yaw — matches authoring intent.
    glm::mat4 m(1.0f);
    m = glm::translate(m, cfg.hingeWorld);
    m = glm::rotate(m, cfg.baseRotationRad, glm::vec3(0, 1, 0));
    m = glm::rotate(m, angleRad, axisVec(cfg.rotationAxis));
    if (offsetMeters != 0.0f && glm::dot(cfg.slideDirLocal, cfg.slideDirLocal) > 0.0f) {
        m = glm::translate(m, cfg.slideDirLocal * offsetMeters);
    }
    return m;
}

} // namespace Core
} // namespace Phyxel
