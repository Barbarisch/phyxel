#pragma once

#include "scene/NPCBehavior.h"
#include "ai/PerceptionSystem.h"
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core { class AStarPathfinder; }
namespace Scene {

/// NPC walks between a list of waypoints in order.
/// Uses A* pathfinding when available, falls back to direct-line movement.
/// Pauses at each waypoint. Includes perception system with FOV cone visualization.
class PatrolBehavior : public NPCBehavior {
public:
    /// @param waypoints  Ordered list of world positions to visit.
    /// @param walkSpeed  Movement speed in units/second.
    /// @param waitTime   Seconds to wait at each waypoint before advancing.
    PatrolBehavior(const std::vector<glm::vec3>& waypoints, float walkSpeed = 2.0f,
                   float waitTime = 2.0f);

    void update(float dt, NPCContext& ctx) override;
    void onInteract(Entity* interactor) override;
    void onEvent(const std::string& eventType, const nlohmann::json& data) override;
    std::string getBehaviorName() const override { return "Patrol"; }

    // Runtime modification
    void setWaypoints(const std::vector<glm::vec3>& waypoints);
    const std::vector<glm::vec3>& getWaypoints() const { return m_waypoints; }
    size_t getCurrentWaypointIndex() const { return m_currentWaypoint; }
    float getWalkSpeed() const { return m_walkSpeed; }
    void setWalkSpeed(float speed) { m_walkSpeed = speed; }

    /// Set phrases the NPC may say when arriving at a waypoint.
    void setArrivalPhrases(const std::vector<std::string>& phrases) { m_arrivalPhrases = phrases; }
    void setSpeechChance(float chance) { m_speechChance = chance; }

    /// Set pathfinder for obstacle-aware navigation (may be null for direct-line fallback).
    void setPathfinder(Core::AStarPathfinder* pathfinder) { m_pathfinder = pathfinder; }

    /// Invalidate the currently computed path, forcing a recompute on next update.
    void invalidatePath() { m_pathComputed = false; m_pathNodes.clear(); m_currentPathNode = 0; }

    /// Access perception for external queries (e.g. API)
    AI::PerceptionComponent& getPerception() { return m_perception; }
    const AI::PerceptionComponent& getPerception() const { return m_perception; }

private:
    void updatePerception(float dt, NPCContext& ctx, const glm::vec3& forward);
    void computePath(const glm::vec3& from, const glm::vec3& to);
    std::vector<glm::vec3> m_waypoints;
    size_t m_currentWaypoint = 0;
    float m_walkSpeed;
    float m_waitTime;
    float m_waitTimer = 0.0f;
    bool m_waiting = false;
    static constexpr float ARRIVAL_THRESHOLD = 0.5f;
    static constexpr float PATH_NODE_THRESHOLD = 0.3f; ///< Tighter threshold for path sub-waypoints

    // Pathfinding
    Core::AStarPathfinder* m_pathfinder = nullptr;
    std::vector<glm::vec3> m_pathNodes;   ///< Current computed sub-waypoints
    size_t m_currentPathNode = 0;          ///< Index into m_pathNodes
    bool m_pathComputed = false;           ///< True if we have a path for current waypoint
    float m_pathRetryTimer = 0.0f;         ///< Cooldown before retrying failed pathfind
    static constexpr float PATH_RETRY_DELAY = 2.0f;

    // Ambient speech bubble config
    std::vector<std::string> m_arrivalPhrases;
    float m_speechChance = 0.3f; // 30% chance to speak at waypoint

    // Perception (FOV + LOS)
    AI::PerceptionComponent m_perception;

    // Look-around sweep during waypoint waits
    float m_baseYaw = 0.0f;        // yaw when arriving at waypoint
    float m_lookAroundPhase = 0.0f; // sweep oscillator

    // Stuck detection (debug)
    glm::vec3 m_lastLoggedPos{0.0f};
    float m_stuckTimer = 0.0f;
};

} // namespace Scene
} // namespace Phyxel
