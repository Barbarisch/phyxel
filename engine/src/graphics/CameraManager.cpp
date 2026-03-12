#include "graphics/CameraManager.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Phyxel {
namespace Graphics {

// ============================================================================
// CameraTransition
// ============================================================================

void CameraTransition::start(const CameraSlot& from, const CameraSlot& to,
                             float durationSec, EaseType ease) {
    from_ = from;
    to_ = to;
    duration_ = durationSec;
    elapsed_ = 0.0f;
    easeType_ = ease;
    active_ = true;
}

bool CameraTransition::update(float dt, Camera& camera) {
    if (!active_) return false;

    elapsed_ += dt;
    float t = (duration_ > 0.0f) ? std::min(elapsed_ / duration_, 1.0f) : 1.0f;
    float eased = ease(t);

    // Lerp position
    glm::vec3 pos = glm::mix(from_.position, to_.position, eased);
    camera.setPosition(pos);

    // Lerp yaw/pitch (handle angle wrapping)
    float yawDiff = to_.yaw - from_.yaw;
    // Wrap to [-180, 180]
    while (yawDiff > 180.0f) yawDiff -= 360.0f;
    while (yawDiff < -180.0f) yawDiff += 360.0f;
    float yaw = from_.yaw + yawDiff * eased;
    float pitch = from_.pitch + (to_.pitch - from_.pitch) * eased;
    camera.setYaw(yaw);
    camera.setPitch(pitch);

    if (t >= 1.0f) {
        camera.setMode(to_.mode);
        active_ = false;
        return false; // done
    }
    return true; // still in progress
}

float CameraTransition::ease(float t) const {
    switch (easeType_) {
        case EaseType::Linear:
            return t;
        case EaseType::EaseInOut:
            // Cubic ease-in-out
            return t < 0.5f
                ? 4.0f * t * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }
    return t;
}

// ============================================================================
// CameraPath
// ============================================================================

void CameraPath::addWaypoint(const CameraWaypoint& wp) {
    waypoints_.push_back(wp);
}

void CameraPath::clearWaypoints() {
    waypoints_.clear();
    stop();
}

void CameraPath::play() {
    if (waypoints_.size() < 2) return;
    playing_ = true;
    paused_ = false;
    finished_ = false;
    currentSegment_ = 0;
    segmentTime_ = 0.0f;
    dwellTime_ = 0.0f;
    dwelling_ = false;
}

void CameraPath::pause() {
    paused_ = true;
}

void CameraPath::stop() {
    playing_ = false;
    paused_ = false;
    finished_ = false;
    currentSegment_ = 0;
    segmentTime_ = 0.0f;
    dwellTime_ = 0.0f;
    dwelling_ = false;
}

bool CameraPath::update(float dt, Camera& camera) {
    if (!playing_ || paused_ || finished_ || waypoints_.size() < 2) return false;

    // Handle dwell at waypoint
    if (dwelling_) {
        dwellTime_ -= dt;
        if (dwellTime_ > 0.0f) return true;
        dwelling_ = false;
        currentSegment_++;
        segmentTime_ = 0.0f;

        if (currentSegment_ >= waypoints_.size() - 1) {
            if (looping_) {
                currentSegment_ = 0;
            } else {
                finished_ = true;
                playing_ = false;
                return false;
            }
        }
    }

    // Travel speed: 1 second per segment (configurable via waypoint.dwellTime pacing)
    float segmentDuration = 1.0f;
    segmentTime_ += dt;
    float t = std::min(segmentTime_ / segmentDuration, 1.0f);

    size_t n = waypoints_.size();
    size_t i1 = currentSegment_;
    size_t i2 = currentSegment_ + 1;
    size_t i0 = (i1 > 0) ? i1 - 1 : i1;
    size_t i3 = std::min(i2 + 1, n - 1);

    glm::vec3 pos = catmullRom(waypoints_[i0].position, waypoints_[i1].position,
                               waypoints_[i2].position, waypoints_[i3].position, t);
    float yaw = lerpAngle(waypoints_[i1].yaw, waypoints_[i2].yaw, t);
    float pitch = waypoints_[i1].pitch + (waypoints_[i2].pitch - waypoints_[i1].pitch) * t;

    camera.setPosition(pos);
    camera.setYaw(yaw);
    camera.setPitch(pitch);

    if (t >= 1.0f) {
        // Arrived at waypoint — start dwelling
        dwelling_ = true;
        dwellTime_ = waypoints_[i2].dwellTime;
    }

    return true;
}

glm::vec3 CameraPath::catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                  const glm::vec3& p2, const glm::vec3& p3, float t) const {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float CameraPath::lerpAngle(float a, float b, float t) const {
    float diff = b - a;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return a + diff * t;
}

// ============================================================================
// CameraManager
// ============================================================================

CameraManager::CameraManager(Camera* camera)
    : camera_(camera) {
}

int CameraManager::createSlot(const CameraSlot& slot) {
    // Check for duplicate name
    if (findSlotIndex(slot.name) >= 0) {
        LOG_WARN("CameraManager", "Slot name '{}' already exists", slot.name);
        return -1;
    }
    slots_.push_back(slot);
    LOG_DEBUG("CameraManager", "Created camera slot '{}' at ({:.1f}, {:.1f}, {:.1f})",
              slot.name, slot.position.x, slot.position.y, slot.position.z);
    return static_cast<int>(slots_.size() - 1);
}

int CameraManager::createSlot(const std::string& name, const glm::vec3& position,
                               float yaw, float pitch, CameraMode mode) {
    CameraSlot slot;
    slot.name = name;
    slot.position = position;
    slot.yaw = yaw;
    slot.pitch = pitch;
    slot.mode = mode;
    return createSlot(slot);
}

bool CameraManager::removeSlot(const std::string& name) {
    int idx = findSlotIndex(name);
    if (idx < 0) return false;

    slots_.erase(slots_.begin() + idx);

    // Adjust active slot index
    if (activeSlotName_ == name) {
        activeSlotName_.clear();
        activeSlotIndex_ = -1;
    } else if (activeSlotIndex_ > idx) {
        activeSlotIndex_--;
    }
    return true;
}

bool CameraManager::updateSlot(const std::string& name, const CameraSlot& slot) {
    int idx = findSlotIndex(name);
    if (idx < 0) return false;
    slots_[idx] = slot;
    slots_[idx].name = name; // Preserve name
    return true;
}

const CameraSlot* CameraManager::getSlot(const std::string& name) const {
    int idx = findSlotIndex(name);
    if (idx < 0) return nullptr;
    return &slots_[idx];
}

bool CameraManager::setActiveSlot(const std::string& name) {
    int idx = findSlotIndex(name);
    if (idx < 0) return false;

    transition_.cancel();
    activeSlotName_ = name;
    activeSlotIndex_ = idx;
    applyCameraSlot(slots_[idx]);
    LOG_DEBUG("CameraManager", "Snapped to camera slot '{}'", name);
    return true;
}

bool CameraManager::transitionToSlot(const std::string& name, float durationSec,
                                      CameraTransition::EaseType ease) {
    int idx = findSlotIndex(name);
    if (idx < 0) return false;

    // Capture current state as the "from"
    CameraSlot from = captureCurrentState("_transition_from");
    transition_.start(from, slots_[idx], durationSec, ease);
    activeSlotName_ = name;
    activeSlotIndex_ = idx;
    LOG_DEBUG("CameraManager", "Transitioning to camera slot '{}' over {:.1f}s", name, durationSec);
    return true;
}

bool CameraManager::cycleSlot(float transitionDuration) {
    if (slots_.empty()) return false;

    int nextIdx = (activeSlotIndex_ + 1) % static_cast<int>(slots_.size());
    const std::string& name = slots_[nextIdx].name;

    if (transitionDuration > 0.0f) {
        return transitionToSlot(name, transitionDuration);
    }
    return setActiveSlot(name);
}

bool CameraManager::cyclePrevSlot(float transitionDuration) {
    if (slots_.empty()) return false;

    int prevIdx = activeSlotIndex_ - 1;
    if (prevIdx < 0) prevIdx = static_cast<int>(slots_.size()) - 1;
    const std::string& name = slots_[prevIdx].name;

    if (transitionDuration > 0.0f) {
        return transitionToSlot(name, transitionDuration);
    }
    return setActiveSlot(name);
}

bool CameraManager::followEntity(const std::string& slotName, const std::string& entityId,
                                  float distance, float height) {
    int idx = findSlotIndex(slotName);
    if (idx < 0) return false;
    slots_[idx].followEntityId = entityId;
    slots_[idx].followDistance = distance;
    slots_[idx].followHeight = height;
    return true;
}

bool CameraManager::unfollowEntity(const std::string& slotName) {
    int idx = findSlotIndex(slotName);
    if (idx < 0) return false;
    slots_[idx].followEntityId.clear();
    return true;
}

void CameraManager::update(float deltaTime) {
    if (!camera_) return;

    // Cinematic path has highest priority
    if (path_.isPlaying()) {
        path_.update(deltaTime, *camera_);
        return;
    }

    // Transition in progress
    if (transition_.isActive()) {
        transition_.update(deltaTime, *camera_);
        return;
    }

    // Entity follow for active slot
    if (activeSlotIndex_ >= 0 && activeSlotIndex_ < static_cast<int>(slots_.size())) {
        const auto& slot = slots_[activeSlotIndex_];
        if (!slot.followEntityId.empty() && entityLookup_) {
            auto pos = entityLookup_(slot.followEntityId);
            if (pos.has_value()) {
                camera_->setDistanceFromTarget(slot.followDistance);
                camera_->updatePositionFromTarget(pos.value(), slot.followHeight);
            }
        }
    }
}

CameraSlot CameraManager::captureCurrentState(const std::string& name) const {
    CameraSlot slot;
    slot.name = name;
    if (camera_) {
        slot.position = camera_->getPosition();
        slot.yaw = camera_->getYaw();
        slot.pitch = camera_->getPitch();
        slot.mode = camera_->getMode();
    }
    return slot;
}

int CameraManager::findSlotIndex(const std::string& name) const {
    for (size_t i = 0; i < slots_.size(); i++) {
        if (slots_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void CameraManager::applyCameraSlot(const CameraSlot& slot) {
    if (!camera_) return;
    camera_->setPosition(slot.position);
    camera_->setYaw(slot.yaw);
    camera_->setPitch(slot.pitch);
    camera_->setMode(slot.mode);
}

} // namespace Graphics
} // namespace Phyxel
