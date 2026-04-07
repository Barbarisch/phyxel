#include "core/InteractionManager.h"
#include "core/EntityRegistry.h"
#include "core/PlacedObjectManager.h"
#include "core/InteractionProfileManager.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"

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

void InteractionManager::update(float dt, const glm::vec3& playerPos) {
    m_nearestNPC = nullptr;
    m_nearestSeatObjId.clear();
    m_nearestSeatPtId.clear();
    m_nearestSeatTemplateName.clear();

    if (m_cooldownTimer > 0.0f) {
        m_cooldownTimer -= dt;
    }

    // --- NPC detection ---
    if (m_registry) {
        auto npcs = m_registry->getEntitiesByType("npc");
        float bestDist2 = std::numeric_limits<float>::max();
        for (const auto& [id, entity] : npcs) {
            auto* npc = dynamic_cast<Scene::NPCEntity*>(entity);
            if (!npc) continue;
            glm::vec3 diff = npc->getPosition() - playerPos;
            float dist2 = glm::dot(diff, diff);
            float radius = npc->getInteractionRadius();
            if (dist2 < radius * radius && dist2 < bestDist2) {
                bestDist2 = dist2;
                m_nearestNPC = npc;
            }
        }
    }

    // --- Seat detection (with archetype filtering) ---
    if (m_placedObjects) {
        auto [objId, ptId] = m_placedObjects->findNearestFreePoint(
            playerPos, SEAT_INTERACT_RADIUS, "seat");
        if (!objId.empty()) {
            const PlacedObject* obj = m_placedObjects->get(objId);
            if (obj) {
                for (const auto& pt : obj->interactionPoints) {
                    if (pt.pointId == ptId) {
                        // Archetype gating: skip if player's archetype is not supported
                        if (!pt.supportsArchetype(m_playerArchetype)) {
                            break;
                        }

                        bool wasEmpty = m_nearestSeatObjId.empty();
                        m_nearestSeatObjId          = objId;
                        m_nearestSeatPtId           = ptId;
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
                                objId, ptId,
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
}

void InteractionManager::tryInteract(Scene::Entity* playerEntity) {
    if (m_cooldownTimer > 0.0f) return;

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
