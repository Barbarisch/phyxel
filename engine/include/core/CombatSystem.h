#pragma once

#include "core/DamageTypes.h"

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

namespace Scene { class Entity; }
namespace Core { class EntityRegistry; }
namespace Core { class SoundRegistry; }

namespace Core {

// ============================================================================
// DamageEvent — dispatched when damage is dealt
// ============================================================================
struct DamageEvent {
    std::string attackerId;
    std::string targetId;
    float amount = 0.0f;
    float actualDamage = 0.0f; // After armor/resistance
    DamageType type = DamageType::Physical;
    glm::vec3 knockback{0.0f};
    bool killed = false;
    std::string hitBone;  // Name of the bone AABB that was hit (empty if not bone-tested)

    nlohmann::json toJson() const {
        nlohmann::json j = {
            {"attackerId", attackerId},
            {"targetId", targetId},
            {"amount", amount},
            {"actualDamage", actualDamage},
            {"type", damageTypeToString(type)},
            {"killed", killed}
        };
        if (!hitBone.empty()) j["hitBone"] = hitBone;
        return j;
    }
};

// ============================================================================
// CombatSystem — handles attack resolution and damage delivery
// ============================================================================
class CombatSystem {
public:
    /// Attack parameters for a single strike.
    struct AttackParams {
        std::string attackerId;
        glm::vec3 attackerPos{0.0f};
        glm::vec3 attackerForward{0.0f, 0.0f, 1.0f};
        float damage = 1.0f;
        float reach = 1.5f;
        float coneAngleDeg = 90.0f;    // How wide the attack cone is
        DamageType damageType = DamageType::Physical;
        float knockbackForce = 2.0f;
        // The attacker entity, excluded from its own hit by pointer. Robust
        // when attackerId is a display label (e.g. "player") that differs from
        // the attacker's registered id — id-only self-exclusion would miss it
        // and the attacker would strike (and knock back) itself.
        const Scene::Entity* attackerEntity = nullptr;
    };

    /// Perform an attack: find targets in range/cone, deal damage.
    /// Returns list of DamageEvents for all entities hit.
    std::vector<DamageEvent> performAttack(
        const AttackParams& params,
        EntityRegistry& registry);

    /// Single entry point for dealing damage to one entity. Mutates the
    /// target's HealthComponent, sets its i-frame timer, applies knockback,
    /// builds the DamageEvent, and dispatches the onDamage callback. Both the
    /// real-time melee path (performAttack) and turn-based / scripted callers
    /// route through here, so death, hit reactions, and events stay consistent
    /// (single source of truth — see docs/TurnBasedCombat.md S2).
    ///
    /// Returns the resulting event. If the target is null, has no health, or is
    /// already dead, returns an event with actualDamage 0 and does NOT dispatch.
    DamageEvent applyDamage(
        Scene::Entity* target,
        const std::string& targetId,
        float amount,
        const std::string& sourceId,
        DamageType type = DamageType::Physical,
        const glm::vec3& knockback = glm::vec3(0.0f),
        const std::string& hitBone = "",
        bool weaponsClash = false);   ///< target was MID-SWING too → metal clang, not flesh

    /// Get/set invulnerability duration (seconds after taking damage).
    float getInvulnerabilityDuration() const { return m_invulnDuration; }
    void setInvulnerabilityDuration(float seconds) { m_invulnDuration = seconds; }

    /// Check if an entity is still in invulnerability frames.
    bool isInvulnerable(const std::string& entityId) const;

    /// Update invulnerability timers. Call each frame.
    void update(float dt);

    /// Callback for damage events (for game events, sound, VFX, etc.)
    using OnDamageCallback = std::function<void(const DamageEvent&)>;
    void setOnDamage(OnDamageCallback cb) { m_onDamage = std::move(cb); }

    /// Combat audio (impact thud/clang, pain grunt, death scream) emitted at
    /// the target's position from inside applyDamage — the single damage entry
    /// point — so EVERY host (editor and standalone/packaged games) gets
    /// combat sound without per-host wiring. Optional: null = silent combat
    /// (the pre-catalog behavior).
    void setSoundRegistry(SoundRegistry* registry) { m_soundRegistry = registry; }
    /// For combat behaviors (swing whoosh, battle cries) — they already carry
    /// a CombatSystem* in NPCContext, so this needs no extra plumbing.
    SoundRegistry* getSoundRegistry() const { return m_soundRegistry; }

    /// Optional external invulnerability predicate. When set and it returns true
    /// for a candidate target, the hit is skipped (in addition to the post-hit
    /// i-frame timers). Used for dodge i-frames: the host checks the target's
    /// AnimatedVoxelCharacter::isDodgeInvulnerable(). Decoupled so CombatSystem
    /// has no scene dependency and the same hook covers player AND NPC dodges.
    using InvulnerabilityQuery = std::function<bool(const Scene::Entity*)>;
    void setInvulnerabilityQuery(InvulnerabilityQuery q) { m_invulnQuery = std::move(q); }

private:
    float m_invulnDuration = 0.5f;
    std::unordered_map<std::string, float> m_invulnTimers; // entityId → remaining time
    OnDamageCallback m_onDamage;
    InvulnerabilityQuery m_invulnQuery;
    SoundRegistry* m_soundRegistry = nullptr;
};

} // namespace Core
} // namespace Phyxel
