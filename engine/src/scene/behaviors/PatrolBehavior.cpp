#include "scene/behaviors/PatrolBehavior.h"
#include "scene/Entity.h"
#include "ui/SpeechBubbleManager.h"
#include "utils/Logger.h"
#include <random>

namespace Phyxel {
namespace Scene {

PatrolBehavior::PatrolBehavior(const std::vector<glm::vec3>& waypoints, float walkSpeed,
                               float waitTime)
    : m_waypoints(waypoints), m_walkSpeed(walkSpeed), m_waitTime(waitTime) {}

void PatrolBehavior::update(float dt, NPCContext& ctx) {
    if (m_waypoints.empty() || !ctx.self) return;

    if (m_waiting) {
        m_waitTimer += dt;
        // Stop movement while waiting
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
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
        // Arrived at waypoint — stop moving
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        m_waiting = true;
        m_waitTimer = 0.0f;

        // Ambient speech bubble
        if (!m_arrivalPhrases.empty() && ctx.speechBubbleManager && !ctx.selfId.empty()) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            if (roll(rng) < m_speechChance) {
                std::uniform_int_distribution<size_t> pick(0, m_arrivalPhrases.size() - 1);
                ctx.speechBubbleManager->say(ctx.selfId, m_arrivalPhrases[pick(rng)], 3.0f);
            }
        }
        return;
    }

    // Compute XZ direction toward waypoint and set velocity (gravity handles Y)
    glm::vec3 direction = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
    ctx.self->setMoveVelocity(direction * m_walkSpeed);

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
