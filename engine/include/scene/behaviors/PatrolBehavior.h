#pragma once

#include "scene/NPCBehavior.h"
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Scene {

/// NPC walks between a list of waypoints in order.
/// Direct-line movement (no pathfinding). Pauses at each waypoint.
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

private:
    std::vector<glm::vec3> m_waypoints;
    size_t m_currentWaypoint = 0;
    float m_walkSpeed;
    float m_waitTime;
    float m_waitTimer = 0.0f;
    bool m_waiting = false;
    static constexpr float ARRIVAL_THRESHOLD = 0.5f;

    // Ambient speech bubble config
    std::vector<std::string> m_arrivalPhrases;
    float m_speechChance = 0.3f; // 30% chance to speak at waypoint
};

} // namespace Scene
} // namespace Phyxel
