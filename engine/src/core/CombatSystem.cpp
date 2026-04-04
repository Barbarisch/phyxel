#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "scene/Entity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Phyxel {
namespace Core {

std::vector<DamageEvent> CombatSystem::performAttack(
    const AttackParams& params,
    EntityRegistry& registry)
{
    std::vector<DamageEvent> events;

    // Find entities within reach
    auto nearby = registry.getEntitiesNear(params.attackerPos, params.reach);

    float coneThreshold = std::cos(glm::radians(params.coneAngleDeg * 0.5f));

    for (const auto& [entityId, entity] : nearby) {
        // Skip self
        if (entityId == params.attackerId) continue;

        // Skip if in invulnerability frames
        if (isInvulnerable(entityId)) continue;

        // Check if target is within the attack cone
        glm::vec3 toTarget = entity->getPosition() - params.attackerPos;
        float dist = glm::length(toTarget);
        if (dist < 0.001f) continue; // On top of attacker — skip

        glm::vec3 dirToTarget = toTarget / dist;
        float dot = glm::dot(
            glm::normalize(glm::vec3(params.attackerForward.x, 0.0f, params.attackerForward.z)),
            glm::normalize(glm::vec3(dirToTarget.x, 0.0f, dirToTarget.z))
        );

        if (dot < coneThreshold) continue; // Outside cone

        // Bone AABB refinement: if target is an animated character,
        // check if the attack sphere actually overlaps any bone AABB
        std::string hitBoneName;
        if (auto* animChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity)) {
            auto boneAABBs = animChar->getBoneAABBs();
            bool boneHit = false;
            for (const auto& bone : boneAABBs) {
                // Sphere-AABB overlap: closest point on AABB to sphere center
                glm::vec3 bmin = bone.center - bone.halfExtents;
                glm::vec3 bmax = bone.center + bone.halfExtents;
                glm::vec3 closest = glm::clamp(params.attackerPos, bmin, bmax);
                float d2 = glm::dot(closest - params.attackerPos, closest - params.attackerPos);
                if (d2 <= params.reach * params.reach) {
                    boneHit = true;
                    hitBoneName = bone.boneName;
                    break;
                }
            }
            if (!boneHit) continue; // Attack didn't reach any bone
        }

        // Get health component
        auto* health = entity->getHealthComponent();
        if (!health || !health->isAlive()) continue;

        // Calculate knockback direction
        glm::vec3 knockback = dirToTarget * params.knockbackForce;
        knockback.y = params.knockbackForce * 0.3f; // Slight upward lift

        // Deal damage
        float actual = health->takeDamage(params.damage, params.attackerId);

        DamageEvent event;
        event.attackerId = params.attackerId;
        event.targetId = entityId;
        event.amount = params.damage;
        event.actualDamage = actual;
        event.type = params.damageType;
        event.knockback = knockback;
        event.killed = !health->isAlive();
        event.hitBone = hitBoneName;

        // Apply invulnerability frames
        m_invulnTimers[entityId] = m_invulnDuration;

        // Apply knockback via velocity
        entity->setMoveVelocity(knockback);

        events.push_back(event);

        LOG_INFO("Combat", "{} hit {} for {:.1f} damage (actual: {:.1f}){}",
                 params.attackerId, entityId, params.damage, actual,
                 event.killed ? " — KILLED" : "");

        if (m_onDamage) m_onDamage(event);
    }

    return events;
}

bool CombatSystem::isInvulnerable(const std::string& entityId) const {
    auto it = m_invulnTimers.find(entityId);
    return it != m_invulnTimers.end() && it->second > 0.0f;
}

void CombatSystem::update(float dt) {
    auto it = m_invulnTimers.begin();
    while (it != m_invulnTimers.end()) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = m_invulnTimers.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Core
} // namespace Phyxel
