#include "ai/PerceptionSystem.h"
#include "core/EntityRegistry.h"
#include "scene/Entity.h"

#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace AI {

// ============================================================================
// SenseResult
// ============================================================================

nlohmann::json SenseResult::toJson() const {
    return {
        {"entityId", entityId},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}}},
        {"distance", distance},
        {"angle", angle},
        {"isVisible", isVisible},
        {"lastSeen", lastSeen},
        {"threatLevel", threatLevel}
    };
}

// ============================================================================
// PerceptionComponent
// ============================================================================

void PerceptionComponent::update(float dt,
                                  const glm::vec3& npcPos,
                                  const glm::vec3& npcForward,
                                  const std::string& selfId,
                                  Core::EntityRegistry& registry)
{
    decayMemory(dt);

    // Draw debug cone every frame so it doesn't flash (beginFrame clears lines)
    if (debugConeDraw) {
        glm::vec3 fwd = m_lastForward;
        if (glm::length(fwd) < 0.001f) fwd = npcForward;
        bool hasThreat = !getThreats(0.5f).empty();
        debugConeDraw(npcPos, fwd, visionRange, visionAngle * 0.5f, hasThreat);
    }

    m_timer += dt;
    if (m_timer < updateInterval) return;
    m_timer = 0.0f;

    m_lastForward = npcForward;
    perceive(npcPos, npcForward, selfId, registry);
}

void PerceptionComponent::perceive(const glm::vec3& npcPos,
                                    const glm::vec3& npcForward,
                                    const std::string& selfId,
                                    Core::EntityRegistry& registry)
{
    // Reset visibility for all known entities at start of perception tick.
    // Entities that are currently visible will be re-set below.
    for (auto& [id, sr] : m_known) {
        sr.isVisible = false;
    }

    // Use the larger of vision/hearing range for the spatial query
    float queryRadius = std::max(visionRange, hearingRange);
    auto nearby = registry.getEntitiesNear(npcPos, queryRadius);

    // Normalize forward on XZ plane
    glm::vec3 fwd2d = glm::vec3(npcForward.x, 0.0f, npcForward.z);
    float fwdLen = glm::length(fwd2d);
    if (fwdLen > 0.001f) fwd2d /= fwdLen;
    else fwd2d = glm::vec3(0, 0, -1);

    float halfConeRad = glm::radians(visionAngle * 0.5f);
    float coneThreshold = std::cos(halfConeRad);

    for (const auto& [entityId, entity] : nearby) {
        if (entityId == selfId) continue;

        glm::vec3 toEntity = entity->getPosition() - npcPos;
        float dist = glm::length(toEntity);
        if (dist < 0.001f) continue;

        glm::vec3 dir2d = glm::vec3(toEntity.x, 0.0f, toEntity.z);
        float dir2dLen = glm::length(dir2d);
        float dot = 0.0f;
        float angleDeg = 180.0f;
        if (dir2dLen > 0.001f) {
            dir2d /= dir2dLen;
            dot = glm::dot(fwd2d, dir2d);
            angleDeg = glm::degrees(std::acos(glm::clamp(dot, -1.0f, 1.0f)));
        }

        bool visible = (dist <= visionRange) && (dot >= coneThreshold);
        bool heard = (dist <= hearingRange);

        // If in vision cone and LOS checker is available, verify line-of-sight
        if (visible && losCheck) {
            glm::vec3 eyePos = npcPos + glm::vec3(0.0f, 1.5f, 0.0f); // eye height offset
            glm::vec3 targetPos = entity->getPosition() + glm::vec3(0.0f, 1.0f, 0.0f); // target body center
            if (!losCheck(eyePos, targetPos)) {
                visible = false;
            }
        }

        if (!visible && !heard) continue;

        // Update or create sense entry
        auto it = m_known.find(entityId);
        if (it != m_known.end()) {
            // Update existing
            it->second.position = entity->getPosition();
            it->second.distance = dist;
            it->second.angle = angleDeg;
            if (visible) {
                it->second.isVisible = true;
                it->second.lastSeen = 0.0f;
            }
            // Don't reset threat — that's set externally
        } else {
            // New detection
            SenseResult sr;
            sr.entityId = entityId;
            sr.position = entity->getPosition();
            sr.distance = dist;
            sr.angle = angleDeg;
            sr.isVisible = visible;
            sr.lastSeen = 0.0f;
            sr.threatLevel = 0.0f;
            m_known[entityId] = sr;
        }
    }
}

void PerceptionComponent::decayMemory(float dt) {
    auto it = m_known.begin();
    while (it != m_known.end()) {
        it->second.lastSeen += dt;
        if (it->second.lastSeen > memoryDuration) {
            it = m_known.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<SenseResult> PerceptionComponent::getVisibleEntities() const {
    std::vector<SenseResult> result;
    for (const auto& [id, sr] : m_known) {
        if (sr.isVisible) result.push_back(sr);
    }
    return result;
}

std::vector<SenseResult> PerceptionComponent::getHeardEntities() const {
    std::vector<SenseResult> result;
    for (const auto& [id, sr] : m_known) {
        if (sr.distance <= hearingRange) result.push_back(sr);
    }
    return result;
}

std::vector<SenseResult> PerceptionComponent::getThreats(float minThreat) const {
    std::vector<SenseResult> result;
    for (const auto& [id, sr] : m_known) {
        if (sr.threatLevel >= minThreat) result.push_back(sr);
    }
    return result;
}

bool PerceptionComponent::isAwareOf(const std::string& entityId) const {
    return m_known.find(entityId) != m_known.end();
}

const SenseResult* PerceptionComponent::getSense(const std::string& entityId) const {
    auto it = m_known.find(entityId);
    return (it != m_known.end()) ? &it->second : nullptr;
}

void PerceptionComponent::setThreatLevel(const std::string& entityId, float level) {
    auto it = m_known.find(entityId);
    if (it != m_known.end()) {
        it->second.threatLevel = level;
    }
}

nlohmann::json PerceptionComponent::toJson() const {
    nlohmann::json j;
    j["visionRange"] = visionRange;
    j["visionAngle"] = visionAngle;
    j["hearingRange"] = hearingRange;
    j["memoryDuration"] = memoryDuration;
    nlohmann::json entities = nlohmann::json::array();
    for (const auto& [id, sr] : m_known) {
        entities.push_back(sr.toJson());
    }
    j["knownEntities"] = entities;
    j["count"] = m_known.size();
    return j;
}

} // namespace AI
} // namespace Phyxel
