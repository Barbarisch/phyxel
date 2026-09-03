#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/SoundRegistry.h"
#include "core/HealthComponent.h"
#include "scene/Entity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include <glm/gtc/constants.hpp>
#include <cctype>
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
        // Skip self — by pointer (robust) and by id label.
        if (entity == params.attackerEntity) continue;
        if (entityId == params.attackerId) continue;

        // Skip if in invulnerability frames (post-hit timer, or an external
        // predicate such as dodge i-frames).
        if (isInvulnerable(entityId)) continue;
        if (m_invulnQuery && m_invulnQuery(entity)) continue;

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

        // Skip targets with no usable health component (the cone/bone tests
        // above already qualified the hit; applyDamage re-checks defensively).
        auto* health = entity->getHealthComponent();
        if (!health || !health->isAlive()) continue;

        // Calculate knockback direction
        glm::vec3 knockback = dirToTarget * params.knockbackForce;
        knockback.y = params.knockbackForce * 0.3f; // Slight upward lift

        // Weapons CLASH when the target is mid-swing themselves — two blades
        // trading blows ring metal-on-metal instead of a body hit. (Pure
        // audio flavor: damage is unchanged; parry MECHANICS are a separate
        // feature.)
        bool clash = false;
        if (auto* animTarget = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity)) {
            clash = animTarget->getAnimationState() == Scene::AnimatedCharacterState::Attack;
        }

        // Deal damage through the single damage entry point.
        DamageEvent event = applyDamage(entity, entityId, params.damage,
                                        params.attackerId, params.damageType,
                                        knockback, hitBoneName, clash);
        events.push_back(event);
    }

    return events;
}

DamageEvent CombatSystem::applyDamage(
    Scene::Entity* target,
    const std::string& targetId,
    float amount,
    const std::string& sourceId,
    DamageType type,
    const glm::vec3& knockback,
    const std::string& hitBone,
    bool weaponsClash)
{
    DamageEvent event;
    event.attackerId = sourceId;
    event.targetId   = targetId;
    event.amount     = amount;
    event.type       = type;
    event.knockback  = knockback;
    event.hitBone    = hitBone;

    if (!target) return event;
    auto* health = target->getHealthComponent();
    if (!health || !health->isAlive()) return event;

    float actual = health->takeDamage(amount, sourceId);
    event.actualDamage = actual;
    event.killed       = !health->isAlive();

    // Post-hit invulnerability frames (so a single swing can't multi-hit).
    m_invulnTimers[targetId] = m_invulnDuration;

    // Apply knockback via velocity if any was requested.
    if (knockback != glm::vec3(0.0f))
        target->setMoveVelocity(knockback);

    // Plain {} only — this logger prints printf-style specs LITERALLY and then
    // shifts the remaining args, so "{:.1f}" produced
    // "hit X for {:.1f} damage (actual: {:.1f})4". Fourth instance of this bug.
    LOG_INFO("Combat", "{} hit {} for {} damage (actual: {}){}",
             sourceId, targetId, amount, actual,
             event.killed ? " — KILLED" : "");

    // Combat audio at the point of impact — the target's position, so a fight
    // to your left SOUNDS to your left. Emitted here (the single damage entry
    // point) so real-time NPC brawls, turn-based combat, and player swings all
    // sound identical in every host, editor or shipped game.
    if (m_soundRegistry) {
        const glm::vec3 pos = target->getPosition();
        if (actual <= 0.0f) {
            // Fully absorbed (armor/resistance): metal ring, no vocal.
            m_soundRegistry->playEvent("combat.impact.metal", pos);
        } else {
            // Impact keyed by DAMAGE TYPE so a fire bolt cracks and an ice
            // shard shatters instead of everything slapping like a fist:
            // "combat.impact.<type>" (lowercased enum name — fire/ice/
            // lightning/...) falling back to the generic flesh hit. Purely
            // data-driven: adding the event to sounds.json IS the feature.
            // Physical hits on a mid-swing target ring METAL — blades met.
            std::string typeName = damageTypeToString(type);
            for (auto& c : typeName) c = static_cast<char>(::tolower(c));
            const bool physical = (type == DamageType::Physical ||
                                   type == DamageType::Bludgeoning ||
                                   type == DamageType::Piercing ||
                                   type == DamageType::Slashing);
            if (weaponsClash && physical) {
                m_soundRegistry->playEvent("combat.impact.metal", pos);
            } else {
                m_soundRegistry->playEventOr("combat.impact." + typeName,
                                             "combat.impact.flesh", pos);
            }
            if (event.killed) {
                m_soundRegistry->playEvent("combat.death.scream", pos);
            } else {
                m_soundRegistry->playEvent("combat.grunt.pain", pos);
            }
        }
    }

    if (m_onDamage) m_onDamage(event);
    return event;
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
