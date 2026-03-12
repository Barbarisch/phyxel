#pragma once

#include "Camera.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {

/// A named camera configuration that can be snapped to or transitioned into.
struct CameraSlot {
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = -90.0f;
    float pitch = 0.0f;
    CameraMode mode = CameraMode::Free;
    std::string followEntityId;         // Empty = no follow
    float followDistance = 5.0f;
    float followHeight = 1.5f;
};

/// A waypoint on a cinematic camera path.
struct CameraWaypoint {
    glm::vec3 position;
    float yaw;
    float pitch;
    float dwellTime = 0.0f;            // Seconds to pause at this waypoint
};

/// Smooth transition between two camera states.
class CameraTransition {
public:
    enum class EaseType { Linear, EaseInOut };

    void start(const CameraSlot& from, const CameraSlot& to,
               float durationSec, EaseType ease = EaseType::EaseInOut);
    bool update(float dt, Camera& camera);
    bool isActive() const { return active_; }
    void cancel() { active_ = false; }

private:
    float ease(float t) const;

    CameraSlot from_;
    CameraSlot to_;
    float duration_ = 0.0f;
    float elapsed_ = 0.0f;
    EaseType easeType_ = EaseType::EaseInOut;
    bool active_ = false;
};

/// An ordered list of waypoints that a camera can play through.
class CameraPath {
public:
    void addWaypoint(const CameraWaypoint& wp);
    void clearWaypoints();
    size_t waypointCount() const { return waypoints_.size(); }

    void play();
    void pause();
    void stop();
    bool update(float dt, Camera& camera);

    bool isPlaying() const { return playing_; }
    bool isFinished() const { return finished_; }
    void setLooping(bool loop) { looping_ = loop; }

private:
    glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                         const glm::vec3& p2, const glm::vec3& p3, float t) const;
    float lerpAngle(float a, float b, float t) const;

    std::vector<CameraWaypoint> waypoints_;
    size_t currentSegment_ = 0;
    float segmentTime_ = 0.0f;
    float dwellTime_ = 0.0f;
    bool playing_ = false;
    bool paused_ = false;
    bool finished_ = false;
    bool looping_ = false;
    bool dwelling_ = false;
};

/// Manages multiple named camera slots, transitions, and cinematic paths.
class CameraManager {
public:
    explicit CameraManager(Camera* camera);

    // --- Slot management ---
    int createSlot(const CameraSlot& slot);
    int createSlot(const std::string& name, const glm::vec3& position,
                   float yaw = -90.0f, float pitch = 0.0f,
                   CameraMode mode = CameraMode::Free);
    bool removeSlot(const std::string& name);
    bool updateSlot(const std::string& name, const CameraSlot& slot);
    const CameraSlot* getSlot(const std::string& name) const;
    const std::vector<CameraSlot>& getSlots() const { return slots_; }
    size_t slotCount() const { return slots_.size(); }

    // --- Active slot ---
    /// Snap camera to a named slot immediately.
    bool setActiveSlot(const std::string& name);
    /// Smoothly transition to a named slot.
    bool transitionToSlot(const std::string& name, float durationSec = 1.0f,
                          CameraTransition::EaseType ease = CameraTransition::EaseType::EaseInOut);
    /// Cycle to the next slot (wraps around). Snap by default, or transition.
    bool cycleSlot(float transitionDuration = 0.0f);
    /// Cycle to the previous slot.
    bool cyclePrevSlot(float transitionDuration = 0.0f);
    const std::string& getActiveSlotName() const { return activeSlotName_; }

    // --- Entity follow ---
    /// Attach a slot to follow an entity. Caller provides a position lookup callback.
    using EntityPositionFunc = std::function<std::optional<glm::vec3>(const std::string& entityId)>;
    void setEntityPositionLookup(EntityPositionFunc func) { entityLookup_ = std::move(func); }
    bool followEntity(const std::string& slotName, const std::string& entityId,
                      float distance = 5.0f, float height = 1.5f);
    bool unfollowEntity(const std::string& slotName);

    // --- Cinematic path ---
    CameraPath& getPath() { return path_; }
    const CameraPath& getPath() const { return path_; }

    // --- Per-frame update ---
    /// Call once per frame. Handles transitions, entity follow, and cinematic paths.
    void update(float deltaTime);

    // --- Save/restore current camera state ---
    /// Capture the current camera state into a slot (for "save position" functionality).
    CameraSlot captureCurrentState(const std::string& name) const;

    Camera* getCamera() const { return camera_; }

private:
    int findSlotIndex(const std::string& name) const;
    void applyCameraSlot(const CameraSlot& slot);

    Camera* camera_;
    std::vector<CameraSlot> slots_;
    std::string activeSlotName_;
    int activeSlotIndex_ = -1;

    CameraTransition transition_;
    CameraPath path_;
    EntityPositionFunc entityLookup_;
};

} // namespace Graphics
} // namespace Phyxel
