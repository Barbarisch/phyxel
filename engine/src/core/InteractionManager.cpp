#include "core/InteractionManager.h"
#include "core/EntityRegistry.h"
#include "core/PlacedObjectManager.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

void InteractionManager::update(float dt, const glm::vec3& playerPos) {
    m_nearestNPC = nullptr;
    m_nearestSeatObjId.clear();
    m_nearestSeatPtId.clear();

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

    // --- Seat detection ---
    if (m_placedObjects) {
        auto [objId, ptId] = m_placedObjects->findNearestFreePoint(
            playerPos, SEAT_INTERACT_RADIUS, "seat");
        if (!objId.empty()) {
            m_nearestSeatObjId = objId;
            m_nearestSeatPtId  = ptId;
            // Retrieve world pos and facing for the prompt/callback
            const PlacedObject* obj = m_placedObjects->get(objId);
            if (obj) {
                for (const auto& pt : obj->interactionPoints) {
                    if (pt.pointId == ptId) {
                        m_nearestSeatPos       = pt.worldPos;
                        m_nearestSeatFacingYaw = pt.facingYaw;
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
        LOG_INFO("InteractionManager", "Player sitting at '{}':'{}'",
                 m_nearestSeatObjId, m_nearestSeatPtId);
        m_seatCallback(m_nearestSeatObjId, m_nearestSeatPtId, m_nearestSeatPos, m_nearestSeatFacingYaw);
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
