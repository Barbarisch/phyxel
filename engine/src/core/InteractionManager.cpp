#include "core/InteractionManager.h"
#include "core/EntityRegistry.h"
#include "core/PlacedObjectManager.h"
#include "core/InteractionProfileManager.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"
#include <cmath>

namespace Phyxel {
namespace Core {

/// Rotate a template-local offset by object rotation (0/90/180/270 degrees around Y).
static glm::vec3 rotateOffsetByDegrees(const glm::vec3& offset, int rotationDegrees) {
    switch (rotationDegrees % 360) {
        case 90:  return { -offset.z, offset.y,  offset.x };
        case 180: return { -offset.x, offset.y, -offset.z };
        case 270: return {  offset.z, offset.y, -offset.x };
        default:  return offset;
    }
}

void InteractionManager::update(float dt, const glm::vec3& playerPos, const glm::vec3& playerFront) {
    m_nearestNPC = nullptr;
    m_nearestSeatObjId.clear();
    m_nearestSeatPtId.clear();
    m_nearestSeatTemplateName.clear();
    m_nearestDoorObjId.clear();
    m_nearestDoorPtId.clear();
    m_activePromptText.clear();
    m_activePromptWorldPos = glm::vec3(0.0f);

    if (m_cooldownTimer > 0.0f) {
        m_cooldownTimer -= dt;
    }

    // --- NPC detection (with view-angle check) ---
    if (m_registry) {
        auto npcs = m_registry->getEntitiesByType("npc");
        float bestDist2 = std::numeric_limits<float>::max();
        bool hasFront = glm::dot(playerFront, playerFront) > 0.001f;
        glm::vec3 frontXZ = hasFront ? glm::normalize(glm::vec3(playerFront.x, 0.0f, playerFront.z)) : glm::vec3(0);

        for (const auto& [id, entity] : npcs) {
            auto* npc = dynamic_cast<Scene::NPCEntity*>(entity);
            if (!npc) continue;
            glm::vec3 diff = npc->getPosition() - playerPos;
            float dist2 = glm::dot(diff, diff);
            float radius = npc->getInteractionRadius();
            if (dist2 < radius * radius && dist2 < bestDist2) {
                // View angle check for NPCs (use default cone)
                if (hasFront && dist2 > 0.01f) {
                    glm::vec3 toNPC = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
                    float cosAngle = glm::dot(frontXZ, toNPC);
                    if (cosAngle < cosf(glm::radians(DEFAULT_VIEW_ANGLE_HALF))) continue;
                }
                bestDist2 = dist2;
                m_nearestNPC = npc;
            }
        }
    }

    // --- Door detection (with per-point radius and view angle) ---
    if (m_placedObjects) {
        auto result = m_placedObjects->findNearestFreePointEx(
            playerPos, playerFront, DOOR_INTERACT_RADIUS, "door_handle");
        if (result.found) {
            m_nearestDoorObjId = result.objectId;
            m_nearestDoorPtId  = result.pointId;
        }
    }

    // --- Seat detection (with per-point radius, view angle, and archetype filtering) ---
    if (m_placedObjects) {
        auto result = m_placedObjects->findNearestFreePointEx(
            playerPos, playerFront, SEAT_INTERACT_RADIUS, "seat");
        if (result.found) {
            const PlacedObject* obj = m_placedObjects->get(result.objectId);
            if (obj) {
                for (const auto& pt : obj->interactionPoints) {
                    if (pt.pointId == result.pointId) {
                        // Archetype gating: skip if player's archetype is not supported
                        if (!pt.supportsArchetype(m_playerArchetype)) {
                            break;
                        }

                        bool wasEmpty = m_nearestSeatObjId.empty();
                        m_nearestSeatObjId          = result.objectId;
                        m_nearestSeatPtId           = result.pointId;
                        m_nearestSeatTemplateName   = obj->templateName;
                        m_nearestSeatAnchorPos      = pt.worldPos;
                        m_nearestSeatFacingYaw      = pt.facingYaw;
                        m_nearestSeatObjectRotation = pt.objectRotation;
                        // Store default offsets from asset def (may be overridden by profile at interact time)
                        m_nearestSeatSitDownOffset      = pt.worldSitDownOffset;
                        m_nearestSeatSittingIdleOffset  = pt.worldSittingIdleOffset;
                        m_nearestSeatSitStandUpOffset   = pt.worldSitStandUpOffset;
                        m_nearestSeatSitBlendDuration   = pt.sitBlendDuration;
                        m_nearestSeatHeightOffset       = pt.seatHeightOffset;
                        if (wasEmpty) {
                            LOG_INFO("InteractionManager",
                                "Seat in range: obj='{}' pt='{}' anchorPos=({:.1f},{:.1f},{:.1f}) "
                                "playerPos=({:.1f},{:.1f},{:.1f}) archetype='{}'",
                                result.objectId, result.pointId,
                                pt.worldPos.x, pt.worldPos.y, pt.worldPos.z,
                                playerPos.x, playerPos.y, playerPos.z,
                                m_playerArchetype);
                        }
                        break;
                    }
                }
            }
        }
    }

    // --- Resolve active prompt (priority: NPC > Door > Seat) ---
    if (m_nearestNPC) {
        m_activePromptText = "Interact";
        m_activePromptWorldPos = m_nearestNPC->getPosition();
    } else if (!m_nearestDoorObjId.empty()) {
        // Resolve door prompt text from the interaction point
        const PlacedObject* obj = m_placedObjects ? m_placedObjects->get(m_nearestDoorObjId) : nullptr;
        if (obj) {
            for (const auto& pt : obj->interactionPoints) {
                if (pt.pointId == m_nearestDoorPtId) {
                    m_activePromptText = pt.promptText.empty() ? "Open / Close" : pt.promptText;
                    m_activePromptWorldPos = pt.worldPos;
                    break;
                }
            }
        }
        if (m_activePromptText.empty()) m_activePromptText = "Open / Close";
    } else if (!m_nearestSeatObjId.empty()) {
        // Resolve seat prompt text from the interaction point
        const PlacedObject* obj = m_placedObjects ? m_placedObjects->get(m_nearestSeatObjId) : nullptr;
        if (obj) {
            for (const auto& pt : obj->interactionPoints) {
                if (pt.pointId == m_nearestSeatPtId) {
                    m_activePromptText = pt.promptText.empty() ? "Sit down" : pt.promptText;
                    m_activePromptWorldPos = pt.worldPos;
                    break;
                }
            }
        }
        if (m_activePromptText.empty()) m_activePromptText = "Sit down";
    }
}

void InteractionManager::tryInteract(Scene::Entity* playerEntity) {
    LOG_WARN("InteractionManager", "tryInteract: cooldown={:.3f} nearNPC={} nearDoor='{}' doorCb={} nearSeat='{}'",
             m_cooldownTimer, (m_nearestNPC != nullptr), m_nearestDoorObjId,
             (m_doorCallback != nullptr), m_nearestSeatObjId);
    if (m_cooldownTimer > 0.0f) { LOG_WARN("InteractionManager", "  -> blocked by cooldown"); return; }

    // Prefer NPC if one is in range; fall back to seat.
    if (m_nearestNPC) {
        m_cooldownTimer = INTERACT_COOLDOWN;
        LOG_INFO("InteractionManager", "Player interacting with NPC '{}'", m_nearestNPC->getName());
        if (auto* behavior = m_nearestNPC->getBehavior()) {
            behavior->onInteract(playerEntity);
        }
        if (m_interactCallback) {
            m_interactCallback(m_nearestNPC);
        }
        return;
    }

    if (!m_nearestDoorObjId.empty() && m_doorCallback) {
        m_cooldownTimer = INTERACT_COOLDOWN;
        LOG_INFO("InteractionManager", "Player interacting with door '{}'", m_nearestDoorObjId);
        m_doorCallback(m_nearestDoorObjId, m_nearestDoorPtId);
        m_nearestDoorObjId.clear();
        m_nearestDoorPtId.clear();
        return;
    }

    if (!m_nearestSeatObjId.empty() && m_seatCallback) {
        m_cooldownTimer = INTERACT_COOLDOWN;
        // Claim the seat so nobody else (NPC) can grab it
        std::string occupantId = "player";
        if (m_placedObjects) {
            m_placedObjects->claimInteractionPoint(m_nearestSeatObjId, m_nearestSeatPtId, occupantId);
        }

        // Resolve per-archetype profile offsets (override asset defaults if profile exists)
        glm::vec3 sitDown = m_nearestSeatSitDownOffset;
        glm::vec3 sittingIdle = m_nearestSeatSittingIdleOffset;
        glm::vec3 sitStandUp = m_nearestSeatSitStandUpOffset;
        float blendDur = m_nearestSeatSitBlendDuration;
        float heightOff = m_nearestSeatHeightOffset;

        if (m_profileManager) {
            const auto* profile = m_profileManager->getProfile(
                m_playerArchetype, m_nearestSeatTemplateName, m_nearestSeatPtId);
            if (profile) {
                // Profile offsets are in template-local space — rotate to world space
                sitDown     = rotateOffsetByDegrees(profile->sitDownOffset,      m_nearestSeatObjectRotation);
                sittingIdle = rotateOffsetByDegrees(profile->sittingIdleOffset,   m_nearestSeatObjectRotation);
                sitStandUp  = rotateOffsetByDegrees(profile->sitStandUpOffset,    m_nearestSeatObjectRotation);
                blendDur    = profile->sitBlendDuration;
                heightOff   = profile->seatHeightOffset;
                LOG_INFO("InteractionManager", "Using profile for archetype '{}' template '{}' point '{}'",
                         m_playerArchetype, m_nearestSeatTemplateName, m_nearestSeatPtId);
            }
        }

        LOG_INFO("InteractionManager", "Player sitting at '{}':'{}'",
                 m_nearestSeatObjId, m_nearestSeatPtId);
        m_seatCallback(m_nearestSeatObjId, m_nearestSeatPtId,
                       m_nearestSeatAnchorPos, m_nearestSeatFacingYaw,
                       sitDown, sittingIdle, sitStandUp, blendDur, heightOff);
        m_nearestSeatObjId.clear();
        m_nearestSeatPtId.clear();
    }
}

void InteractionManager::triggerInteraction(Scene::NPCEntity* npc) {
    if (!npc) return;
    LOG_INFO("InteractionManager", "API-triggered interaction with NPC '{}'", npc->getName());
    if (m_interactCallback) {
        m_interactCallback(npc);
    }
}

void InteractionManager::releaseSeat(const std::string& occupantId) {
    if (m_placedObjects) {
        m_placedObjects->releaseAllByOccupant(occupantId);
    }
}

} // namespace Core
} // namespace Phyxel
