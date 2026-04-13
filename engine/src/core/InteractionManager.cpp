#include "core/InteractionManager.h"
#include "core/EntityRegistry.h"
#include "core/PlacedObjectManager.h"
#include "core/InteractionProfileManager.h"
#include "core/interactions/SeatInteractionHandler.h"
#include "core/interactions/DoorInteractionHandler.h"
#include "core/interactions/NPCInteractionHandler.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"
#include <cmath>

namespace Phyxel {
namespace Core {

// --- Legacy callback setters: delegate to the appropriate handler in the registry ---

void InteractionManager::setInteractCallback(InteractCallback callback) {
    if (m_handlerRegistry) {
        if (auto* h = dynamic_cast<NPCInteractionHandler*>(m_handlerRegistry->getHandler("npc"))) {
            h->setInteractCallback(std::move(callback));
            return;
        }
    }
    // Fallback: store locally if no registry yet (will be wired later)
    m_fallbackNPCCallback = std::move(callback);
}

void InteractionManager::setSeatCallback(SeatCallback callback) {
    if (m_handlerRegistry) {
        if (auto* h = dynamic_cast<SeatInteractionHandler*>(m_handlerRegistry->getHandler("seat"))) {
            h->setSeatCallback(std::move(callback));
            return;
        }
    }
    m_fallbackSeatCallback = std::move(callback);
}

void InteractionManager::setDoorCallback(DoorCallback callback) {
    if (m_handlerRegistry) {
        if (auto* h = dynamic_cast<DoorInteractionHandler*>(m_handlerRegistry->getHandler("door_handle"))) {
            h->setDoorCallback(std::move(callback));
            return;
        }
    }
    m_fallbackDoorCallback = std::move(callback);
}

bool InteractionManager::hasDoorCallback() const {
    if (m_handlerRegistry) {
        if (auto* h = dynamic_cast<DoorInteractionHandler*>(m_handlerRegistry->getHandler("door_handle"))) {
            return true;  // Handler exists
        }
    }
    return false;
}

// --- Build typeData JSON from seat-specific fields on InteractionPoint ---

nlohmann::json InteractionManager::buildSeatTypeData(const InteractionPoint& pt) {
    return {
        {"sitDownOffset",      {pt.worldSitDownOffset.x,     pt.worldSitDownOffset.y,     pt.worldSitDownOffset.z}},
        {"sittingIdleOffset",  {pt.worldSittingIdleOffset.x, pt.worldSittingIdleOffset.y, pt.worldSittingIdleOffset.z}},
        {"sitStandUpOffset",   {pt.worldSitStandUpOffset.x,  pt.worldSitStandUpOffset.y,  pt.worldSitStandUpOffset.z}},
        {"sitBlendDuration",   pt.sitBlendDuration},
        {"seatHeightOffset",   pt.seatHeightOffset}
    };
}

void InteractionManager::update(float dt, const glm::vec3& playerPos, const glm::vec3& playerFront) {
    m_nearestNPC = nullptr;
    m_nearest.clear();
    m_activePromptText.clear();
    m_activePromptWorldPos = glm::vec3(0.0f);

    if (m_cooldownTimer > 0.0f) {
        m_cooldownTimer -= dt;
    }

    // --- NPC detection (special case: NPCs are entities, not placed objects) ---
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

    // --- Generic placed-object interaction detection ---
    // Scan all registered handler types and find the best candidate.
    if (m_placedObjects && m_handlerRegistry) {
        for (const auto& [type, handler] : m_handlerRegistry->getHandlers()) {
            if (type == "npc") continue;  // NPCs handled above

            float defaultRadius = handler->getDefaultRadius();
            auto result = m_placedObjects->findNearestFreePointEx(
                playerPos, playerFront, defaultRadius, type);

            if (!result.found) continue;

            int priority = handler->getPriority();
            if (!m_nearest.found || priority > m_nearest.priority) {
                const PlacedObject* obj = m_placedObjects->get(result.objectId);
                if (!obj) continue;

                // Find the matching interaction point for archetype filtering + data extraction
                const InteractionPoint* matchedPt = nullptr;
                for (const auto& pt : obj->interactionPoints) {
                    if (pt.pointId == result.pointId) {
                        if (!pt.supportsArchetype(m_playerArchetype)) break;
                        matchedPt = &pt;
                        break;
                    }
                }
                if (!matchedPt) continue;

                bool wasEmpty = !m_nearest.found;
                m_nearest.found = true;
                m_nearest.type = type;
                m_nearest.objectId = result.objectId;
                m_nearest.pointId = result.pointId;
                m_nearest.templateName = obj->templateName;
                m_nearest.worldPos = matchedPt->worldPos;
                m_nearest.facingYaw = matchedPt->facingYaw;
                m_nearest.objectRotation = matchedPt->objectRotation;
                m_nearest.promptText = matchedPt->promptText;
                m_nearest.priority = priority;

                // Build type-specific data for seat interactions
                if (type == "seat") {
                    m_nearest.typeData = buildSeatTypeData(*matchedPt);
                } else {
                    m_nearest.typeData = nlohmann::json::object();
                }

                if (wasEmpty) {
                    LOG_INFO("InteractionManager",
                        "{} in range: obj='{}' pt='{}' pos=({:.1f},{:.1f},{:.1f}) archetype='{}'",
                        type, result.objectId, result.pointId,
                        matchedPt->worldPos.x, matchedPt->worldPos.y, matchedPt->worldPos.z,
                        m_playerArchetype);
                }
            }
        }
    }

    // --- Resolve active prompt (priority: NPC > placed-object interaction) ---
    int npcPriority = m_handlerRegistry ? 50 : 100;  // NPC handler default priority
    if (m_handlerRegistry) {
        if (auto* h = m_handlerRegistry->getHandler("npc"))
            npcPriority = h->getPriority();
    }

    if (m_nearestNPC && (!m_nearest.found || npcPriority >= m_nearest.priority)) {
        m_activePromptText = "Interact";
        m_activePromptWorldPos = m_nearestNPC->getPosition();
    } else if (m_nearest.found) {
        // Use custom prompt from the point, or fall back to handler's default
        if (!m_nearest.promptText.empty()) {
            m_activePromptText = m_nearest.promptText;
        } else if (m_handlerRegistry) {
            if (auto* h = m_handlerRegistry->getHandler(m_nearest.type))
                m_activePromptText = h->getDefaultPrompt();
        }
        m_activePromptWorldPos = m_nearest.worldPos;
    }
}

void InteractionManager::tryInteract(Scene::Entity* playerEntity) {
    LOG_WARN("InteractionManager", "tryInteract: cooldown={:.3f} nearNPC={} nearType='{}' nearObj='{}'",
             m_cooldownTimer, (m_nearestNPC != nullptr),
             m_nearest.found ? m_nearest.type : "",
             m_nearest.found ? m_nearest.objectId : "");
    if (m_cooldownTimer > 0.0f) { LOG_WARN("InteractionManager", "  -> blocked by cooldown"); return; }

    // Determine if NPC wins priority
    int npcPriority = 50;
    if (m_handlerRegistry) {
        if (auto* h = m_handlerRegistry->getHandler("npc"))
            npcPriority = h->getPriority();
    }

    bool npcWins = m_nearestNPC && (!m_nearest.found || npcPriority >= m_nearest.priority);

    if (npcWins) {
        m_cooldownTimer = INTERACT_COOLDOWN;
        // Dispatch through NPC handler if registered, else use fallback
        if (m_handlerRegistry) {
            if (auto* h = m_handlerRegistry->getHandler("npc")) {
                InteractionContext ctx;
                ctx.npc = m_nearestNPC;
                ctx.playerEntity = playerEntity;
                ctx.playerArchetype = m_playerArchetype;
                ctx.worldPos = m_nearestNPC->getPosition();
                h->execute(ctx);
                return;
            }
        }
        // Fallback (no registry or no NPC handler)
        LOG_INFO("InteractionManager", "Player interacting with NPC '{}'", m_nearestNPC->getName());
        if (auto* behavior = m_nearestNPC->getBehavior()) {
            behavior->onInteract(playerEntity);
        }
        if (m_fallbackNPCCallback) {
            m_fallbackNPCCallback(m_nearestNPC);
        }
        return;
    }

    if (m_nearest.found && m_handlerRegistry) {
        if (auto* handler = m_handlerRegistry->getHandler(m_nearest.type)) {
            m_cooldownTimer = INTERACT_COOLDOWN;

            InteractionContext ctx;
            ctx.objectId = m_nearest.objectId;
            ctx.pointId = m_nearest.pointId;
            ctx.templateName = m_nearest.templateName;
            ctx.playerEntity = playerEntity;
            ctx.worldPos = m_nearest.worldPos;
            ctx.facingYaw = m_nearest.facingYaw;
            ctx.objectRotation = m_nearest.objectRotation;
            ctx.playerArchetype = m_playerArchetype;
            ctx.typeData = m_nearest.typeData;

            handler->execute(ctx);

            m_nearest.clear();
            return;
        }
    }
}

void InteractionManager::triggerInteraction(Scene::NPCEntity* npc) {
    if (!npc) return;
    LOG_INFO("InteractionManager", "API-triggered interaction with NPC '{}'", npc->getName());

    if (m_handlerRegistry) {
        if (auto* h = dynamic_cast<NPCInteractionHandler*>(m_handlerRegistry->getHandler("npc"))) {
            InteractionContext ctx;
            ctx.npc = npc;
            h->execute(ctx);
            return;
        }
    }
    // Fallback
    if (m_fallbackNPCCallback) {
        m_fallbackNPCCallback(npc);
    }
}

void InteractionManager::releaseSeat(const std::string& occupantId) {
    if (m_placedObjects) {
        m_placedObjects->releaseAllByOccupant(occupantId);
    }
}

} // namespace Core
} // namespace Phyxel
