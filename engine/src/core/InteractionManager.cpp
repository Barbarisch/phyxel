#include "core/InteractionManager.h"
#include "core/EntityRegistry.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

void InteractionManager::update(float dt, const glm::vec3& playerPos) {
    m_nearestNPC = nullptr;

    if (!m_registry) return;

    // Decrease cooldown
    if (m_cooldownTimer > 0.0f) {
        m_cooldownTimer -= dt;
    }

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

void InteractionManager::tryInteract(Scene::Entity* playerEntity) {
    if (!m_nearestNPC) return;

    if (m_cooldownTimer > 0.0f) return;
    m_cooldownTimer = INTERACT_COOLDOWN;

    LOG_INFO("InteractionManager", "Player interacting with NPC '{}'", m_nearestNPC->getName());

    // Trigger the NPC's behavior callback
    if (auto* behavior = m_nearestNPC->getBehavior()) {
        behavior->onInteract(playerEntity);
    }

    // Fire the external callback (e.g. to open dialogue UI)
    if (m_interactCallback) {
        m_interactCallback(m_nearestNPC);
    }
}

} // namespace Core
} // namespace Phyxel
