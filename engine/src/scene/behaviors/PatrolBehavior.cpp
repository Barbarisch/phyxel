#include "scene/behaviors/PatrolBehavior.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Scene {

PatrolBehavior::PatrolBehavior(const std::vector<glm::vec3>& waypoints, float walkSpeed,
                               float waitTime)
    : m_waypoints(waypoints), m_walkSpeed(walkSpeed), m_waitTime(waitTime) {}

void PatrolBehavior::update(float dt, NPCContext& ctx) {
    if (m_waypoints.empty() || !ctx.self) return;

    if (m_waiting) {
        m_waitTimer += dt;
        if (m_waitTimer >= m_waitTime) {
            m_waiting = false;
            m_waitTimer = 0.0f;
            m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
        }
        return;
    }

    // Move toward current waypoint
    glm::vec3 pos = ctx.self->getPosition();
    glm::vec3 target = m_waypoints[m_currentWaypoint];
    glm::vec3 diff = target - pos;
    // Only consider XZ plane distance (ignore height difference for arrival)
    float distXZ = glm::length(glm::vec2(diff.x, diff.z));

    if (distXZ < ARRIVAL_THRESHOLD) {
        // Arrived at waypoint
        m_waiting = true;
        m_waitTimer = 0.0f;
        return;
    }

    // Normalize direction and move
    glm::vec3 direction = glm::normalize(diff);
    glm::vec3 newPos = pos + direction * m_walkSpeed * dt;
    ctx.self->setPosition(newPos);

    // Face movement direction
    if (glm::length(glm::vec2(direction.x, direction.z)) > 0.01f) {
        float yaw = glm::degrees(atan2(direction.x, direction.z));
        ctx.self->setRotation(glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)));
    }
}

void PatrolBehavior::onInteract(Entity* interactor) {
    LOG_INFO("PatrolBehavior", "NPC on patrol interacted with");
    // Pause patrol briefly on interaction (stop for one wait cycle)
    m_waiting = true;
    m_waitTimer = 0.0f;
}

void PatrolBehavior::onEvent(const std::string& eventType, const nlohmann::json& data) {
    // Could respond to "alarm" events by changing waypoints, etc.
}

void PatrolBehavior::setWaypoints(const std::vector<glm::vec3>& waypoints) {
    m_waypoints = waypoints;
    m_currentWaypoint = 0;
    m_waiting = false;
    m_waitTimer = 0.0f;
}

} // namespace Scene
} // namespace Phyxel
