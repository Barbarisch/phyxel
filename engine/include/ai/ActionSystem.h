#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Scene { class Entity; }
namespace Core { class EntityRegistry; class AStarPathfinder; }
namespace UI { class SpeechBubbleManager; }

namespace AI {

class Blackboard;

// ============================================================================
// Action status returned by update()
// ============================================================================
enum class ActionStatus {
    Running,
    Success,
    Failure
};

// ============================================================================
// ActionContext — systems available to actions (subset of NPCContext)
// ============================================================================
struct ActionContext {
    Scene::Entity* self = nullptr;
    std::string selfId;
    Core::EntityRegistry* entityRegistry = nullptr;
    UI::SpeechBubbleManager* speechBubbleManager = nullptr;
    Blackboard* blackboard = nullptr;
};

// ============================================================================
// NPCAction — base class for all NPC actions
// ============================================================================
class NPCAction {
public:
    virtual ~NPCAction() = default;

    virtual void start(ActionContext& ctx) { (void)ctx; }
    virtual ActionStatus update(float dt, ActionContext& ctx) = 0;
    virtual void abort(ActionContext& ctx) { (void)ctx; }

    const std::string& getName() const { return m_name; }

protected:
    std::string m_name = "Action";
};

// ============================================================================
// Concrete Actions
// ============================================================================

/// Walk toward a position. Succeeds when within threshold.
/// Uses A* pathfinding when a pathfinder is set, otherwise direct-line movement.
class MoveToAction : public NPCAction {
public:
    MoveToAction(const glm::vec3& target, float speed = 2.0f, float threshold = 0.5f)
        : m_target(target), m_speed(speed), m_threshold(threshold) { m_name = "MoveTo"; }

    /// Set pathfinder for obstacle-aware navigation (may be null for direct-line fallback).
    void setPathfinder(Core::AStarPathfinder* pathfinder) { m_pathfinder = pathfinder; }

    void start(ActionContext& ctx) override;
    ActionStatus update(float dt, ActionContext& ctx) override;
    void abort(ActionContext& ctx) override;

private:
    glm::vec3 m_target;
    float m_speed;
    float m_threshold;
    Core::AStarPathfinder* m_pathfinder = nullptr;
    std::vector<glm::vec3> m_pathNodes;
    size_t m_currentPathNode = 0;
    bool m_pathComputed = false;
};

/// Face a target position. Succeeds immediately after rotating.
class LookAtAction : public NPCAction {
public:
    explicit LookAtAction(const glm::vec3& target) : m_target(target) { m_name = "LookAt"; }

    ActionStatus update(float dt, ActionContext& ctx) override;

private:
    glm::vec3 m_target;
};

/// Wait for a duration. Succeeds when time elapses.
class WaitAction : public NPCAction {
public:
    explicit WaitAction(float duration) : m_duration(duration) { m_name = "Wait"; }

    void start(ActionContext& ctx) override { m_elapsed = 0.0f; }
    ActionStatus update(float dt, ActionContext& ctx) override;

private:
    float m_duration;
    float m_elapsed = 0.0f;
};

/// Show a speech bubble. Succeeds immediately.
class SpeakAction : public NPCAction {
public:
    SpeakAction(const std::string& text, float duration = 3.0f)
        : m_text(text), m_duration(duration) { m_name = "Speak"; }

    ActionStatus update(float dt, ActionContext& ctx) override;

private:
    std::string m_text;
    float m_duration;
};

/// Set a blackboard value. Succeeds immediately.
class SetBlackboardAction : public NPCAction {
public:
    SetBlackboardAction(const std::string& key, const std::string& value)
        : m_key(key), m_value(value) { m_name = "SetBlackboard"; }

    ActionStatus update(float dt, ActionContext& ctx) override;

private:
    std::string m_key;
    std::string m_value;
};

/// Move toward the entity ID stored in a blackboard key. Succeeds when close.
class MoveToEntityAction : public NPCAction {
public:
    MoveToEntityAction(const std::string& bbKey = "target", float speed = 2.0f, float threshold = 1.5f)
        : m_bbKey(bbKey), m_speed(speed), m_threshold(threshold) { m_name = "MoveToEntity"; }

    ActionStatus update(float dt, ActionContext& ctx) override;

private:
    std::string m_bbKey;
    float m_speed;
    float m_threshold;
};

/// Flee away from the entity stored in a blackboard key. Runs for duration seconds.
class FleeAction : public NPCAction {
public:
    FleeAction(const std::string& bbKey = "threat", float speed = 4.0f, float duration = 3.0f)
        : m_bbKey(bbKey), m_speed(speed), m_duration(duration) { m_name = "Flee"; }

    void start(ActionContext& ctx) override { m_elapsed = 0.0f; }
    ActionStatus update(float dt, ActionContext& ctx) override;

private:
    std::string m_bbKey;
    float m_speed;
    float m_duration;
    float m_elapsed = 0.0f;
};

} // namespace AI
} // namespace Phyxel
