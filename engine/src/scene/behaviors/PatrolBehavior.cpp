#include "scene/behaviors/PatrolBehavior.h"
#include "scene/Entity.h"
#include "core/EntityRegistry.h"
#include "core/ChunkManager.h"
#include "graphics/RaycastVisualizer.h"
#include "ui/SpeechBubbleManager.h"
#include "utils/Logger.h"
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <cmath>

namespace Phyxel {
namespace Scene {

PatrolBehavior::PatrolBehavior(const std::vector<glm::vec3>& waypoints, float walkSpeed,
                               float waitTime)
    : m_waypoints(waypoints), m_walkSpeed(walkSpeed), m_waitTime(waitTime) {}

void PatrolBehavior::update(float dt, NPCContext& ctx) {
    if (m_waypoints.empty() || !ctx.self) return;

    // Derive forward from entity rotation (model faces +Z; atan2(x,z) yaw convention)
    glm::vec3 forward = ctx.self->getRotation() * glm::vec3(0.0f, 0.0f, 1.0f);

    // Update perception (FOV cone, LOS) every frame
    updatePerception(dt, ctx, forward);

    if (m_waiting) {
        m_waitTimer += dt;
        // Stop movement while waiting
        ctx.self->setMoveVelocity(glm::vec3(0.0f));

        // Look-around sweep while waiting at waypoint
        m_lookAroundPhase += dt;
        float sweep = std::sin(m_lookAroundPhase * 2.0f) * 40.0f; // +/- 40 degrees
        float yawRad = glm::radians(m_baseYaw + sweep);
        ctx.self->setRotation(glm::angleAxis(yawRad, glm::vec3(0, 1, 0)));

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
        m_lookAroundPhase = 0.0f;

        // Capture base yaw for look-around sweep (face toward next waypoint)
        size_t nextWP = (m_currentWaypoint + 1) % m_waypoints.size();
        glm::vec3 toNext = m_waypoints[nextWP] - pos;
        if (glm::length(glm::vec2(toNext.x, toNext.z)) > 0.01f) {
            m_baseYaw = glm::degrees(std::atan2(toNext.x, toNext.z));
        }

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

void PatrolBehavior::updatePerception(float dt, NPCContext& ctx, const glm::vec3& forward) {
    if (!ctx.self || !ctx.entityRegistry) return;

    glm::vec3 pos = ctx.self->getPosition();

    // Wire voxel line-of-sight check if ChunkManager is available
    if (ctx.chunkManager && !m_perception.losCheck) {
        m_perception.losCheck = [cm = ctx.chunkManager](const glm::vec3& from, const glm::vec3& to) -> bool {
            glm::vec3 dir = to - from;
            float dist = glm::length(dir);
            if (dist < 0.01f) return true;
            dir /= dist;

            const float step = 0.5f;
            int steps = static_cast<int>(dist / step);
            for (int i = 1; i < steps; ++i) {
                glm::vec3 p = from + dir * (step * static_cast<float>(i));
                glm::ivec3 voxel(static_cast<int>(std::floor(p.x)),
                                 static_cast<int>(std::floor(p.y)),
                                 static_cast<int>(std::floor(p.z)));
                if (cm->hasVoxelAt(voxel)) {
                    return false;
                }
            }
            return true;
        };
    }

    // Wire debug cone visualization if RaycastVisualizer is available and enabled
    if (ctx.raycastVisualizer && ctx.raycastVisualizer->isEnabled() && !m_perception.debugConeDraw) {
        m_perception.debugConeDraw = [viz = ctx.raycastVisualizer](
            const glm::vec3& npcPos, const glm::vec3& npcForward,
            float range, float halfAngleDeg, bool hasThreat)
        {
            glm::vec3 eye = npcPos + glm::vec3(0.0f, 1.5f, 0.0f);
            glm::vec3 color = hasThreat ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 1.0f, 0.3f);

            glm::vec3 fwd2d(npcForward.x, 0.0f, npcForward.z);
            float fwdLen = glm::length(fwd2d);
            if (fwdLen > 0.001f) fwd2d /= fwdLen;
            else fwd2d = glm::vec3(0, 0, 1);

            float halfRad = glm::radians(halfAngleDeg);
            constexpr int NUM_SEGMENTS = 12;

            std::vector<glm::vec3> edgePoints;
            for (int i = 0; i <= NUM_SEGMENTS; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(NUM_SEGMENTS);
                float angle = -halfRad + t * 2.0f * halfRad;
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);
                glm::vec3 dir(fwd2d.x * cosA + fwd2d.z * sinA,
                              0.0f,
                              -fwd2d.x * sinA + fwd2d.z * cosA);
                edgePoints.push_back(eye + dir * range);
            }

            for (int i = 0; i <= NUM_SEGMENTS; i += 3) {
                viz->addLine(eye, edgePoints[i], color);
            }
            for (int i = 0; i < NUM_SEGMENTS; ++i) {
                viz->addLine(edgePoints[i], edgePoints[i + 1], color);
            }

            glm::vec3 fwdColor = hasThreat ? glm::vec3(1.0f, 0.5f, 0.0f) : glm::vec3(0.5f, 1.0f, 0.5f);
            viz->addLine(eye, eye + fwd2d * range, fwdColor);
        };
    } else if (ctx.raycastVisualizer && !ctx.raycastVisualizer->isEnabled()) {
        m_perception.debugConeDraw = nullptr;
    }

    m_perception.update(dt, pos, forward, ctx.selfId, *ctx.entityRegistry);
}

} // namespace Scene
} // namespace Phyxel
