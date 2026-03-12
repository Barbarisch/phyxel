#include "scene/behaviors/IdleBehavior.h"
#include "scene/Entity.h"
#include "core/EntityRegistry.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Scene {

void IdleBehavior::update(float dt, NPCContext& ctx) {
    m_lookTimer += dt;
    if (m_lookTimer < LOOK_INTERVAL) return;
    m_lookTimer = 0.0f;

    // Optionally face the nearest entity
    if (!ctx.entityRegistry || !ctx.self) return;

    auto nearby = ctx.entityRegistry->getEntitiesNear(ctx.self->getPosition(), m_lookRadius);
    float bestDist = m_lookRadius * m_lookRadius;
    glm::vec3 bestPos = ctx.self->getPosition();
    bool found = false;

    for (const auto& [id, entity] : nearby) {
        if (entity == ctx.self) continue;
        float d2 = glm::dot(entity->getPosition() - ctx.self->getPosition(),
                            entity->getPosition() - ctx.self->getPosition());
        if (d2 < bestDist) {
            bestDist = d2;
            bestPos = entity->getPosition();
            found = true;
        }
    }

    if (found) {
        glm::vec3 dir = bestPos - ctx.self->getPosition();
        if (glm::length(dir) > 0.01f) {
            float yaw = glm::degrees(atan2(dir.x, dir.z));
            ctx.self->setRotation(glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)));
        }
    }
}

void IdleBehavior::onInteract(Entity* interactor) {
    LOG_INFO("IdleBehavior", "NPC interacted with by entity");
}

void IdleBehavior::onEvent(const std::string& eventType, const nlohmann::json& data) {
    // Idle NPCs don't react to events
}

} // namespace Scene
} // namespace Phyxel
